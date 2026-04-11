#include "start_task.h"

#include "adc.h"
#include "DHT22.h"
#include "DS18B20.h"
#include "OLED.h"
#include "usart.h"

#include <stdio.h>

TaskHandle_t StartTaskHandle;

static TaskHandle_t DS18B20TaskHandle;
static TaskHandle_t DHT22TaskHandle;
static TaskHandle_t OLEDTaskHandle;
static TaskHandle_t UartTxTaskHandle;

/*DS18B20*/
#define DS18B20_TASK_NAME          "DS18B20_Task"
#define DS18B20_TASK_STACK_SIZE    384U
#define DS18B20_TASK_PRIORITY      (tskIDLE_PRIORITY + 2)
/*DHT22*/
#define DHT22_TASK_NAME            "DHT22_Task"
#define DHT22_TASK_STACK_SIZE      384U
#define DHT22_TASK_PRIORITY        (tskIDLE_PRIORITY + 2)
#define DHT22_POWER_ON_DELAY_MS    2000U
#define DHT22_RETRY_COUNT          3U
#define DHT22_RETRY_DELAY_MS       100U
/*OLED*/
#define OLED_TASK_NAME             "OLED_Task"
#define OLED_TASK_STACK_SIZE       384U
#define OLED_TASK_PRIORITY         (tskIDLE_PRIORITY + 1)
/*UART*/
#define UART_TX_TASK_NAME          "UART_TX_Task"
#define UART_TX_TASK_STACK_SIZE    768U
#define UART_TX_TASK_PRIORITY      (tskIDLE_PRIORITY + 1)
#define UART_TX_PERIOD_MS          2000U

#define ADC_CHANNEL_COUNT          2U
#define ADC_MQ2_INDEX              0U
#define ADC_LIGHT_INDEX            1U
#define ADC_FULL_SCALE             4095.0f
#define TEMPERATURE_ALARM_LIMIT    70.0f
#define SMOKE_ALARM_LIMIT          80.0f

static float g_temperature = 0.0f;
static float g_humidity = 0.0f;
static uint8_t g_ds18b20_online = 0U;
static uint8_t g_dht22_online = 0U;
static uint16_t g_adc_buffer[ADC_CHANNEL_COUNT];

static float ADC_ToPercent(uint16_t adc_value)
{
    return ((float)adc_value * 100.0f) / ADC_FULL_SCALE;
}

/**
  * @brief  UART发送任务，周期上报当前这一组传感器数据
  * @param  pvParameters: 任务参数，当前未使用
  * @retval 无
  */
static void UART_TX_Task(void *pvParameters)
{
    char tx_buffer[160];
    int length;
    float temperature;
    float humidity;
    float smoke_value;
    float light_value;
    uint8_t alarm;
    TickType_t last_wake_time;

    (void)pvParameters;

    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        temperature = g_temperature;
        humidity = g_humidity;
        smoke_value = ADC_ToPercent(g_adc_buffer[ADC_MQ2_INDEX]);
        light_value = ADC_ToPercent(g_adc_buffer[ADC_LIGHT_INDEX]);
        alarm = ((g_ds18b20_online != 0U) && (temperature >= TEMPERATURE_ALARM_LIMIT)) ||
                (smoke_value >= SMOKE_ALARM_LIMIT);

        length = snprintf(tx_buffer,
                          sizeof(tx_buffer),
                          "{\"alarm\":%u,\"humidity\":%.1f,\"light\":%.1f,\"smoke\":%.1f,\"temperature\":%.1f}\r\n",
                          (unsigned int)alarm,
                          humidity,
                          light_value,
                          smoke_value,
                          temperature);

        if (length > 0)
        {
            if ((size_t)length >= sizeof(tx_buffer))
            {
                length = (int)(sizeof(tx_buffer) - 1U);
            }

            if (HAL_UART_Transmit(&huart1, (uint8_t *)tx_buffer, (uint16_t)length, 100U) != HAL_OK)
            {
                Error_Handler();
            }
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(UART_TX_PERIOD_MS));
    }
}

/**
  * @brief  DS18B20采集任务
  * @param  pvParameters: 任务参数，当前未使用
  * @retval 无
  */
static void DS18B20_Task(void *pvParameters)
{
    float temperature;

    (void)pvParameters;

    if (DS18B20_Init() != DS18B20_OK)
    {
        g_ds18b20_online = 0U;
    }
    else
    {
        g_ds18b20_online = 1U;
    }

    while (1)
    {
        if (DS18B20_ReadTemperature(&temperature) == DS18B20_OK)
        {
            g_temperature = temperature;
            g_ds18b20_online = 1U;
        }
        else
        {
            g_ds18b20_online = 0U;
        }

        vTaskDelay(pdMS_TO_TICKS(2000U));
    }
}

/**
  * @brief  DHT22采集任务
  * @param  pvParameters: 任务参数，当前未使用
  * @retval 无
  */
static void DHT22_Task(void *pvParameters)
{
    float temperature;
    float humidity;
    uint8_t retry;
    uint8_t read_ok;

    (void)pvParameters;

    if (DHT22_Init() != DHT22_OK)
    {
        g_dht22_online = 0U;
    }
    else
    {
        g_dht22_online = 1U;
    }

    /* DHT22上电后需要稳定一段时间，首轮读取过早容易失败。 */
    vTaskDelay(pdMS_TO_TICKS(DHT22_POWER_ON_DELAY_MS));

    while (1)
    {
        read_ok = 0U;

        for (retry = 0U; retry < DHT22_RETRY_COUNT; retry++)
        {
            if (DHT22_Read(&temperature, &humidity) == DHT22_OK)
            {
                g_humidity = humidity;
                g_dht22_online = 1U;
                read_ok = 1U;
                break;
            }

            if ((retry + 1U) < DHT22_RETRY_COUNT)
            {
                vTaskDelay(pdMS_TO_TICKS(DHT22_RETRY_DELAY_MS));
            }
        }

        if (read_ok == 0U)
        {
            g_dht22_online = 0U;
        }

        vTaskDelay(pdMS_TO_TICKS(2000U));
    }
}

