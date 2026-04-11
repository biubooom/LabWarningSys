#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "onenet_mqtt.h"
#include "wifi_sta.h"

static onenet_telemetry_t s_telemetry = {
    .temperature = 25.0f,
    .humidity = 50.0f,
    .light = 60.0f,
    .smoke = 10.0f,
    .alarm = false,
};

/**
  * @brief  周期性构造示例遥测数据并上报到 OneNET
  * @param  arg: 任务参数，当前未使用
  * @retval 无
  */
static void telemetry_task(void *arg)
{
    while (1) {
        s_telemetry.temperature += 0.2f;
        if (s_telemetry.temperature > 30.0f) {
            s_telemetry.temperature = 25.0f;
        }

        s_telemetry.humidity += 0.5f;
        if (s_telemetry.humidity > 70.0f) {
            s_telemetry.humidity = 50.0f;
        }

        s_telemetry.light += 1.0f;
        if (s_telemetry.light > 100.0f) {
            s_telemetry.light = 60.0f;
        }

        s_telemetry.smoke += 0.8f;
        if (s_telemetry.smoke > 35.0f) {
            s_telemetry.smoke = 10.0f;
        }

        s_telemetry.alarm = (s_telemetry.smoke >= 25.0f);
        onenet_mqtt_publish_telemetry(&s_telemetry);
        vTaskDelay(pdMS_TO_TICKS(5000));
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
    xTaskCreate(telemetry_task, "telemetry_task", 4096, NULL, 5, NULL);
}
