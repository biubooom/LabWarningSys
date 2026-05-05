#include "Onewire.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* 单总线引脚控制宏 */
#define ONEWIRE_DQ_HIGH() HAL_GPIO_WritePin(OW_DQ_GPIO_Port, OW_DQ_Pin, GPIO_PIN_SET)
#define ONEWIRE_DQ_LOW()  HAL_GPIO_WritePin(OW_DQ_GPIO_Port, OW_DQ_Pin, GPIO_PIN_RESET)
#define ONEWIRE_DQ_READ() HAL_GPIO_ReadPin(OW_DQ_GPIO_Port, OW_DQ_Pin)

static SemaphoreHandle_t OnewireMutex;
static uint8_t OnewireDelayReady;

/**
  * @brief  使能DWT计数器，用于单总线微秒级延时
  * @param  无
  * @retval 无
  */
static void Onewire_EnableDelayCounter(void)
{
    if (OnewireDelayReady == 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        OnewireDelayReady = 1U;
    }
}

/**
  * @brief  单总线微秒延时
  * @param  DelayUs: 延时时间，单位微秒
  * @retval 无
  */
static void Onewire_DelayUs(uint32_t DelayUs)
{
    uint32_t start_cycle;
    uint32_t delay_cycle;

    Onewire_EnableDelayCounter();

    start_cycle = DWT->CYCCNT;
    delay_cycle = DelayUs * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start_cycle) < delay_cycle)
    {
    }
}

/**
  * @brief  进入单总线临界区，避免时序被任务切换打断
  * @param  无
  * @retval 无
  */
static void Onewire_EnterCritical(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        taskENTER_CRITICAL();
    }
}

/**
  * @brief  退出单总线临界区
  * @param  无
  * @retval 无
  */
static void Onewire_ExitCritical(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        taskEXIT_CRITICAL();
    }
}

/**
  * @brief  初始化单总线驱动
  * @param  无
  * @retval ONEWIRE_OK: 初始化成功
  *         ONEWIRE_ERROR: 初始化失败
  */
Onewire_Status_t Onewire_Init(void)
{
    if (OnewireMutex == NULL)
    {
        OnewireMutex = xSemaphoreCreateMutex();
        if (OnewireMutex == NULL)
        {
            return ONEWIRE_ERROR;
        }
    }

    ONEWIRE_DQ_HIGH();
    Onewire_EnableDelayCounter();

    return ONEWIRE_OK;
}

/**
  * @brief  获取单总线互斥锁，避免多任务同时访问总线
  * @param  无
  * @retval 无
  */
void Onewire_Lock(void)
{
    if (OnewireMutex != NULL)
    {
        (void)xSemaphoreTake(OnewireMutex, portMAX_DELAY);
    }
}

/**
  * @brief  释放单总线互斥锁
  * @param  无
  * @retval 无
  */
void Onewire_Unlock(void)
{
    if (OnewireMutex != NULL)
    {
        (void)xSemaphoreGive(OnewireMutex);
    }
}

/**
  * @brief  发送复位脉冲并检测从机存在脉冲
  * @param  无
  * @retval 1: 检测到设备
  *         0: 未检测到设备
  */
uint8_t Onewire_Reset(void)
{
    uint8_t presence;

    Onewire_EnterCritical();
    ONEWIRE_DQ_HIGH();
    Onewire_DelayUs(5U);
    ONEWIRE_DQ_LOW();
    Onewire_DelayUs(500U);
    ONEWIRE_DQ_HIGH();
    Onewire_DelayUs(70U);
    presence = (uint8_t)(ONEWIRE_DQ_READ() == GPIO_PIN_RESET);
    Onewire_DelayUs(430U);
    Onewire_ExitCritical();

    return presence;
}

/**
  * @brief  向单总线写入1位数据
  * @param  BitValue: 要写入的位值
  * @retval 无
  */
void Onewire_WriteBit(uint8_t BitValue)
{
    Onewire_EnterCritical();
    ONEWIRE_DQ_LOW();
    Onewire_DelayUs(2U);

    if (BitValue != 0U)
    {
        ONEWIRE_DQ_HIGH();
    }

    Onewire_DelayUs(60U);
    ONEWIRE_DQ_HIGH();
    Onewire_DelayUs(5U);
    Onewire_ExitCritical();
}

/**
  * @brief  从单总线读取1位数据
  * @param  无
  * @retval 读取到的位值
  */
uint8_t Onewire_ReadBit(void)
{
    uint8_t bit_value;

    Onewire_EnterCritical();
    ONEWIRE_DQ_LOW();
    Onewire_DelayUs(2U);
    ONEWIRE_DQ_HIGH();
    Onewire_DelayUs(10U);
    bit_value = (uint8_t)(ONEWIRE_DQ_READ() == GPIO_PIN_SET);
    Onewire_DelayUs(55U);
    Onewire_ExitCritical();

    return bit_value;
}

/**
  * @brief  向单总线按低位在前写入1字节数据
  * @param  Data: 要写入的数据
  * @retval 无
  */
void Onewire_WriteByte(uint8_t Data)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++)
    {
        Onewire_WriteBit(Data & 0x01U);
        Data >>= 1U;
    }
}

/**
  * @brief  从单总线按低位在前读取1字节数据
  * @param  无
  * @retval 读取到的数据
  */
uint8_t Onewire_ReadByte(void)
{
    uint8_t i;
    uint8_t data = 0U;

    for (i = 0U; i < 8U; i++)
    {
        data >>= 1U;
        if (Onewire_ReadBit() != 0U)
        {
            data |= 0x80U;
        }
    }

    return data;
}

