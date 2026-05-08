#ifndef __DS18B20_H
#define __DS18B20_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "Onewire.h"

typedef enum
{
    DS18B20_OK = 0,
    DS18B20_ERROR
} DS18B20_Status_t;

#define DS18B20_ROM_CODE_SIZE ONEWIRE_ROM_CODE_SIZE

/**
  * @brief  初始化DS18B20驱动
  * @param  无
  * @retval DS18B20_OK: 初始化成功
  *         DS18B20_ERROR: 初始化失败
  */
DS18B20_Status_t DS18B20_Init(void);

/**
  * @brief  读取DS18B20温度
  * @param  Temperature: 温度值输出，单位摄氏度
  * @retval DS18B20_OK: 读取成功
  *         DS18B20_ERROR: 读取失败
  */
DS18B20_Status_t DS18B20_ReadTemperature(float *Temperature);

DS18B20_Status_t DS18B20_SearchRomCodes(uint8_t (*RomCodes)[DS18B20_ROM_CODE_SIZE], uint8_t MaxDevices, uint8_t *FoundDevices);

DS18B20_Status_t DS18B20_StartAllConversion(void);

DS18B20_Status_t DS18B20_ReadTemperatureByRom(const uint8_t RomCode[DS18B20_ROM_CODE_SIZE], float *Temperature);

DS18B20_Status_t DS18B20_ReadTemperatureByRomWithoutConvert(const uint8_t RomCode[DS18B20_ROM_CODE_SIZE], float *Temperature);

#ifdef __cplusplus
}
#endif

#endif /* __DS18B20_H */
