#ifndef SIM_H
#define SIM_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define SIM_UART                UART_NUM_1
#define SIM_TX_PIN              21
#define SIM_RX_PIN              20
#define SIM_BAUDRATE            115200
#define BUF_SIZE                1024

#define MCU_SIM_EN_PIN          2
#define MCU_SIM_DTR_PIN         0
#define LED_onboard_ESP32       10
#define BUZZER                  7
#define PIN_MAH                 1
#define PHONE_MAIN              "0344651393"

void sim_uart_init(void);
void sim_gpio_init(void);
void sim_init(void);

int sim_send_cmd(const char *cmd);
bool sim_send_cmd_with_resp(const char *cmd, char *out_buf, size_t buf_size);
int sim_read_response(char *resp_buf, size_t max_len, TickType_t timeout_ms);

void sim_send_sms(const char *phone_number, const char *msg);
bool sim_delete_sms();

bool parse_sms_phone(const char *input, char *phone_out);
bool parse_sms_message(const char *input, char *msg_out);

char *convert_to_decimal(const char *coord, char direction);
char *get_gps_location(void);

#endif
