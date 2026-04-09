#include "DHT22.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define DHT22_DQ_HIGH() HAL_GPIO_WritePin(DHT22__OW_DQ_GPIO_Port, DHT22__OW_DQ_Pin, GPIO_PIN_SET)
#define DHT22_DQ_LOW()  HAL_GPIO_WritePin(DHT22__OW_DQ_GPIO_Port, DHT22__OW_DQ_Pin, GPIO_PIN_RESET)
#define DHT22_DQ_READ() HAL_GPIO_ReadPin(DHT22__OW_DQ_GPIO_Port, DHT22__OW_DQ_Pin)

static SemaphoreHandle_t DHT22_Mutex;
static uint8_t DHT22_DelayReady;

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
static uint8_t DHT22_WaitForLevel(GPIO_PinState PinState, uint32_t TimeoutUs)
{
    uint32_t start_cycle;
    uint32_t timeout_cycle;

    DHT22_EnableDelayCounter();
    start_cycle = DWT->CYCCNT;
    timeout_cycle = TimeoutUs * (SystemCoreClock / 1000000U);

    while (DHT22_DQ_READ() != PinState)
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
  * @param  无
  * @retval 读取到的位值
  */
static uint8_t DHT22_ReadBit(void)
{
    uint8_t bit_value;

    if (DHT22_WaitForLevel(GPIO_PIN_RESET, 70U) == 0U)
    {
        return 0U;
    }

    if (DHT22_WaitForLevel(GPIO_PIN_SET, 100U) == 0U)
    {
        return 0U;
    }

    DHT22_DelayUs(40U);
    bit_value = (uint8_t)(DHT22_DQ_READ() == GPIO_PIN_SET);

    if (DHT22_WaitForLevel(GPIO_PIN_RESET, 100U) == 0U)
    {
        return bit_value;
    }

    return bit_value;
}

/**
  * @brief  初始化DHT22驱动
  * @param  无
  * @retval DHT22_OK: 初始化成功
  *         DHT22_ERROR: 初始化失败
  */
DHT22_Status_t DHT22_Init(void)
{
    if (DHT22_Mutex == NULL)
    {
        DHT22_Mutex = xSemaphoreCreateMutex();
        if (DHT22_Mutex == NULL)
        {
            return DHT22_ERROR;
        }
    }

    DHT22_DQ_HIGH();
    DHT22_EnableDelayCounter();

    return DHT22_OK;
}

/**
  * @brief  读取DHT22温湿度数据
  * @param  Temperature: 温度输出，单位摄氏度
  * @param  Humidity: 湿度输出，单位%RH
  * @retval DHT22_OK: 读取成功
  *         DHT22_ERROR: 读取失败
  */
DHT22_Status_t DHT22_Read(float *Temperature, float *Humidity)
{
    uint8_t i;
    uint8_t j;
    uint8_t data[5] = {0U};
    uint16_t raw_humidity;
    uint16_t raw_temperature;

    if ((Temperature == NULL) || (Humidity == NULL))
    {
        return DHT22_ERROR;
    }

    if (DHT22_Mutex == NULL)
    {
        return DHT22_ERROR;
    }

    (void)xSemaphoreTake(DHT22_Mutex, portMAX_DELAY);
    DHT22_EnterCritical();

    DHT22_DQ_HIGH();
    DHT22_DelayUs(1000U);
    DHT22_DQ_LOW();
    DHT22_DelayUs(1200U);
    DHT22_DQ_HIGH();
    DHT22_DelayUs(30U);

    if (DHT22_WaitForLevel(GPIO_PIN_RESET, 100U) == 0U)
    {
        DHT22_ExitCritical();
        (void)xSemaphoreGive(DHT22_Mutex);
        return DHT22_ERROR;
    }

    if (DHT22_WaitForLevel(GPIO_PIN_SET, 100U) == 0U)
    {
        DHT22_ExitCritical();
        (void)xSemaphoreGive(DHT22_Mutex);
        return DHT22_ERROR;
    }

    if (DHT22_WaitForLevel(GPIO_PIN_RESET, 100U) == 0U)
    {
        DHT22_ExitCritical();
        (void)xSemaphoreGive(DHT22_Mutex);
        return DHT22_ERROR;
    }

    for (i = 0U; i < 5U; i++)
    {
        for (j = 0U; j < 8U; j++)
        {
            data[i] <<= 1U;
            data[i] |= DHT22_ReadBit();
        }
    }

    DHT22_ExitCritical();
    (void)xSemaphoreGive(DHT22_Mutex);

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