/**
  * @brief  OLED显示任务
  * @param  pvParameters: 任务参数，当前未使用
  * @retval 无
  */
static void OLED_Task(void *pvParameters)
{
    int32_t temperature_x10;
    int32_t humidity_x10;
    int32_t smoke_x10;
    int32_t light_x10;
    uint32_t fraction;
    float smoke_percent;
    float light_percent;
    uint8_t alarm;

    (void)pvParameters;

    if (OLED_Init() != OLED_OK)
    {
        Error_Handler();
    }

    OLED_ShowString(1, 1, "Temp:");
    OLED_ShowString(2, 1, "Humi:");
    OLED_ShowString(3, 1, "Smoke:");
    OLED_ShowString(4, 1, "Light:");

    while (1)
    {
        smoke_percent = ADC_ToPercent(g_adc_buffer[ADC_MQ2_INDEX]);
        light_percent = ADC_ToPercent(g_adc_buffer[ADC_LIGHT_INDEX]);
        alarm = ((g_ds18b20_online != 0U) && (g_temperature >= TEMPERATURE_ALARM_LIMIT)) ||
                (smoke_percent >= SMOKE_ALARM_LIMIT);

        if (g_ds18b20_online != 0U)
        {
            temperature_x10 = (int32_t)(g_temperature * 10.0f);
            fraction = (uint32_t)((temperature_x10 < 0) ? -(temperature_x10 % 10) : (temperature_x10 % 10));
            OLED_ShowSignedNum(1, 6, temperature_x10 / 10, 3);
            if ((temperature_x10 > -10) && (temperature_x10 < 10))
            {
                OLED_ShowChar(1, 8, '0');
            }
            OLED_ShowChar(1, 10, '.');
            OLED_ShowNum(1, 11, fraction, 1);
            OLED_ShowString(1, 12, "C  ");
        }
        else
        {
            OLED_ShowString(1, 6, "Error   ");
        }

        if (g_dht22_online != 0U)
        {
            humidity_x10 = (int32_t)(g_humidity * 10.0f);
            fraction = (uint32_t)(humidity_x10 % 10);
            OLED_ShowNum(2, 6, (uint32_t)(humidity_x10 / 10), 3);
            OLED_ShowChar(2, 9, '.');
            OLED_ShowNum(2, 10, fraction, 1);
            OLED_ShowChar(2, 11, '%');
            OLED_ShowString(2, 12, "  ");
        }
        else
        {
            OLED_ShowString(2, 6, "Error   ");
        }

        smoke_x10 = (int32_t)(smoke_percent * 10.0f);
        fraction = (uint32_t)(smoke_x10 % 10);
        OLED_ShowNum(3, 7, (uint32_t)(smoke_x10 / 10), 3);
        OLED_ShowChar(3, 10, '.');
        OLED_ShowNum(3, 11, fraction, 1);
        OLED_ShowString(3, 12, "%");

        light_x10 = (int32_t)(light_percent * 10.0f);
        fraction = (uint32_t)(light_x10 % 10);
        OLED_ShowNum(4, 7, (uint32_t)(light_x10 / 10), 3);
        OLED_ShowChar(4, 10, '.');
        OLED_ShowNum(4, 11, fraction, 1);
        OLED_ShowString(4, 12, "%");

        OLED_ShowChar(1, 16, (alarm != 0U) ? 'A' : 'N');
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/**
  * @brief  启动任务，负责创建系统中的其他应用任务
  * @param  pvParameters: 任务参数，当前未使用
  * @retval 无
  */
void StartTask(void *pvParameters)
{
    BaseType_t task_status;

    (void)pvParameters;

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_buffer, ADC_CHANNEL_COUNT) != HAL_OK)
    {
        Error_Handler();
    }

    task_status = xTaskCreate(DS18B20_Task, DS18B20_TASK_NAME, DS18B20_TASK_STACK_SIZE, NULL, DS18B20_TASK_PRIORITY, &DS18B20TaskHandle);
    if (task_status != pdPASS)
    {
        Error_Handler();
    }

    task_status = xTaskCreate(DHT22_Task, DHT22_TASK_NAME, DHT22_TASK_STACK_SIZE, NULL, DHT22_TASK_PRIORITY, &DHT22TaskHandle);
    if (task_status != pdPASS)
    {
        Error_Handler();
    }

    task_status = xTaskCreate(OLED_Task, OLED_TASK_NAME, OLED_TASK_STACK_SIZE, NULL, OLED_TASK_PRIORITY, &OLEDTaskHandle);
    if (task_status != pdPASS)
    {
        Error_Handler();
    }

    task_status = xTaskCreate(UART_TX_Task, UART_TX_TASK_NAME, UART_TX_TASK_STACK_SIZE, NULL, UART_TX_TASK_PRIORITY, &UartTxTaskHandle);
    if (task_status != pdPASS)
    {
        Error_Handler();
    }

    vTaskDelete(NULL);
}
