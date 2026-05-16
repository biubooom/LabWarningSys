#include "start_task.h"

#include "adc.h"
#include "DHT22.h"
#include "DS18B20.h"
#include "OLED.h"
#include "main.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

TaskHandle_t StartTaskHandle;

typedef struct
{
    /* detected 表示 DET 当前确认已插入，initialized 表示该组已完成初始化 */
    uint8_t detected;
    uint8_t initialized;
    /* 两类数字传感器分别记录在线状态，避免一个失败拖累另一项显示 */
    uint8_t dht22_online;
    uint8_t ds18b20_online;
    /* DS18B20 通过单总线 ROM 码和插槽绑定 */
    uint8_t rom_valid;
    float temperature;
    float humidity;
    float smoke;
    float light;
    uint8_t rom_code[DS18B20_ROM_CODE_SIZE];
} SensorGroup_t;

static TaskHandle_t SensorDetectTaskHandle;
static TaskHandle_t SensorSampleTaskHandle;
static TaskHandle_t OLEDTaskHandle;
static TaskHandle_t UartTxTaskHandle;
static volatile uint8_t g_ds18b20_assignment_dirty = 1U;

/* 四组传感器固定槽位数量 */
#define SENSOR_GROUP_COUNT          4U

/* FreeRTOS任务名称、栈大小与优先级配置 */
#define SENSOR_DETECT_TASK_NAME     "SensorDetect"
#define SENSOR_DETECT_TASK_STACK    384U
#define SENSOR_DETECT_TASK_PRIO     (tskIDLE_PRIORITY + 3)
#define SENSOR_SAMPLE_TASK_NAME     "SensorSample"
#define SENSOR_SAMPLE_TASK_STACK    768U
#define SENSOR_SAMPLE_TASK_PRIO     (tskIDLE_PRIORITY + 2)
#define OLED_TASK_NAME              "OLED_Task"
#define OLED_TASK_STACK_SIZE        512U
#define OLED_TASK_PRIORITY          (tskIDLE_PRIORITY + 1)
#define UART_TX_TASK_NAME           "UART_TX_Task"
#define UART_TX_TASK_STACK_SIZE     1024U
#define UART_TX_TASK_PRIORITY       (tskIDLE_PRIORITY + 3)
/* 采样、显示和插拔检测相关时序参数 */
#define DET_DEBOUNCE_MS             50U      // DET插拔检测消抖时间，单位毫秒
#define SAMPLE_PERIOD_MS            2000U    // 传感器采样任务运行周期，单位毫秒
#define UART_TX_PERIOD_MS           2000U    // UART原始数据帧发送周期，单位毫秒
#define OLED_ROTATE_PERIOD_MS       2000U    // OLED详细信息轮播切换周期，单位毫秒 
#define DHT22_POWER_ON_DELAY_MS     2000U    // DHT22上电后首次读取前的预热等待时间，单位毫秒 
#define DHT22_RETRY_COUNT           3U       // DHT22单次采样失败后的最大重试次数 
#define DHT22_RETRY_DELAY_MS        100U     // DHT22两次重试之间的间隔时间，单位毫秒 
#define DS18B20_CONVERT_WAIT_MS     800U     // DS18B20并行转换等待时间，单位毫秒
#define DS18B20_RETRY_COUNT         0U       // DS18B20读取失败后先不补读，用于验证基础采样周期
#define DS18B20_RETRY_DELAY_MS      20U      // DS18B20补读前的短暂间隔，单位毫秒

/* ADC基础参数与单总线设备数量上限 */
#define ADC_CHANNEL_COUNT           8U
#define ADC_FULL_SCALE              4095.0f
#define DS18B20_MAX_DEVICES         SENSOR_GROUP_COUNT
/* DS18B20单总线搜索顺序与实际组号不一致时，使用该表修正ROM到组的绑定顺序 */
#define DS18B20_G1_ROM_INDEX        1U
#define DS18B20_G2_ROM_INDEX        3U
#define DS18B20_G3_ROM_INDEX        2U
#define DS18B20_G4_ROM_INDEX        0U

