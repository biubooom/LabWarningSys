#include "DHT22.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define DHT22_BIT_HIGH_THRESHOLD_US 50U

static SemaphoreHandle_t DHT22_Mutex;
static uint8_t DHT22_DelayReady;
/* 四组 DHT22 各自占用独立引脚，但共用同一套时序驱动 */
static GPIO_TypeDef *const s_dht22_ports[DHT22_GROUP_COUNT] = {
    G1_DHT22_DQ_GPIO_Port,
    G2_DHT22_DQ_GPIO_Port,
    G3_DHT22_DQ_GPIO_Port,
    G4_DHT22_DQ_GPIO_Port,
};
static const uint16_t s_dht22_pins[DHT22_GROUP_COUNT] = {
    G1_DHT22_DQ_Pin,
    G2_DHT22_DQ_Pin,
    G3_DHT22_DQ_Pin,
    G4_DHT22_DQ_Pin,
};

/**
  * @brief  将指定组的DHT22引脚切换为开漏输出模式
  * @param  group_index: 传感器组索引，范围0~3
  * @note   主机发送起始信号时需要主动驱动数据线高低电平
  * @retval 无
  */
static void DHT22_SetPinOutput(uint8_t group_index)
{
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Pin = s_dht22_pins[group_index];
    gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(s_dht22_ports[group_index], &gpio_init);
}

/**
  * @brief  将指定组的DHT22引脚切换为输入模式
  * @param  group_index: 传感器组索引，范围0~3
  * @note   发送完起始信号后必须释放总线，让DHT22回拉应答并输出40位数据
  * @retval 无
  */
static void DHT22_SetPinInput(uint8_t group_index)
{
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Pin = s_dht22_pins[group_index];
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(s_dht22_ports[group_index], &gpio_init);
}

/**
  * @brief  将指定组的DHT22数据线拉高
  * @param  group_index: 传感器组索引，范围0~3
  * @note   DHT22总线空闲时应保持高电平
  * @retval 无
  */
static void DHT22_DQ_High(uint8_t group_index)
{
    HAL_GPIO_WritePin(s_dht22_ports[group_index], s_dht22_pins[group_index], GPIO_PIN_SET);
}

/**
  * @brief  将指定组的DHT22数据线拉低
  * @param  group_index: 传感器组索引，范围0~3
  * @note   起始信号和位时序均通过拉低总线实现
  * @retval 无
  */
static void DHT22_DQ_Low(uint8_t group_index)
{
    HAL_GPIO_WritePin(s_dht22_ports[group_index], s_dht22_pins[group_index], GPIO_PIN_RESET);
}

/**
  * @brief  读取指定组的DHT22数据线电平
  * @param  group_index: 传感器组索引，范围0~3
  * @note   用于应答检测和逐位采样
  * @retval 当前GPIO电平状态
  */
static GPIO_PinState DHT22_DQ_Read(uint8_t group_index)
{
    return HAL_GPIO_ReadPin(s_dht22_ports[group_index], s_dht22_pins[group_index]);
}

/**
  * @brief  使能DWT计数器，用于DHT22微秒级延时
  * @param  无
  * @retval 无
  */
static void DHT22_EnableDelayCounter(void)
{
    if (DHT22_DelayReady == 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        DHT22_DelayReady = 1U;
    }
}

/**
  * @brief  DHT22微秒延时
  * @param  DelayUs: 延时时间，单位微秒
  * @retval 无
  */
static void DHT22_DelayUs(uint32_t DelayUs)
{
    uint32_t start_cycle;
    uint32_t delay_cycle;

    DHT22_EnableDelayCounter();

    start_cycle = DWT->CYCCNT;
    delay_cycle = DelayUs * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start_cycle) < delay_cycle)
    {
    }
}

/**
  * @brief  进入DHT22临界区，避免采样时序被打断
  * @param  无
  * @retval 无
  */
static void DHT22_EnterCritical(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        taskENTER_CRITICAL();
    }
}

