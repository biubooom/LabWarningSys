#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "sensor_data.h"

/**
  * @brief  OneNET单组上报数据
  * @note   字段含义与STM32上传的四组原始数据保持一致
  */
typedef struct {
    /*!< 当前组是否在线 */
    bool online;
    /*!< 当前组温度 */
    float temperature;
    /*!< 当前组湿度 */
    float humidity;
    /*!< 当前组光照 */
    float light;
    /*!< 当前组烟雾 */
    float smoke;
    /*!< 当前组告警状态 */
    bool alarm;
} onenet_group_telemetry_t;

/**
  * @brief  OneNET四组汇总上报数据
  * @note   ESP32侧根据原始采样值计算告警，再把四组状态统一上报到平台
  */
typedef struct {
    /*!< 四组传感器快照 */
    onenet_group_telemetry_t groups[SENSOR_GROUP_COUNT];
    /*!< 串口链路是否在线 */
    bool link_online;
    /*!< 系统总告警状态 */
    bool system_alarm;
} onenet_snapshot_t;

/**
  * @brief  启动OneNET MQTT连接
  * @param  无
  * @retval ESP_OK: 启动成功
  *         其他: 启动失败
  */
esp_err_t onenet_mqtt_start(void);

/**
  * @brief  上报四组遥测数据到OneNET
  * @param  snapshot: 待上报的四组快照
  * @retval ESP_OK: 上报成功
  *         ESP_ERR_INVALID_ARG: 参数无效
  *         ESP_FAIL: 上报失败
  */
esp_err_t onenet_mqtt_publish_snapshot(const onenet_snapshot_t *snapshot);
