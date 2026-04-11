#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "app_config.h"
#include "onenet_mqtt.h"
#include "wifi_sta.h"

static const char *TAG = "uart_bridge";

/**
  * @brief  初始化与STM32通信的UART
  * @param  无
  * @retval ESP_OK: 初始化成功
  *         其他: 初始化失败
  */
static esp_err_t uart_bridge_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = APP_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(APP_UART_PORT_NUM,
                                        APP_UART_BUFFER_SIZE,
                                        0,
                                        0,
                                        NULL,
                                        0));
    ESP_ERROR_CHECK(uart_param_config(APP_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(APP_UART_PORT_NUM,
                                 APP_UART_TX_PIN,
                                 APP_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    return ESP_OK;
}

/**
  * @brief  解析STM32发送的一帧JSON数据
  * @param  line: 以'\0'结尾的串口文本帧
  * @param  telemetry: 解析结果输出指针
  * @retval true: 解析成功
  *         false: 解析失败
  */
static bool parse_uart_frame(const char *line, onenet_telemetry_t *telemetry)
{
    unsigned int alarm = 0;
    float humidity = 0.0f;
    float light = 0.0f;
    float smoke = 0.0f;
    float temperature = 0.0f;
    int matched;

    if (line == NULL || telemetry == NULL) {
        return false;
    }

    matched = sscanf(line,
                     "{\"alarm\":%u,\"humidity\":%f,\"light\":%f,\"smoke\":%f,\"temperature\":%f}",
                     &alarm,
                     &humidity,
                     &light,
                     &smoke,
                     &temperature);
    if (matched != 5) {
        return false;
    }

    telemetry->alarm = (alarm != 0U);
    telemetry->humidity = humidity;
    telemetry->light = light;
    telemetry->smoke = smoke;
    telemetry->temperature = temperature;
    return true;
}

/**
  * @brief  从UART接收STM32数据并上报至OneNET
  * @param  arg: 任务参数，当前未使用
  * @retval 无
  */
static void uart_rx_task(void *arg)
{
    uint8_t rx_data[APP_UART_BUFFER_SIZE];
    char line_buffer[256];
    size_t line_length = 0;

    (void)arg;

    while (1) {
        int rx_length = uart_read_bytes(APP_UART_PORT_NUM,
                                        rx_data,
                                        sizeof(rx_data),
                                        pdMS_TO_TICKS(1000));
        if (rx_length <= 0) {
            continue;
        }

        for (int i = 0; i < rx_length; ++i) {
            char ch = (char)rx_data[i];

            if (ch == '\r') {
                continue;
            }

            if (ch == '\n') {
                onenet_telemetry_t telemetry;

                line_buffer[line_length] = '\0';
                if (line_length > 0U) {
                    if (parse_uart_frame(line_buffer, &telemetry)) {
                        ESP_LOGI(TAG,
                                 "uart data: alarm=%d humidity=%.1f light=%.1f smoke=%.1f temperature=%.1f",
                                 telemetry.alarm,
                                 telemetry.humidity,
                                 telemetry.light,
                                 telemetry.smoke,
                                 telemetry.temperature);
                        onenet_mqtt_publish_telemetry(&telemetry);
                    } else {
                        ESP_LOGW(TAG, "invalid uart frame: %s", line_buffer);
                    }
                }
                line_length = 0U;
                continue;
            }

            if (line_length + 1U < sizeof(line_buffer)) {
                line_buffer[line_length++] = ch;
            } else {
                ESP_LOGW(TAG, "uart frame too long, drop current line");
                line_length = 0U;
            }
        }
    }
}

/**
  * @brief  应用程序主入口
  * @param  无
  * @retval 无
  */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 先联网，再启动云平台连接。 */
    ESP_ERROR_CHECK(wifi_sta_init());
    ESP_ERROR_CHECK(onenet_mqtt_start());
    ESP_ERROR_CHECK(uart_bridge_init());
    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, NULL);
}