/**
  * @brief  退出DHT22临界区
  * @param  无
  * @retval 无
  */
static void DHT22_ExitCritical(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        taskEXIT_CRITICAL();
    }
}

/**
  * @brief  等待引脚变为指定电平
  * @param  PinState: 目标电平
  * @param  TimeoutUs: 超时时间，单位微秒
  * @retval 1: 等待成功
  *         0: 等待超时
  */
static uint8_t DHT22_WaitForLevel(uint8_t group_index, GPIO_PinState PinState, uint32_t TimeoutUs)
{
    uint32_t start_cycle;
    uint32_t timeout_cycle;

    DHT22_EnableDelayCounter();
    start_cycle = DWT->CYCCNT;
    timeout_cycle = TimeoutUs * (SystemCoreClock / 1000000U);

    while (DHT22_DQ_Read(group_index) != PinState)
    {
        if ((DWT->CYCCNT - start_cycle) >= timeout_cycle)
        {
            return 0U;
        }
    }

    return 1U;
}

/**
  * @brief  读取DHT22的1位数据
  * @param  group_index: 传感器组索引，范围0~3
  * @note   通过测量高电平持续时间判断当前位是0还是1，比固定时刻采样更抗边沿抖动
  * @retval 读取到的位值
  */
static uint8_t DHT22_ReadBit(uint8_t group_index)
{
    uint32_t start_cycle;
    uint32_t high_cycles;
    uint32_t high_time_us;

    if (DHT22_WaitForLevel(group_index, GPIO_PIN_RESET, 70U) == 0U)
    {
        return 0U;
    }

    if (DHT22_WaitForLevel(group_index, GPIO_PIN_SET, 100U) == 0U)
    {
        return 0U;
    }

    DHT22_EnableDelayCounter();
    start_cycle = DWT->CYCCNT;

    if (DHT22_WaitForLevel(group_index, GPIO_PIN_RESET, 100U) == 0U)
    {
        return 1U;
    }

    high_cycles = DWT->CYCCNT - start_cycle;
    high_time_us = high_cycles / (SystemCoreClock / 1000000U);

    return (uint8_t)(high_time_us > DHT22_BIT_HIGH_THRESHOLD_US);
}

/**
  * @brief  初始化DHT22驱动
  * @param  无
  * @note   兼容旧单组接口，默认初始化第0组
  * @retval DHT22_OK: 初始化成功
  *         DHT22_ERROR: 初始化失败
  */
DHT22_Status_t DHT22_Init(void)
{
    return DHT22_InitGroup(0U);
}

/**
  * @brief  初始化指定组的DHT22驱动
  * @param  GroupIndex: 传感器组索引，范围0~3
  * @note   仅初始化共享互斥资源和对应数据线空闲状态，不立即读取数据
  * @retval DHT22_OK: 初始化成功
  *         DHT22_ERROR: 初始化失败
  */
DHT22_Status_t DHT22_InitGroup(uint8_t GroupIndex)
{
    if (GroupIndex >= DHT22_GROUP_COUNT)
    {
        return DHT22_ERROR;
    }

    if (DHT22_Mutex == NULL)
    {
        DHT22_Mutex = xSemaphoreCreateMutex();
        if (DHT22_Mutex == NULL)
        {
            return DHT22_ERROR;
        }
    }

    /* DHT22 空闲时恢复为开漏输出高电平，确保下次能主动发起起始信号 */
    DHT22_SetPinOutput(GroupIndex);
    DHT22_DQ_High(GroupIndex);
    DHT22_EnableDelayCounter();

    return DHT22_OK;
}

/**
  * @brief  读取DHT22温湿度数据
  * @param  Temperature: 温度输出，单位摄氏度
  * @param  Humidity: 湿度输出，单位%RH
  * @note   兼容旧单组接口，默认读取第0组
  * @retval DHT22_OK: 读取成功
  *         DHT22_ERROR: 读取失败
  */
