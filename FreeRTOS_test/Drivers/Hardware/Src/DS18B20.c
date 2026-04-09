#include "DS18B20.h"

#include "Onewire.h"

#include "FreeRTOS.h"
#include "task.h"

#define DS18B20_CMD_SKIP_ROM        0xCCU
#define DS18B20_CMD_CONVERT_T       0x44U
#define DS18B20_CMD_READ_SCRATCHPAD 0xBEU
#define DS18B20_SCRATCHPAD_SIZE     9U

/**
  * @brief  初始化DS18B20驱动
  * @param  无
  * @retval DS18B20_OK: 初始化成功
  *         DS18B20_ERROR: 初始化失败
  */
DS18B20_Status_t DS18B20_Init(void)
{
    if (Onewire_Init() != ONEWIRE_OK)
    {
        return DS18B20_ERROR;
    }

    Onewire_Lock();
    if (Onewire_Reset() == 0U)
    {
        Onewire_Unlock();
        return DS18B20_ERROR;
    }
    Onewire_Unlock();

    return DS18B20_OK;
}

/**
  * @brief  读取DS18B20温度
  * @param  Temperature: 温度值输出，单位摄氏度
  * @retval DS18B20_OK: 读取成功
  *         DS18B20_ERROR: 读取失败
  */
DS18B20_Status_t DS18B20_ReadTemperature(float *Temperature)
{
    uint8_t i;
    uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE];
    int16_t raw_temperature;

    if (Temperature == NULL)
    {
        return DS18B20_ERROR;
    }

    Onewire_Lock();

    if (Onewire_Reset() == 0U)
    {
        Onewire_Unlock();
        return DS18B20_ERROR;
    }

    Onewire_WriteByte(DS18B20_CMD_SKIP_ROM);
    Onewire_WriteByte(DS18B20_CMD_CONVERT_T);
    Onewire_Unlock();

    vTaskDelay(pdMS_TO_TICKS(750U));

    Onewire_Lock();
    if (Onewire_Reset() == 0U)
    {
        Onewire_Unlock();
        return DS18B20_ERROR;
    }

    Onewire_WriteByte(DS18B20_CMD_SKIP_ROM);
    Onewire_WriteByte(DS18B20_CMD_READ_SCRATCHPAD);
    for (i = 0U; i < DS18B20_SCRATCHPAD_SIZE; i++)
    {
        scratchpad[i] = Onewire_ReadByte();
    }
    Onewire_Unlock();

    if (Onewire_Crc8(scratchpad, DS18B20_SCRATCHPAD_SIZE - 1U) != scratchpad[DS18B20_SCRATCHPAD_SIZE - 1U])
    {
        return DS18B20_ERROR;
    }

    raw_temperature = (int16_t)(((uint16_t)scratchpad[1] << 8U) | scratchpad[0]);
    *Temperature = (float)raw_temperature / 16.0f;

    return DS18B20_OK;
}
