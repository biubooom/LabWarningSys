#pragma once

#include "esp_err.h"

/**
  * @brief  初始化Wi-Fi STA模式并连接到指定热点
  * @param  无
  * @retval ESP_OK: 连接成功
  *         ESP_FAIL: 连接失败
  *         ESP_ERR_NO_MEM: 内存不足
  */
esp_err_t wifi_sta_init(void);