DHT22_Status_t DHT22_Read(float *Temperature, float *Humidity)
{
    return DHT22_ReadGroup(0U, Temperature, Humidity);
}

/**
  * @brief  读取指定组的DHT22温湿度数据
  * @param  GroupIndex: 传感器组索引，范围0~3
  * @param  Temperature: 温度输出，单位摄氏度
  * @param  Humidity: 湿度输出，单位%RH
  * @note   读取过程依赖微秒级时序与临界区保护，多任务环境下通过互斥锁串行访问
  * @retval DHT22_OK: 读取成功
  *         DHT22_ERROR: 读取失败
  */
DHT22_Status_t DHT22_ReadGroup(uint8_t GroupIndex, float *Temperature, float *Humidity)
{
    uint8_t i;
    uint8_t j;
    uint8_t data[5] = {0U};
    uint16_t raw_humidity;
    uint16_t raw_temperature;

    if ((GroupIndex >= DHT22_GROUP_COUNT) || (Temperature == NULL) || (Humidity == NULL))
    {
        return DHT22_ERROR;
    }

    if (DHT22_Mutex == NULL)
    {
        return DHT22_ERROR;
    }

    (void)xSemaphoreTake(DHT22_Mutex, portMAX_DELAY);
    DHT22_EnterCritical();

    /* 主机起始信号：先拉高稳定，再拉低至少 1ms，请求传感器应答 */
    DHT22_SetPinOutput(GroupIndex);
    DHT22_DQ_High(GroupIndex);
    DHT22_DelayUs(1000U);
    DHT22_DQ_Low(GroupIndex);
    DHT22_DelayUs(1200U);
    DHT22_DQ_High(GroupIndex);
    DHT22_DelayUs(30U);
    /* 起始信号结束后必须切成输入，让DHT22把总线拉低作为应答 */
    DHT22_SetPinInput(GroupIndex);

    if (DHT22_WaitForLevel(GroupIndex, GPIO_PIN_RESET, 100U) == 0U)
    {
        DHT22_ExitCritical();
        DHT22_SetPinOutput(GroupIndex);
        DHT22_DQ_High(GroupIndex);
        (void)xSemaphoreGive(DHT22_Mutex);
        return DHT22_ERROR;
    }

    if (DHT22_WaitForLevel(GroupIndex, GPIO_PIN_SET, 100U) == 0U)
    {
        DHT22_ExitCritical();
        DHT22_SetPinOutput(GroupIndex);
        DHT22_DQ_High(GroupIndex);
        (void)xSemaphoreGive(DHT22_Mutex);
        return DHT22_ERROR;
    }

    if (DHT22_WaitForLevel(GroupIndex, GPIO_PIN_RESET, 100U) == 0U)
    {
        DHT22_ExitCritical();
        DHT22_SetPinOutput(GroupIndex);
        DHT22_DQ_High(GroupIndex);
        (void)xSemaphoreGive(DHT22_Mutex);
        return DHT22_ERROR;
    }

    for (i = 0U; i < 5U; i++)
    {
        for (j = 0U; j < 8U; j++)
        {
            data[i] <<= 1U;
            data[i] |= DHT22_ReadBit(GroupIndex);
        }
    }

    DHT22_ExitCritical();
    DHT22_SetPinOutput(GroupIndex);
    DHT22_DQ_High(GroupIndex);
    (void)xSemaphoreGive(DHT22_Mutex);

    /* DHT22 最后 1 字节为前 4 字节累加和校验 */
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
    {
        return DHT22_ERROR;
    }

    raw_humidity = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
    raw_temperature = (uint16_t)(((uint16_t)data[2] << 8U) | data[3]);

    *Humidity = (float)raw_humidity / 10.0f;

    if ((raw_temperature & 0x8000U) != 0U)
    {
        raw_temperature &= 0x7FFFU;
        *Temperature = -((float)raw_temperature / 10.0f);
    }
    else
    {
        *Temperature = (float)raw_temperature / 10.0f;
    }

    return DHT22_OK;
}
