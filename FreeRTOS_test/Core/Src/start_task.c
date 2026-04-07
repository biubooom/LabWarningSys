#include "start_task.h"

#include "gpio.h"
#include "key.h"

TaskHandle_t StartTaskHandle;

static TaskHandle_t LED0BlinkHandle;
static TaskHandle_t LED1BlinkHandle;
static TaskHandle_t KeyScanHandle;

/*LED0*/
#define LED0_TASK_NAME             "LED0_Blink"
#define LED0_TASK_STACK_SIZE       128U
#define LED0_TASK_PRIORITY         (tskIDLE_PRIORITY + 1)
/*LED1 */
#define LED1_TASK_NAME             "LED1_Blink"
#define LED1_TASK_STACK_SIZE       128U
#define LED1_TASK_PRIORITY         (tskIDLE_PRIORITY + 1)
/*Key Scan*/
#define KEY_SCAN_TASK_NAME         "Key_Scan"
#define KEY_SCAN_TASK_STACK_SIZE   128U
#define KEY_SCAN_TASK_PRIORITY     (tskIDLE_PRIORITY + 2)

#define LED0_Enable(x)  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, (GPIO_PinState)(x))
#define LED0_Toggle()  HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin)
#define LED1_Toggle()  HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin)

static void LED0_Blink_Task(void *pvParameters)
{
    (void)pvParameters;  //避免编译器警告，使用该参数时删除

    while (1)
    {
        LED0_Toggle();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

static void LED1_Blink_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        LED1_Toggle();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


/**
* @brief  Key scan task
* @param  pvParameters: Task parameters (not used)
* @note   按下KEY0暂停/恢复LED0闪烁，
*         按下KEY1暂停/恢复LED1闪烁，
*         按下KEY2切换LED0状态，
*  按下KEY3切换LED1状态
 */
static void Key_Scan_Task(void *pvParameters)
{
    uint8_t key_value;
    uint8_t led0_task_paused = 0U;
    uint8_t led1_task_paused = 0U;

    (void)pvParameters;

    while (1)
    {
        key_value = Key_Scan(0);

        switch (key_value)
        {
            case KEY0_PRESS:
                if (led0_task_paused == 0U)
                {
                    vTaskSuspend(LED0BlinkHandle);
                    led0_task_paused = 1U;
                }
                else
                {
                    vTaskResume(LED0BlinkHandle);
                    led0_task_paused = 0U;
                }
                break;

            case KEY1_PRESS:
                if (led1_task_paused == 0U)
                {
                    vTaskSuspend(LED1BlinkHandle);
                    led1_task_paused = 1U;
                }
                else
                {
                    vTaskResume(LED1BlinkHandle);
                    led1_task_paused = 0U;
                }
                break;

            case KEY2_PRESS:
                LED0_Toggle();
                break;

            case KEY3_PRESS:
                LED1_Toggle();
                break;

            default:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void StartTask(void *pvParameters)
{
    BaseType_t task_status;

    (void)pvParameters;

    task_status = xTaskCreate(LED0_Blink_Task, LED0_TASK_NAME, LED0_TASK_STACK_SIZE, NULL, LED0_TASK_PRIORITY, &LED0BlinkHandle);
    if (task_status != pdPASS)
    {
        Error_Handler();
    }

    task_status = xTaskCreate(LED1_Blink_Task, LED1_TASK_NAME, LED1_TASK_STACK_SIZE, NULL, LED1_TASK_PRIORITY, &LED1BlinkHandle);
    if (task_status != pdPASS)
    {
        Error_Handler();
    }

    task_status = xTaskCreate(Key_Scan_Task, KEY_SCAN_TASK_NAME, KEY_SCAN_TASK_STACK_SIZE, NULL, KEY_SCAN_TASK_PRIORITY, &KeyScanHandle);
    if (task_status != pdPASS)
    {
        Error_Handler();
    }

    vTaskDelete(NULL);
}
