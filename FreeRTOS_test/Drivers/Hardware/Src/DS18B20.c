#include "DS18B20.h"

#include "Onewire.h"

#include "FreeRTOS.h"
#include "task.h"

#define DS18B20_CMD_SKIP_ROM        0xCCU
#define DS18B20_CMD_MATCH_ROM       0x55U
#define DS18B20_CMD_CONVERT_T       0x44U
#define DS18B20_CMD_READ_SCRATCHPAD 0xBEU
#define DS18B20_SCRATCHPAD_SIZE     9U

/**
  * @brief  读取指定DS18B20的Scratchpad数据
  * @param  rom_code: 目标器件ROM地址，传NULL时广播到总线单设备
  * @param  scratchpad: 接收Scratchpad数据的缓冲区，长度9字节
  * @note   当前工程中多个DS18B20共用一根OW_DQ总线，群组场景下应传入ROM地址
  * @retval DS18B20_OK: 读取成功
  *         DS18B20_ERROR: 读取失败
  */
static DS18B20_Status_t DS18B20_ReadScratchpad(const uint8_t *rom_code, uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE])
{
    uint8_t i;

    if (scratchpad == NULL)
    {
        return DS18B20_ERROR;
    }

    Onewire_Lock();

    if (Onewire_Reset() == 0U)
    {
        Onewire_Unlock();
        return DS18B20_ERROR;
    }

    /* rom_code 为空时表示广播到总线上全部器件，否则只匹配指定探头 */
    if (rom_code == NULL)
    {
        Onewire_WriteByte(DS18B20_CMD_SKIP_ROM);
    }
    else
    {
        Onewire_WriteByte(DS18B20_CMD_MATCH_ROM);
        for (i = 0U; i < DS18B20_ROM_CODE_SIZE; i++)
        {
            Onewire_WriteByte(rom_code[i]);
        }
    }

    /* 温度转换较慢，发起后先释放总线给其他任务 */
    Onewire_WriteByte(DS18B20_CMD_CONVERT_T);
    Onewire_Unlock();

    vTaskDelay(pdMS_TO_TICKS(750U));

    Onewire_Lock();
    if (Onewire_Reset() == 0U)
    {
        Onewire_Unlock();
        return DS18B20_ERROR;
    }

    if (rom_code == NULL)
    {
        Onewire_WriteByte(DS18B20_CMD_SKIP_ROM);
    }
    else
    {
        Onewire_WriteByte(DS18B20_CMD_MATCH_ROM);
        for (i = 0U; i < DS18B20_ROM_CODE_SIZE; i++)
        {
            Onewire_WriteByte(rom_code[i]);
        }
    }

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

    return DS18B20_OK;
}

/**
  * @brief  初始化DS18B20驱动
  * @param  无
  * @note   该函数只初始化底层1-Wire总线与互斥资源，不区分具体传感器组。
  *         当前工程中4个DS18B20共用同一根OW_DQ总线，后续通过ROM地址区分各设备。
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
  * @note   该接口适用于总线上只有一个DS18B20的场景，多设备时应使用按ROM读取接口
  * @retval DS18B20_OK: 读取成功
  *         DS18B20_ERROR: 读取失败
  */
DS18B20_Status_t DS18B20_ReadTemperature(float *Temperature)
{
    uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE];
    int16_t raw_temperature;

    if (Temperature == NULL)
    {
        return DS18B20_ERROR;
    }

    if (DS18B20_ReadScratchpad(NULL, scratchpad) != DS18B20_OK)
    {
        return DS18B20_ERROR;
    }

    raw_temperature = (int16_t)(((uint16_t)scratchpad[1] << 8U) | scratchpad[0]);
    *Temperature = (float)raw_temperature / 16.0f;

    return DS18B20_OK;
}

/**
  * @brief  搜索单总线上的全部DS18B20 ROM地址
  * @param  RomCodes: ROM地址输出缓冲区
  * @param  MaxDevices: 最多搜索的设备数量
  * @param  FoundDevices: 实际搜索到的设备数量输出
  * @note   用于四组共线场景下建立“插槽到ROM”的对应关系
  * @retval DS18B20_OK: 搜索成功
  *         DS18B20_ERROR: 搜索失败
  */
DS18B20_Status_t DS18B20_SearchRomCodes(uint8_t (*RomCodes)[DS18B20_ROM_CODE_SIZE], uint8_t MaxDevices, uint8_t *FoundDevices)
{
    /* 直接复用底层单总线搜索流程，对上层屏蔽 Dallas Search ROM 细节 */
    return (Onewire_SearchRomCodes(RomCodes, MaxDevices, FoundDevices) == ONEWIRE_OK) ? DS18B20_OK : DS18B20_ERROR;
}

/**
  * @brief  按ROM地址读取指定DS18B20温度
  * @param  RomCode: 目标器件的8字节ROM地址
  * @param  Temperature: 温度值输出，单位摄氏度
  * @note   当前四组DS18B20共线时应优先使用该接口，避免多探头读值串组
  * @retval DS18B20_OK: 读取成功
  *         DS18B20_ERROR: 读取失败
  */
DS18B20_Status_t DS18B20_ReadTemperatureByRom(const uint8_t RomCode[DS18B20_ROM_CODE_SIZE], float *Temperature)
{
    uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE];
    int16_t raw_temperature;

    if ((RomCode == NULL) || (Temperature == NULL))
    {
        return DS18B20_ERROR;
    }

    if (DS18B20_ReadScratchpad(RomCode, scratchpad) != DS18B20_OK)
    {
        return DS18B20_ERROR;
    }

    raw_temperature = (int16_t)(((uint16_t)scratchpad[1] << 8U) | scratchpad[0]);
    *Temperature = (float)raw_temperature / 16.0f;
    return DS18B20_OK;
}
