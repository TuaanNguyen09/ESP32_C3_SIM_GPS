#include "SIM.h"
#include "esp_sleep.h"


EventGroupHandle_t xSmsEventGroup;
#define SMS_RECEIVED_BIT    (1 << 0)
#define SMS_PROCESSED_BIT   (1 << 1)
#define BUZZER_ON_BIT       (1 << 2)
#define READY_TO_SLEEP_BIT  (1 << 3)
#define NEED_GPS_BIT        (1 << 4)

static SemaphoreHandle_t xSmsMutex;

TimerHandle_t xAutoOffTimer;

static const char *TAG = "MAIN";

char g_sms_phone[20] = {0};
char g_sms_message[160] = {0};

char GPS_phone[20] = {0};

void task_GPS(void *param)
{
    static const char *TAG_GPS = "GET_GPS";
    while (1) {
        ESP_LOGI(TAG_GPS,"Bắt đầu lấy GPS");
        EventBits_t bits = xEventGroupWaitBits(
            xSmsEventGroup,
            NEED_GPS_BIT,
            pdTRUE,    // clear bit sau khi nhận
            pdFALSE,
            portMAX_DELAY
        );

        if (bits & NEED_GPS_BIT) {
            // thử lấy GPS nhiều lần
            char *loc = NULL;
            for (int i = 0; i < 30; i++) {
                loc = get_gps_location();
                if (loc && strlen(loc) > 0) break;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            if (loc) {
                sim_send_sms(GPS_phone, loc);
                GPS_phone[0]= '\0';
            }
        }
    }
}


void task_receive_sms(void *param)
{
    static const char *TAG_RECEIVE_SMS = "Receive_SMS";
    char buf[BUF_SIZE] = {0};

    while (1) {
        get_gps_location();
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (sim_send_cmd_with_resp("AT+CMGL=\"ALL\"", buf, sizeof(buf))) {
            char phone[20] = {0};
            char message[160] = {0};

            if (strstr(buf, "+CMGL:") && parse_sms_phone(buf, phone) && parse_sms_message(buf, message)) {
                xSemaphoreTake(xSmsMutex, portMAX_DELAY);
                strncpy(g_sms_phone, phone, sizeof(g_sms_phone));
                strncpy(g_sms_message, message, sizeof(g_sms_message));
                xSemaphoreGive(xSmsMutex);

                // Báo cho handle_sms biết có SMS mới
                xEventGroupSetBits(xSmsEventGroup, SMS_RECEIVED_BIT);
                ESP_LOGI(TAG_RECEIVE_SMS,"📩 Tin nhắn mới, chờ xử lý...");

                // Dừng lại chờ xử lý xong
                xEventGroupWaitBits(
                    xSmsEventGroup,
                    SMS_PROCESSED_BIT,
                    pdTRUE,    // Clear bit khi đã xử lý
                    pdTRUE,
                    portMAX_DELAY
                );
                ESP_LOGI(TAG_RECEIVE_SMS,"✅ Tin nhắn đã xử lý, tiếp tục nhận...");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000));  
    }
}



void task_handle_sms(void *param)
{
    static const char *TAG_HANDLE_SMS = "Handle_SMS";
    char phone[20] = {0};
    char message[160] = {0};

    while (1) {
        // Chờ có SMS mới
        xEventGroupWaitBits(
            xSmsEventGroup,
            SMS_RECEIVED_BIT,
            pdTRUE,   // clear bit sau khi nhận
            pdTRUE,
            portMAX_DELAY
        );

        xSemaphoreTake(xSmsMutex, portMAX_DELAY);
        strncpy(phone, g_sms_phone, sizeof(phone));
        strncpy(message, g_sms_message, sizeof(message));
        g_sms_phone[0] = '\0';
        g_sms_message[0] = '\0';
        xSemaphoreGive(xSmsMutex);

        ESP_LOGI(TAG_HANDLE_SMS,"📲 SĐT: %s", phone);
        ESP_LOGI(TAG_HANDLE_SMS,"💬 Tin nhắn: %s", message);

        if (strcasecmp(message, "Loc") == 0) {
            
            //char *loc = get_gps_location();
            //if (loc && strlen(loc) > 0) {
                //char loc_copy[128];
                //snprintf(loc_copy, sizeof(loc_copy), "%s", loc);
                //ESP_LOGI(TAG_HANDLE_SMS, "LOCATION📡: %s", loc_copy);
                //sim_send_sms(phone, loc_copy);
                strcpy(GPS_phone,phone);
                xEventGroupSetBits(xSmsEventGroup, NEED_GPS_BIT);
                ESP_LOGI(TAG_HANDLE_SMS, "🔊CÒI ĐÃ BẬT");
                gpio_set_level(LED_onboard_ESP32, 1); 
                gpio_set_level(BUZZER, 1);
                xEventGroupSetBits(xSmsEventGroup, BUZZER_ON_BIT);          // Set cờ BUZZER_ON_BIT = 1 Báo còi đang bật
                xEventGroupClearBits(xSmsEventGroup, READY_TO_SLEEP_BIT);   // Set cờ Ready_to_sleep bit 2 = 0 chưa cho ngủ
                // Reset lại timer (dừng rồi bắt đầu lại) 
                xTimerReset(xAutoOffTimer, 0);
                //xTimerStart(xAutoOffTimer, 0);
            //}
        }
        else if (strcasecmp(message, "On") == 0) {
            gpio_set_level(LED_onboard_ESP32, 1); 
            gpio_set_level(BUZZER, 1);
            ESP_LOGI(TAG_HANDLE_SMS, "🔊CÒI ĐÃ BẬT");
            sim_send_sms(phone, "Coi da bat"); 
            // (bật còi logic giữ nguyên)
            xEventGroupSetBits(xSmsEventGroup, BUZZER_ON_BIT);              // Set cờ BUZZER_ON_BIT = 1 Báo còi đang bật
            xEventGroupClearBits(xSmsEventGroup, READY_TO_SLEEP_BIT);       // Set cờ Ready_to_sleep bit 2 = 0 chưa cho ngủ
            // Reset lại timer (dừng rồi bắt đầu lại) 
            xTimerReset(xAutoOffTimer, 0);
            //xTimerStart(xAutoOffTimer, 0);
        }
        else if (strcasecmp(message, "Off") == 0) {
            gpio_set_level(LED_onboard_ESP32, 0); 
            gpio_set_level(BUZZER, 0);
            ESP_LOGI(TAG_HANDLE_SMS, "🔕CÒI ĐÃ TẮT");
            sim_send_sms(phone, "Coi da tat");
            // (tắt còi logic giữ nguyên)
            xEventGroupClearBits(xSmsEventGroup, BUZZER_ON_BIT);            //Set cờ BUZZER_ON_BIT = 0 Báo còi đã tắt
        }
        else {
            ESP_LOGI(TAG_HANDLE_SMS,"Tin nhắn không hợp lệ❌⛔❌");
            // sim_send_sms(phone, "Tin nhan khong hop le.");
        }
        phone[0] = '\0';
        message[0] = '\0';
        sim_delete_sms();
        
        xEventGroupSetBits(xSmsEventGroup, SMS_PROCESSED_BIT);         // Báo đã xử lý xong SMS → cho receive_sms chạy tiếp
    }
}


void task_sleep_manager(void *param)
{   
    static const char *TAG_DEEP_SLEEP = "DEEP_SLEEP";
    const int awake_time_ms = 45 * 1000;
    ESP_LOGI(TAG_DEEP_SLEEP, "Task Deep Sleep READY");
    vTaskDelay(pdMS_TO_TICKS(awake_time_ms));               // Thức 30 giây

    while (1){
        EventBits_t bits = xEventGroupGetBits(xSmsEventGroup);

        if ((bits & BUZZER_ON_BIT) == 0) {
            ESP_LOGI(TAG_DEEP_SLEEP, "😴 Không có còi, đi ngủ ngay...");
            esp_deep_sleep_start();
        }

        ESP_LOGI(TAG_DEEP_SLEEP, "🔕 Đang đợi còi tắt để deep sleep...");

        // Đợi đến khi READY_TO_SLEEP_BIT được set bởi timer hoặc lệnh "off"
        xEventGroupWaitBits(
            xSmsEventGroup,
            READY_TO_SLEEP_BIT,
            pdTRUE,
            pdTRUE,
            portMAX_DELAY
        );

        ESP_LOGI(TAG_DEEP_SLEEP, "✅ Còi đã tắt, đi vào deep sleep...");
        esp_deep_sleep_start();
    }
}


void auto_off_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI("TIMER", "⏰ Hết giờ, tắt còi");
    gpio_set_level(LED_onboard_ESP32, 0);
    gpio_set_level(BUZZER, 0);
    xEventGroupClearBits(xSmsEventGroup, BUZZER_ON_BIT);         // Báo còi đã tắt
    xEventGroupSetBits(xSmsEventGroup, READY_TO_SLEEP_BIT);      // Cho phép ngủ
}


