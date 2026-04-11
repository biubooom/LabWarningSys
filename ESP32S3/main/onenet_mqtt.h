#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
  * @brief  OneNET遥测数据结构体
  * @note   结构体成员需与平台物模型标识符保持一致
  */
typedef struct {
    /*!< 温度数据 */
    float temperature;
    /*!< 湿度数据 */
    float humidity;
    /*!< 光敏数据 */
    float light;
    /*!< 烟雾数据 */
    float smoke;
    /*!< 报警状态 */
    bool alarm;
} onenet_telemetry_t;

/**
  * @brief  启动OneNET MQTT连接
  * @param  无
  * @retval ESP_OK: 启动成功
  *         其他: 启动失败
  */
esp_err_t onenet_mqtt_start(void);

/**
  * @brief  上报一组遥测数据到OneNET
  * @param  telemetry: 待上报的遥测数据指针
  * @retval ESP_OK: 上报成功
  *         ESP_ERR_INVALID_ARG: 参数无效
  *         ESP_FAIL: 上报失败
  */
esp_err_t onenet_mqtt_publish_telemetry(const onenet_telemetry_t *telemetry);