/* ADC DMA缓冲区中的通道顺序映射，前4路为MQ2，后4路为光照 */
#define ADC_G1_MQ2_INDEX            0U
#define ADC_G2_MQ2_INDEX            1U
#define ADC_G3_MQ2_INDEX            2U
#define ADC_G4_MQ2_INDEX            3U
#define ADC_G1_LIGHT_INDEX          4U
#define ADC_G2_LIGHT_INDEX          5U
#define ADC_G3_LIGHT_INDEX          6U
#define ADC_G4_LIGHT_INDEX          7U

/* 四组传感器的总状态表，系统里所有任务都从这里读写数据 */
static SensorGroup_t g_sensor_groups[SENSOR_GROUP_COUNT];
/* ADC的DMA接收缓冲区，里面放着8路模拟传感器的最新采样值 */
static uint16_t g_adc_buffer[ADC_CHANNEL_COUNT];

/* 每一组LED对应的GPIO端口，数组下标和group_index一一对应 */
static GPIO_TypeDef *const s_led_ports[SENSOR_GROUP_COUNT] = {
    G1_LED_GPIO_Port,
    G2_LED_GPIO_Port,
    G3_LED_GPIO_Port,
    G4_LED_GPIO_Port,
};
/* 每一组LED对应的GPIO引脚，数组下标和group_index一一对应 */
static const uint16_t s_led_pins[SENSOR_GROUP_COUNT] = {
    G1_LED_Pin,
    G2_LED_Pin,
    G3_LED_Pin,
    G4_LED_Pin,
};
/* 每一组DET检测引脚对应的GPIO端口，数组下标和group_index一一对应 */
static GPIO_TypeDef *const s_det_ports[SENSOR_GROUP_COUNT] = {
    G1_DET_GPIO_Port,
    G2_DET_GPIO_Port,
    G3_DET_GPIO_Port,
    G4_DET_GPIO_Port,
};
/* 每一组DET检测引脚对应的GPIO编号，数组下标和group_index一一对应 */
static const uint16_t s_det_pins[SENSOR_GROUP_COUNT] = {
    G1_DET_Pin,
    G2_DET_Pin,
    G3_DET_Pin,
    G4_DET_Pin,
};
/* 每一组MQ2烟雾传感器在ADC缓冲区里对应的数组下标 */
static const uint8_t s_smoke_adc_index[SENSOR_GROUP_COUNT] = {
    ADC_G1_MQ2_INDEX,
    ADC_G2_MQ2_INDEX,
    ADC_G3_MQ2_INDEX,
    ADC_G4_MQ2_INDEX,
};
/* 每一组光照传感器在ADC缓冲区里对应的数组下标 */
static const uint8_t s_light_adc_index[SENSOR_GROUP_COUNT] = {
    ADC_G1_LIGHT_INDEX,
    ADC_G2_LIGHT_INDEX,
    ADC_G3_LIGHT_INDEX,
    ADC_G4_LIGHT_INDEX,
};
/* 目标组号到DS18B20搜索结果下标的映射表，按当前实物接线校正温度顺序 */
static const uint8_t s_ds18b20_rom_index_map[SENSOR_GROUP_COUNT] = {
    DS18B20_G1_ROM_INDEX,
    DS18B20_G2_ROM_INDEX,
    DS18B20_G3_ROM_INDEX,
    DS18B20_G4_ROM_INDEX,
};

/**
  * @brief  将ADC原始值换算为百分比
  * @param  adc_value: ADC采样原始值
  * @note   当前按12位ADC满量程4095换算，结果用于OLED显示和串口上报
  * @retval 换算后的百分比值
  */
static float ADC_ToPercent(uint16_t adc_value)
{
    /* 模拟量统一换算为 0~100，便于 OLED 和串口直接展示 */
    return ((float)adc_value * 100.0f) / ADC_FULL_SCALE;
}

/**
  * @brief  将ADC原始值换算为“光照强度百分比”
  * @param  adc_value: ADC采样原始值
  * @note   当前光敏分压电路表现为“光越强，ADC值越小”，因此这里做反向映射
  * @retval 换算后的光照强度百分比，数值越大表示光越强
  */
static float ADC_ToLightPercent(uint16_t adc_value)
{
    return 100.0f - ADC_ToPercent(adc_value);
}

/**
  * @brief  读取指定组的DET插入状态
  * @param  group_index: 传感器组索引，范围0~3
  * @note   当前硬件中DET为低电平有效，返回非0表示模块已插入
  * @retval 1: 已插入
  *         0: 未插入
  */
