#ifndef __DHT22_H
#define __DHT22_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
    DHT22_OK = 0,
    DHT22_ERROR
} DHT22_Status_t;

/**
  * @brief  初始化DHT22驱动
  * @param  无
  * @retval DHT22_OK: 初始化成功
  *         DHT22_ERROR: 初始化失败
  */
DHT22_Status_t DHT22_Init(void);

/**
  * @brief  读取DHT22温湿度数据
  * @param  Temperature: 温度输出，单位摄氏度
  * @param  Humidity: 湿度输出，单位%RH
  * @retval DHT22_OK: 读取成功
  *         DHT22_ERROR: 读取失败
  */
DHT22_Status_t DHT22_Read(float *Temperature, float *Humidity);

#ifdef __cplusplus
}
#endif

#endif /* __DHT22_H */
