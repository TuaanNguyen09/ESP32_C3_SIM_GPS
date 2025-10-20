#include "SIM.h"

static const char *  TAG_SIM = "SIM";

#define RESP_BUF_SIZE 512

// Hàm đọc phản hồi từ UART sau khi gửi lệnh AT
int sim_read_response(char *resp_buf, size_t max_len, TickType_t timeout_ms)
{
    int len = uart_read_bytes(SIM_UART, (uint8_t *)resp_buf, max_len - 1, pdMS_TO_TICKS(timeout_ms));
    if (len > 0) {
        resp_buf[len] = '\0';
        ESP_LOGI(TAG_SIM, "📥 Phản hồi: %s", resp_buf);
    } else {
        resp_buf[0] = '\0';
        ESP_LOGW(TAG_SIM, "⚠️ Không có phản hồi từ SIM");
    }
    return len;
}

// Gửi lệnh AT + in log
int sim_send_cmd(const char *cmd)
{
    char full_cmd[128];
    snprintf(full_cmd, sizeof(full_cmd), "%s\r\n", cmd);

    ESP_LOGI(    TAG_SIM, "📤 Gửi lệnh: %s", cmd);
    uart_write_bytes(SIM_UART, full_cmd, strlen(full_cmd));

    char resp[RESP_BUF_SIZE] = {0};
    return sim_read_response(resp, sizeof(resp), 1000);
}

// Gửi lệnh AT và nhận phản hồi
bool sim_send_cmd_with_resp(const char *cmd, char *out_buf, size_t buf_size)
{
    char full_cmd[128];
    snprintf(full_cmd, sizeof(full_cmd), "%s\r\n", cmd);

    ESP_LOGI(TAG_SIM, "📤 Gửi lệnh: %s", cmd);
    uart_write_bytes(SIM_UART, full_cmd, strlen(full_cmd));

    int len = sim_read_response(out_buf, buf_size, 1000);
    return len > 0;
}

