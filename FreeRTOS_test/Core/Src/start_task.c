#include "start_task.h"

#include "adc.h"
#include "DHT22.h"
#include "DS18B20.h"
#include "OLED.h"

TaskHandle_t StartTaskHandle;

static TaskHandle_t DS18B20TaskHandle;
static TaskHandle_t DHT22TaskHandle;
static TaskHandle_t OLEDTaskHandle;

/*DS18B20*/
#define DS18B20_TASK_NAME          "DS18B20_Task"
#define DS18B20_TASK_STACK_SIZE    256U
#define DS18B20_TASK_PRIORITY      (tskIDLE_PRIORITY + 2)
/*DHT22*/
#define DHT22_TASK_NAME            "DHT22_Task"
#define DHT22_TASK_STACK_SIZE      256U
#define DHT22_TASK_PRIORITY        (tskIDLE_PRIORITY + 2)
/*OLED*/
#define OLED_TASK_NAME             "OLED_Task"
#define OLED_TASK_STACK_SIZE       256U
#define OLED_TASK_PRIORITY         (tskIDLE_PRIORITY + 1)

#define ADC_CHANNEL_COUNT          2U
#define ADC_MQ2_INDEX              0U
#define ADC_LIGHT_INDEX            1U

static float g_temperature = 0.0f;
static float g_humidity = 0.0f;
static uint8_t g_ds18b20_online = 0U;
static uint8_t g_dht22_online = 0U;
static uint16_t g_adc_buffer[ADC_CHANNEL_COUNT];

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

        vTaskDelay(pdMS_TO_TICKS(250U));
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

    (void)pvParameters;

    if (DHT22_Init() != DHT22_OK)
    {
        g_dht22_online = 0U;
    }
    else
    {
        g_dht22_online = 1U;
    }

    while (1)
    {
        if (DHT22_Read(&temperature, &humidity) == DHT22_OK)
        {
            g_humidity = humidity;
            g_dht22_online = 1U;
        }
        else
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
    uint32_t fraction;
    size_t free_heap;
    size_t min_free_heap;
    uint16_t mq2_value;
    uint16_t light_value;

    (void)pvParameters;

    if (OLED_Init() != OLED_OK)
    {
        Error_Handler();
    }

    OLED_ShowString(1, 1, "Temp:");
    OLED_ShowString(2, 1, "Humi:");
    OLED_ShowString(3, 1, "H");
    OLED_ShowString(4, 1, "Q");

    while (1)
    {
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

        free_heap = xPortGetFreeHeapSize();
        min_free_heap = xPortGetMinimumEverFreeHeapSize();
        mq2_value = g_adc_buffer[ADC_MQ2_INDEX];
        light_value = g_adc_buffer[ADC_LIGHT_INDEX];

        OLED_ShowNum(3, 2, (uint32_t)free_heap, 4);
        OLED_ShowString(3, 6, " M");
        OLED_ShowNum(3, 8, (uint32_t)min_free_heap, 4);

        OLED_ShowNum(4, 2, (uint32_t)mq2_value, 4);
        OLED_ShowString(4, 6, " L");
        OLED_ShowNum(4, 8, (uint32_t)light_value, 4);
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

    vTaskDelete(NULL);
}