void app_main(void)
{   
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "🔄 Khởi động ESP32...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "📡 Khởi động module SIM...");
    sim_init();

    // config chế độ deepsleep dùng timer
    esp_sleep_enable_timer_wakeup(80 * 1000000ULL); // Ngủ 60 seconds

    // Tạo mutex
    xSmsMutex = xSemaphoreCreateMutex();
    if (xSmsMutex == NULL) {
        ESP_LOGE(TAG, "❌ Không tạo được mutex!");
        return;
    }

    // Tạo EventGroups để set cờ cho hàm receive 
    xSmsEventGroup = xEventGroupCreate();
    if (xSmsEventGroup == NULL) {
        ESP_LOGE(TAG, "❌ Không tạo được Eventgroup!");
        return;
    }

     // Tạo SoftTimer để đếm thời gian bật còi
    xAutoOffTimer = xTimerCreate(
        "AutoOff",                          // Tên timer
        pdMS_TO_TICKS(2 * 60 * 1000),       // 3 phút
        pdFALSE,                            // One-shot
        NULL,
        auto_off_timer_callback
    );
    if (xAutoOffTimer == NULL) {
        ESP_LOGE(TAG, "Không tạo được timer!");
        return;
    }

    // Tạo task nhận và xử lý SMS
    xTaskCreate(task_receive_sms, "task_receive_sms", 4096, NULL, 10, NULL);
    xTaskCreate(task_handle_sms, "task_handle_sms", 4096, NULL, 9, NULL);
    xTaskCreate(task_sleep_manager, "task_sleep_manager", 4096, NULL, 8, NULL);
    xTaskCreate(task_GPS, "task_get_gps", 4096, NULL, 8, NULL);

    ESP_LOGI(TAG, "✅ Đã tạo task nhận và xử lý SMS");
}