void sim_uart_init()
{
    const uart_config_t uart_config = {
        .baud_rate = SIM_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_driver_install(SIM_UART, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(SIM_UART, &uart_config);
    uart_set_pin(SIM_UART, SIM_TX_PIN, SIM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    sim_send_cmd("AT");
}

void sim_gpio_init()
{
    gpio_set_direction(MCU_SIM_EN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(MCU_SIM_DTR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BUZZER, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_onboard_ESP32, GPIO_MODE_OUTPUT);

    gpio_set_level(MCU_SIM_EN_PIN, 1);
    gpio_set_level(MCU_SIM_DTR_PIN, 1);
    gpio_set_level(LED_onboard_ESP32, 1);
    gpio_set_level(BUZZER, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    gpio_set_level(LED_onboard_ESP32, 0);
}

void sim_send_sms(const char *phone_number, const char *message)
{
    char cmd[64];

    // 1. Đặt chế độ SMS Text
    uart_write_bytes(SIM_UART, "AT+CMGF=1\r\n", strlen("AT+CMGF=1\r\n"));
    vTaskDelay(pdMS_TO_TICKS(500));  // Chờ SIM phản ứng

    // 2. Gửi lệnh gửi tin nhắn
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r\n", phone_number);
    uart_write_bytes(SIM_UART, cmd, strlen(cmd));
    vTaskDelay(pdMS_TO_TICKS(500));  // Chờ ký tự '>'

    // 3. Gửi nội dung và kết thúc bằng Ctrl+Z
    uart_write_bytes(SIM_UART, message, strlen(message));
    uart_write_bytes(SIM_UART, "\x1A", 1);  // Ctrl+Z để kết thúc SMS

    // 4. Không cần đọc phản hồi
    vTaskDelay(pdMS_TO_TICKS(1000)); // Chờ SIM xử lý nội bộ
}


bool sim_delete_sms()
{
    char resp[BUF_SIZE] = {0};

    // Gửi lệnh xóa tất cả tin nhắn
    ESP_LOGI(TAG_SIM, "📤 Gửi lệnh: AT+CMGD=1,4");
    uart_write_bytes(SIM_UART, "AT+CMGD=1,4\r\n", strlen("AT+CMGD=1,4\r\n"));

    // Đọc phản hồi
    int len = uart_read_bytes(SIM_UART, (uint8_t *)resp, sizeof(resp) - 1, pdMS_TO_TICKS(2000));
    if (len > 0) {
        resp[len] = '\0';
        ESP_LOGI(TAG_SIM, "📥 Phản hồi xóa: %s", resp);
        if (strstr(resp, "OK")) {
            return true;
        }
    }

    ESP_LOGW(TAG_SIM, "⚠️ Xóa SMS thất bại!");
    return false;
}


char *convert_to_decimal(const char *coord, char direction)
{
    static char result[32];

    int deg_len = (strchr(coord, '.') - coord == 4) ? 2 : 3; // 2 số cho vĩ độ, 3 số cho kinh độ

    char deg_str[4] = {0};
    strncpy(deg_str, coord, deg_len);
    int degrees = atoi(deg_str);

    float minutes = atof(coord + deg_len);
    float decimal = degrees + (minutes / 60.0f);

    if (direction == 'S' || direction == 'W')
        decimal *= -1;

    snprintf(result, sizeof(result), "%.6f", decimal);
    return result;
}

char *get_gps_location(void)
{
    static char gps_url[128];
    char buffer[RESP_BUF_SIZE] = {0};

    // Gửi lệnh và lấy phản hồi vào buffer (hàm sim_send_cmd_with_resp bạn có sẵn)
    sim_send_cmd_with_resp("AT+QGPSLOC=0", buffer, sizeof(buffer));

    // Tìm phần bắt đầu "+QGPSLOC:" trong buffer (loại bỏ lệnh gửi/OK khác)
    char *start = strstr(buffer, "+QGPSLOC:");
    if (!start) {
        ESP_LOGW(TAG_SIM, "⚠️ NOT FOUND GPS");
        return NULL;
    }

    // Bắt đầu từ ngay sau dấu ':' rồi bỏ khoảng trắng
    start = strchr(start, ':');
    if (!start) return NULL;
    start++;
    while (*start == ' ') start++;

    // Parse: bỏ UTC (first field), rồi lấy lat (đến N/S) và lon (đến E/W)
    char lat_raw[32] = {0}, lon_raw[32] = {0};
    char lat_dir = 0, lon_dir = 0;
    // %*[^,],  -> bỏ field đầu (UTC) tới dấu phẩy
    // %31[^NS] -> đọc tới N hoặc S (lat số)
    // %c       -> đọc ký tự N/S
    // %31[^EW] -> đọc tới E hoặc W (lon số)
    // %c       -> đọc ký tự E/W
    int matched = sscanf(start, "%*[^,],%31[^NS]%c,%31[^EW]%c",
                         lat_raw, &lat_dir, lon_raw, &lon_dir);
    if (matched != 4) {
        ESP_LOGW(TAG_SIM, "⚠️ GPS PARSE FAIL (matched=%d): %s", matched, start);
        return NULL;
    }

    // DEBUG: in raw để kiểm tra (bỏ nếu cần)
    //ESP_LOGI(TAG_SIM, "RAW lat=%s%c, lon=%s%c", lat_raw, lat_dir, lon_raw, lon_dir);

    // convert_to_decimal trả về pointer tới buffer static — **phải copy ngay**
    char lat_dec[32] = {0};
    char lon_dec[32] = {0};
    char *tmp;

    tmp = convert_to_decimal(lat_raw, lat_dir); // trả về static buffer
    strncpy(lat_dec, tmp, sizeof(lat_dec) - 1);
    lat_dec[sizeof(lat_dec) - 1] = '\0';

    tmp = convert_to_decimal(lon_raw, lon_dir); // sẽ ghi đè buffer static
    strncpy(lon_dec, tmp, sizeof(lon_dec) - 1);
    lon_dec[sizeof(lon_dec) - 1] = '\0';

    // Tạo url final
    snprintf(gps_url, sizeof(gps_url), "https://www.google.com/maps?q=%s,%s", lat_dec, lon_dec);

    ESP_LOGI(TAG_SIM, "GPS URL: %s", gps_url);
    return gps_url;
}


bool parse_sms_phone(const char *input, char *phone_out)
{
    const char *start = strstr(input, "+CMGL:");
    if (!start) return false;

    for (int i = 0; i < 3; i++) {
        start = strchr(start + 1, '"');
        if (!start) return false;
    }

    const char *quote_end = strchr(start + 1, '"');
    if (!quote_end) return false;

    int len = quote_end - start - 1;
    if (len <= 0 || len >= 20) return false;

    strncpy(phone_out, start + 1, len);
    phone_out[len] = '\0';
    return true;
}

bool parse_sms_message(const char *input, char *msg_out)
{
    const char *msg_start = strstr(input, "\n");
    if (!msg_start) return false;

    msg_start = strchr(msg_start + 1, '\n');
    if (!msg_start) return false;

    msg_start++;
    const char *msg_end = strchr(msg_start, '\r');
    if (!msg_end) msg_end = strchr(msg_start, '\n');
    if (!msg_end) msg_end = msg_start + strlen(msg_start);

    int len = msg_end - msg_start;
    if (len <= 0 || len >= 160) return false;

    strncpy(msg_out, msg_start, len);
    msg_out[len] = '\0';
    return true;
}

void sim_init()
{
    sim_gpio_init();
    sim_uart_init();
    
    ESP_LOGI(TAG_SIM, "🔄 Đang khởi tạo module SIM...");
    sim_send_cmd("AT");
    sim_send_cmd("AT+CPIN?");
    sim_send_cmd("AT+CSQ");
    sim_send_cmd("AT+CREG?");
    sim_send_cmd("AT+CPMS=\"SM\",\"SM\",\"SM\"");
    sim_send_cmd("AT+CMGF=1");
    
    sim_send_cmd("AT+CSCS=\"GSM\"");
    sim_send_cmd("AT+CNMI=2,1,0,0,0");

    sim_send_cmd("AT+QGPS=1");

    vTaskDelay(pdMS_TO_TICKS(5000));
    sim_send_cmd("AT+QGPS?");
    get_gps_location();
    
    ESP_LOGI(TAG_SIM, "✅ SIM init hoàn tất");
}