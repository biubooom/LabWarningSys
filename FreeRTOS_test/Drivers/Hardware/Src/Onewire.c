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