static uint8_t GroupIsInserted(uint8_t group_index)
{
    return (uint8_t)(HAL_GPIO_ReadPin(s_det_ports[group_index], s_det_pins[group_index]) == GPIO_PIN_RESET);
}

/**
  * @brief  设置指定组的状态指示灯
  * @param  group_index: 传感器组索引，范围0~3
  * @param  on: 灯状态，非0为点亮，0为熄灭
  * @note   该LED仅表示该组已接入并完成初始化，不表示告警状态
  * @retval 无
  */
static void GroupSetLed(uint8_t group_index, uint8_t on)
{
    HAL_GPIO_WritePin(s_led_ports[group_index], s_led_pins[group_index], (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
  * @brief  清空指定组的运行态数据
  * @param  group_index: 传感器组索引，范围0~3
  * @note   仅清空采样值和在线状态，不直接改变插拔检测状态
  * @retval 无
  */
static void GroupClearData(uint8_t group_index)
{
    SensorGroup_t *group = &g_sensor_groups[group_index];

    /* 组下线时仅清空运行态数据，不改 detected，由上层决定插拔状态 */
    group->initialized = 0U;
    group->dht22_online = 0U;
    group->ds18b20_online = 0U;
    group->rom_valid = 0U;
    group->temperature = 0.0f;
    group->humidity = 0.0f;
    group->smoke = 0.0f;
    group->light = 0.0f;
    memset(group->rom_code, 0, sizeof(group->rom_code));
}

/**
  * @brief  比较两个DS18B20的ROM地址是否相同
  * @param  lhs: 第一个ROM地址指针
  * @param  rhs: 第二个ROM地址指针
  * @note   ROM地址长度固定为8字节，用于单总线多设备分组绑定
  * @retval 1: 相同
  *         0: 不同
  */
static uint8_t RomCodeEquals(const uint8_t *lhs, const uint8_t *rhs)
{
    return (uint8_t)(memcmp(lhs, rhs, DS18B20_ROM_CODE_SIZE) == 0);
}

/**
  * @brief  刷新四组DS18B20与ROM地址的绑定关系
  * @param  无
  * @note   四个DS18B20共用一根OW_DQ总线，每轮重新搜索并修正地址分配
  * @retval 无
  */
static void RefreshDs18b20Assignments(void)
{
    uint8_t rom_codes[DS18B20_MAX_DEVICES][DS18B20_ROM_CODE_SIZE] = {{0U}};
    uint8_t found = 0U;
    uint8_t rom_used[DS18B20_MAX_DEVICES] = {0U};

    /* 每轮先重新搜索总线设备，避免插拔后仍沿用失效地址 */
    if (DS18B20_SearchRomCodes(rom_codes, DS18B20_MAX_DEVICES, &found) != DS18B20_OK)
    {
        found = 0U;
    }

    for (uint8_t group_index = 0U; group_index < SENSOR_GROUP_COUNT; group_index++)
    {
        SensorGroup_t *group = &g_sensor_groups[group_index];
        uint8_t matched = 0U;

        if ((group->detected == 0U) || (group->rom_valid == 0U))
        {
            /* 未插入或尚未绑定 ROM 的组，先标为无温度探头 */
            group->rom_valid = 0U;
            group->ds18b20_online = 0U;
            memset(group->rom_code, 0, sizeof(group->rom_code));
            continue;
        }

        for (uint8_t rom_index = 0U; rom_index < found; rom_index++)
        {
            if (RomCodeEquals(group->rom_code, rom_codes[rom_index]) != 0U)
            {
                rom_used[rom_index] = 1U;
                matched = 1U;
                break;
            }
        }

        if (matched == 0U)
        {
            /* 之前绑定的 ROM 本轮未搜到，说明探头可能已拔出或总线异常 */
            group->rom_valid = 0U;
            group->ds18b20_online = 0U;
            memset(group->rom_code, 0, sizeof(group->rom_code));
        }
    }

    for (uint8_t group_index = 0U; group_index < SENSOR_GROUP_COUNT; group_index++)
    {
        SensorGroup_t *group = &g_sensor_groups[group_index];

        if ((group->detected == 0U) || (group->rom_valid != 0U))
        {
            continue;
        }

        for (uint8_t rom_index = 0U; rom_index < found; rom_index++)
        {
            if (rom_used[rom_index] == 0U)
            {
                /* 把尚未分配的 ROM 依次补给已插入但未绑定的组 */
                memcpy(group->rom_code, rom_codes[rom_index], DS18B20_ROM_CODE_SIZE);
                group->rom_valid = 1U;
                rom_used[rom_index] = 1U;
                break;
            }
        }
    }

    for (uint8_t group_index = 0U; group_index < SENSOR_GROUP_COUNT; group_index++)
    {
        uint8_t preferred_rom_index = s_ds18b20_rom_index_map[group_index];
        SensorGroup_t *group = &g_sensor_groups[group_index];

        if ((group->detected == 0U) || (preferred_rom_index >= found))
        {
            continue;
        }

        memcpy(group->rom_code, rom_codes[preferred_rom_index], DS18B20_ROM_CODE_SIZE);
        group->rom_valid = 1U;
    }
}

static void MarkDs18b20AssignmentsDirty(void)
{
    g_ds18b20_assignment_dirty = 1U;
}

/**
  * @brief  初始化指定组的传感器运行状态
  * @param  group_index: 传感器组索引，范围0~3
  * @note   在DET确认插入后调用，完成默认数据装载、DHT22初始化和LED点亮
  * @retval 无
  */
static void SensorGroup_Init(uint8_t group_index)
{
    SensorGroup_t *group = &g_sensor_groups[group_index];

    /* 插入确认后，先给该组一个可用的默认采样快照 */
    group->detected = 1U;
    group->initialized = 1U;
    group->dht22_online = 0U;
    group->ds18b20_online = 0U;
    group->temperature = 0.0f;
    group->humidity = 0.0f;
    group->smoke = ADC_ToPercent(g_adc_buffer[s_smoke_adc_index[group_index]]);
    group->light = ADC_ToLightPercent(g_adc_buffer[s_light_adc_index[group_index]]);

    (void)DHT22_InitGroup(group_index);
    MarkDs18b20AssignmentsDirty();
    /* LED 仅表示该组模块已接入并完成初始化，不表示告警 */
    GroupSetLed(group_index, 1U);
}

/**
  * @brief  反初始化指定组的传感器运行状态
  * @param  group_index: 传感器组索引，范围0~3
  * @note   在模块拔出后调用，清空该组数据并释放对应ROM绑定
  * @retval 无
  */
static void SensorGroup_Deinit(uint8_t group_index)
{
    g_sensor_groups[group_index].detected = 0U;
    GroupClearData(group_index);
    MarkDs18b20AssignmentsDirty();
    GroupSetLed(group_index, 0U);
}

/**
  * @brief  处理指定组的插拔状态变化
  * @param  group_index: 传感器组索引，范围0~3
  * @note   EXTI中断仅上报事件，实际消抖和初始化/下线逻辑在任务上下文中完成
  * @retval 无
  */
static void SensorGroup_HandleStateChange(uint8_t group_index)
{
    /* EXTI 只负责通知，这里在任务上下文里完成消抖和最终判定 */
    vTaskDelay(pdMS_TO_TICKS(DET_DEBOUNCE_MS));

    if (GroupIsInserted(group_index) != 0U)
    {
        if (g_sensor_groups[group_index].detected == 0U)
        {
            SensorGroup_Init(group_index);
        }
    }
    else if (g_sensor_groups[group_index].detected != 0U)
    {
        SensorGroup_Deinit(group_index);
    }
}

/**
  * @brief  处理DET外部中断事件
  * @param  GPIO_Pin: 触发中断的GPIO引脚号
  * @note   仅在中断里投递组事件，不做耗时初始化，避免阻塞中断上下文
  * @retval 无
  */
void SensorDetect_HandleExti(uint16_t GPIO_Pin)
{
    uint32_t notify_bits = 0U;
    BaseType_t higher_priority_task_woken = pdFALSE;

    for (uint8_t group_index = 0U; group_index < SENSOR_GROUP_COUNT; group_index++)
    {
        if (GPIO_Pin == s_det_pins[group_index])
        {
            /* 用 bit 位表示是哪一组触发，便于多个插槽共用一个检测任务 */
            notify_bits = (1UL << group_index);
            break;
        }
    }

    if ((notify_bits != 0U) && (SensorDetectTaskHandle != NULL))
    {
        (void)xTaskNotifyFromISR(SensorDetectTaskHandle, notify_bits, eSetBits, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
  * @brief  传感器插拔检测任务
  * @param  pvParameters: 任务参数，未使用
  * @note   上电先主动扫描四组DET状态，之后通过任务通知处理插拔事件
  * @retval 无
  */
static void SensorDetect_Task(void *pvParameters)
{
    uint32_t pending_bits;

    (void)pvParameters;

    /* 上电先主动检查一次四组插槽，保证冷启动也能识别已接入模块 */
    pending_bits = (1UL << SENSOR_GROUP_COUNT) - 1UL;

    while (1)
    {
        if (pending_bits == 0U)
        {
            (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &pending_bits, portMAX_DELAY);
        }

        for (uint8_t group_index = 0U; group_index < SENSOR_GROUP_COUNT; group_index++)
        {
            if ((pending_bits & (1UL << group_index)) != 0U)
            {
                SensorGroup_HandleStateChange(group_index);
            }
        }

        pending_bits = 0U;
    }
}

/**
  * @brief  四组传感器周期采样任务
  * @param  pvParameters: 任务参数，未使用
  * @note   STM32仅采集原始数据，不做告警判定；DS18B20按ROM定向读取
  * @retval 无
  */
static void SensorSample_Task(void *pvParameters)
{
    TickType_t last_wake_time;
    TickType_t ds18b20_convert_start = 0U;
    TickType_t ds18b20_elapsed_ticks;
    TickType_t ds18b20_wait_ticks = pdMS_TO_TICKS(DS18B20_CONVERT_WAIT_MS);
    uint8_t ds18b20_conversion_started;

    (void)pvParameters;

    /* DS18B20 是共享单总线，驱动只需要初始化一次 */
    (void)DS18B20_Init();
    last_wake_time = xTaskGetTickCount();

    /* 给 DHT22 上电预热时间，避免刚启动就读出错 */
    vTaskDelay(pdMS_TO_TICKS(DHT22_POWER_ON_DELAY_MS));

    while (1)
    {
        /* 仅在插拔变化后重新搜索单总线设备，避免每轮全总线搜索拖慢采样周期。 */
        if (g_ds18b20_assignment_dirty != 0U)
        {
            RefreshDs18b20Assignments();
            g_ds18b20_assignment_dirty = 0U;
        }
        ds18b20_conversion_started = 0U;

        for (uint8_t group_index = 0U; group_index < SENSOR_GROUP_COUNT; group_index++)
        {
            SensorGroup_t *group = &g_sensor_groups[group_index];

            if ((group->detected != 0U) && (group->initialized != 0U) && (group->rom_valid != 0U))
            {
                if (DS18B20_StartAllConversion() == DS18B20_OK)
                {
                    ds18b20_convert_start = xTaskGetTickCount();
                    ds18b20_conversion_started = 1U;
                }
                break;
            }
        }

        for (uint8_t group_index = 0U; group_index < SENSOR_GROUP_COUNT; group_index++)
        {
            SensorGroup_t *group = &g_sensor_groups[group_index];
            float temperature;
            float humidity;
            uint8_t dht_ok = 0U;

            /* MQ2 和光照来自 ADC DMA 缓冲区，按资料里的固定顺序取值 */
            group->smoke = ADC_ToPercent(g_adc_buffer[s_smoke_adc_index[group_index]]);
            group->light = ADC_ToLightPercent(g_adc_buffer[s_light_adc_index[group_index]]);

            if ((group->detected == 0U) || (group->initialized == 0U))
            {
                continue;
            }

            for (uint8_t retry = 0U; retry < DHT22_RETRY_COUNT; retry++)
            {
                if (DHT22_ReadGroup(group_index, &temperature, &humidity) == DHT22_OK)
                {
                    group->humidity = humidity;
                    group->dht22_online = 1U;
                    dht_ok = 1U;
                    break;
                }

                if ((retry + 1U) < DHT22_RETRY_COUNT)
                {
                    vTaskDelay(pdMS_TO_TICKS(DHT22_RETRY_DELAY_MS));
                }
            }

            if (dht_ok == 0U)
            {
                group->humidity = 0.0f;
                group->dht22_online = 0U;
            }

        }

        if (ds18b20_conversion_started != 0U)
        {
            ds18b20_elapsed_ticks = xTaskGetTickCount() - ds18b20_convert_start;
            if (ds18b20_elapsed_ticks < ds18b20_wait_ticks)
            {
                vTaskDelay(ds18b20_wait_ticks - ds18b20_elapsed_ticks);
            }
        }

        for (uint8_t group_index = 0U; group_index < SENSOR_GROUP_COUNT; group_index++)
        {
            SensorGroup_t *group = &g_sensor_groups[group_index];
            float temperature;
            uint8_t ds18b20_ok = 0U;

            if ((group->detected == 0U) || (group->initialized == 0U))
            {
                continue;
            }

            /* 温度按 ROM 定向读取，转换阶段已提前并行发起，避免四个探头串行各等 750ms */
            if ((ds18b20_conversion_started != 0U) &&
                (group->rom_valid != 0U) &&
                (DS18B20_ReadTemperatureByRomWithoutConvert(group->rom_code, &temperature) == DS18B20_OK))
            {
                ds18b20_ok = 1U;
            }

            for (uint8_t retry = 0U; (ds18b20_ok == 0U) && (retry < DS18B20_RETRY_COUNT); retry++)
            {
                if (group->rom_valid == 0U)
                {
                    break;
                }

                vTaskDelay(pdMS_TO_TICKS(DS18B20_RETRY_DELAY_MS));
                /* 初次已完成全总线并行转换，补读阶段只重读 scratchpad，避免每组重试再额外等待 750ms。 */
                if (DS18B20_ReadTemperatureByRomWithoutConvert(group->rom_code, &temperature) == DS18B20_OK)
                {
                    ds18b20_ok = 1U;
                }
            }

            if (ds18b20_ok != 0U)
            {
                group->temperature = temperature;
                group->ds18b20_online = 1U;
            }
            else
            {
                group->temperature = 0.0f;
                group->ds18b20_online = 0U;
            }
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

/**
  * @brief  向OLED指定行写入固定宽度字符串
  * @param  line: OLED行号，范围1~4
  * @param  text: 要显示的字符串
  * @note   每行固定16字符，先补空格可以避免短字符串覆盖后残影
  * @retval 无
  */
static void OLED_WriteLine(uint8_t line, const char *text)
{
    char buffer[17];

    /* OLED 每行固定 16 字符，先补齐再写可避免残影 */
    (void)snprintf(buffer, sizeof(buffer), "%-16.16s", text);
    OLED_ShowString(line, 1, buffer);
}

/**
  * @brief  OLED显示刷新任务
  * @param  pvParameters: 任务参数，未使用
  * @note   前两行显示四组在线概览，后两行轮播显示某一组详细原始数据
  * @retval 无
  */
static void OLED_Task(void *pvParameters)
{
    uint8_t detail_group = 0U;
    TickType_t last_rotate = 0U;
    char line[17];

    (void)pvParameters;

    if (OLED_Init() != OLED_OK)
    {
        Error_Handler();
    }

    while (1)
    {
        /* 前两行显示四组在线概览，后两行轮播一组详细数值 */
        (void)snprintf(line,
                       sizeof(line),
                       "G1:%s G2:%s",
                       (g_sensor_groups[0].detected != 0U) ? "ON " : "OFF",
                       (g_sensor_groups[1].detected != 0U) ? "ON " : "OFF");
        OLED_WriteLine(1U, line);

        (void)snprintf(line,
                       sizeof(line),
                       "G3:%s G4:%s",
                       (g_sensor_groups[2].detected != 0U) ? "ON " : "OFF",
                       (g_sensor_groups[3].detected != 0U) ? "ON " : "OFF");
        OLED_WriteLine(2U, line);

        if ((xTaskGetTickCount() - last_rotate) >= pdMS_TO_TICKS(OLED_ROTATE_PERIOD_MS))
        {
            detail_group = (uint8_t)((detail_group + 1U) % SENSOR_GROUP_COUNT);
            last_rotate = xTaskGetTickCount();
        }

        if (g_sensor_groups[detail_group].detected == 0U)
        {
            (void)snprintf(line, sizeof(line), "G%u OFFLINE", (unsigned int)(detail_group + 1U));
            OLED_WriteLine(3U, line);
            OLED_WriteLine(4U, "                ");
        }
        else
        {
            (void)snprintf(line,
                           sizeof(line),
                           "G%u T%4.1f H%4.1f",
                           (unsigned int)(detail_group + 1U),
                           g_sensor_groups[detail_group].temperature,
                           g_sensor_groups[detail_group].humidity);
            OLED_WriteLine(3U, line);

            (void)snprintf(line,
                           sizeof(line),
                           "S%4.1f L%4.1f",
                           g_sensor_groups[detail_group].smoke,
                           g_sensor_groups[detail_group].light);
            OLED_WriteLine(4U, line);
        }

        vTaskDelay(pdMS_TO_TICKS(200U));
    }
}

/**
  * @brief  串口数据发送任务
  * @param  pvParameters: 任务参数，未使用
  * @note   按固定周期通过USART1发送四组原始数据JSON帧，告警由ESP32侧处理
  * @retval 无
  */
static void UART_TX_Task(void *pvParameters)
{
    TickType_t last_wake_time;
    char tx_buffer[1024];
    int length;

    (void)pvParameters;

    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        /* STM32 只上传原始采样数据，告警逻辑留给 ESP32 统一处理 */
        length = snprintf(tx_buffer,
                          sizeof(tx_buffer),
                          "{\"groups\":["
                          "{\"online\":%s,\"temperature\":%.1f,\"humidity\":%.1f,\"smoke\":%.1f,\"light\":%.1f},"
                          "{\"online\":%s,\"temperature\":%.1f,\"humidity\":%.1f,\"smoke\":%.1f,\"light\":%.1f},"
                          "{\"online\":%s,\"temperature\":%.1f,\"humidity\":%.1f,\"smoke\":%.1f,\"light\":%.1f},"
                          "{\"online\":%s,\"temperature\":%.1f,\"humidity\":%.1f,\"smoke\":%.1f,\"light\":%.1f}"
                          "]}\r\n",
                          (g_sensor_groups[0].detected != 0U) ? "true" : "false",
                          g_sensor_groups[0].temperature,
                          g_sensor_groups[0].humidity,
                          g_sensor_groups[0].smoke,
                          g_sensor_groups[0].light,
                          (g_sensor_groups[1].detected != 0U) ? "true" : "false",
                          g_sensor_groups[1].temperature,
                          g_sensor_groups[1].humidity,
                          g_sensor_groups[1].smoke,
                          g_sensor_groups[1].light,
                          (g_sensor_groups[2].detected != 0U) ? "true" : "false",
                          g_sensor_groups[2].temperature,
                          g_sensor_groups[2].humidity,
                          g_sensor_groups[2].smoke,
                          g_sensor_groups[2].light,
                          (g_sensor_groups[3].detected != 0U) ? "true" : "false",
                          g_sensor_groups[3].temperature,
                          g_sensor_groups[3].humidity,
                          g_sensor_groups[3].smoke,
                          g_sensor_groups[3].light);

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
  * @brief  系统启动任务
  * @param  pvParameters: 任务参数，未使用
  * @note   负责初始化运行态数据、启动ADC DMA，并创建采集/显示/发送等任务
  * @retval 无
  */
void StartTask(void *pvParameters)
{
    BaseType_t task_status;

    (void)pvParameters;

    /* 运行态状态表统一从这里清零，后续所有任务共享这份数据 */
    memset(g_sensor_groups, 0, sizeof(g_sensor_groups));

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /* 8 路 ADC 通过 DMA 循环搬运到缓冲区，采样任务直接读最新值 */
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_buffer, ADC_CHANNEL_COUNT) != HAL_OK)
    {
        Error_Handler();
    }

    task_status = xTaskCreate(SensorDetect_Task,
                              SENSOR_DETECT_TASK_NAME,
                              SENSOR_DETECT_TASK_STACK,
                              NULL,
                              SENSOR_DETECT_TASK_PRIO,
                              &SensorDetectTaskHandle);
    if (task_status != pdPASS)
    {
        Error_Handler();
    }

    task_status = xTaskCreate(SensorSample_Task,
                              SENSOR_SAMPLE_TASK_NAME,
                              SENSOR_SAMPLE_TASK_STACK,
                              NULL,
                              SENSOR_SAMPLE_TASK_PRIO,
                              &SensorSampleTaskHandle);
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