/**
  * @brief  发送Skip ROM命令，适用于总线上仅有一个设备
  * @param  无
  * @retval 无
  */
void Onewire_SkipRom(void)
{
    Onewire_WriteByte(0xCCU);
}

/**
  * @brief  发送Match ROM命令并匹配指定设备
  * @param  RomCode: 8字节ROM码
  * @note   当总线上存在多个DS18B20时，后续命令只会作用于该ROM对应的器件
  * @retval 无
  */
void Onewire_MatchRom(const uint8_t RomCode[8])
{
    uint8_t i;

    if (RomCode == NULL)
    {
        return;
    }

    Onewire_WriteByte(0x55U);
    for (i = 0U; i < 8U; i++)
    {
        Onewire_WriteByte(RomCode[i]);
    }
}

/**
  * @brief  搜索单总线上的全部设备ROM地址
  * @param  RomCodes: ROM地址输出缓冲区
  * @param  MaxDevices: 最多搜索的设备数量
  * @param  FoundDevices: 实际搜索到的设备数量输出
  * @note   使用Dallas Search ROM算法遍历总线分叉，适用于多个DS18B20共线场景
  * @retval ONEWIRE_OK: 搜索成功
  *         ONEWIRE_ERROR: 搜索失败
  */
Onewire_Status_t Onewire_SearchRomCodes(uint8_t (*RomCodes)[ONEWIRE_ROM_CODE_SIZE], uint8_t MaxDevices, uint8_t *FoundDevices)
{
    uint8_t device_count = 0U;
    uint8_t last_discrepancy = 0U;
    uint8_t last_device_flag = 0U;
    uint8_t rom_code[ONEWIRE_ROM_CODE_SIZE] = {0U};

    if ((RomCodes == NULL) || (FoundDevices == NULL) || (MaxDevices == 0U))
    {
        return ONEWIRE_ERROR;
    }

    *FoundDevices = 0U;
    Onewire_Lock();

    /* 按 Dallas Search ROM 算法遍历总线上的全部设备地址 */
    while ((last_device_flag == 0U) && (device_count < MaxDevices))
    {
        uint8_t id_bit_number = 1U;
        uint8_t rom_byte_number = 0U;
        uint8_t rom_byte_mask = 1U;
        uint8_t discrepancy_marker = 0U;

        if (Onewire_Reset() == 0U)
        {
            Onewire_Unlock();
            return (device_count > 0U) ? ONEWIRE_OK : ONEWIRE_ERROR;
        }

        Onewire_WriteByte(0xF0U);

        while (rom_byte_number < ONEWIRE_ROM_CODE_SIZE)
        {
            uint8_t id_bit = Onewire_ReadBit();
            uint8_t cmp_id_bit = Onewire_ReadBit();
            uint8_t search_direction;

            if ((id_bit == 1U) && (cmp_id_bit == 1U))
            {
                Onewire_Unlock();
                return (device_count > 0U) ? ONEWIRE_OK : ONEWIRE_ERROR;
            }

            if (id_bit != cmp_id_bit)
            {
                /* 两次读值互补时，说明该位无分叉，直接沿现有方向走 */
                search_direction = id_bit;
            }
            else
            {
                /* 两位都为 0 表示总线上该位存在分叉，需要记录回溯点 */
                if (id_bit_number < last_discrepancy)
                {
                    search_direction = (uint8_t)((rom_code[rom_byte_number] & rom_byte_mask) != 0U);
                }
                else
                {
                    search_direction = (uint8_t)(id_bit_number == last_discrepancy);
                }

                if (search_direction == 0U)
                {
                    discrepancy_marker = id_bit_number;
                }
            }

            if (search_direction != 0U)
            {
                rom_code[rom_byte_number] |= rom_byte_mask;
            }
            else
            {
                rom_code[rom_byte_number] &= (uint8_t)(~rom_byte_mask);
            }

            Onewire_WriteBit(search_direction);
            id_bit_number++;
            rom_byte_mask <<= 1U;

            if (rom_byte_mask == 0U)
            {
                rom_byte_number++;
                rom_byte_mask = 1U;
            }
        }

        last_discrepancy = discrepancy_marker;
        if (last_discrepancy == 0U)
        {
            last_device_flag = 1U;
        }

        /* ROM 最后 1 字节是 CRC，校验不过则丢弃该地址 */
        if (Onewire_Crc8(rom_code, ONEWIRE_ROM_CODE_SIZE - 1U) != rom_code[ONEWIRE_ROM_CODE_SIZE - 1U])
        {
            continue;
        }

        for (uint8_t i = 0U; i < ONEWIRE_ROM_CODE_SIZE; i++)
        {
            RomCodes[device_count][i] = rom_code[i];
        }
        device_count++;
    }

    Onewire_Unlock();
    *FoundDevices = device_count;
    return (device_count > 0U) ? ONEWIRE_OK : ONEWIRE_ERROR;
}

/**
  * @brief  计算Dallas/Maxim协议使用的CRC8
  * @param  Data: 数据缓冲区
  * @param  Length: 数据长度
  * @retval 计算得到的CRC8值
  */
uint8_t Onewire_Crc8(const uint8_t *Data, uint8_t Length)
{
    uint8_t i;
    uint8_t j;
    uint8_t crc = 0U;

    if (Data == NULL)
    {
        return 0U;
    }

    for (i = 0U; i < Length; i++)
    {
        crc ^= Data[i];
        for (j = 0U; j < 8U; j++)
        {
            if ((crc & 0x01U) != 0U)
            {
                crc = (uint8_t)((crc >> 1U) ^ 0x8CU);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}
