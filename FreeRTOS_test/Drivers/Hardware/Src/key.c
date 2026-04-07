#include "key.h"

#include "stm32f1xx_hal.h"

static uint8_t key_up = 1U;
static uint32_t key_tick = 0U;

/**
 * @brief  Scan the keys
 * @param  mode: 0 - scan all keys, 1 - scan specific key
 * @retval The state of the key
 */
uint8_t Key_Scan(uint8_t mode)
{
    uint8_t key_pressed = 0U;

    if (mode != 0U)
    {
        key_up = 1U;
    }

    key_pressed = (uint8_t)(
        (HAL_GPIO_ReadPin(Key0_GPIO_Port, Key0_Pin) == GPIO_PIN_RESET) ||
        (HAL_GPIO_ReadPin(Key1_GPIO_Port, Key1_Pin) == GPIO_PIN_RESET) ||
        (HAL_GPIO_ReadPin(Key2_GPIO_Port, Key2_Pin) == GPIO_PIN_RESET) ||
        (HAL_GPIO_ReadPin(Key3_GPIO_Port, Key3_Pin) == GPIO_PIN_RESET));

    if ((key_up == 1U) && (key_pressed != 0U))
    {
        if ((HAL_GetTick() - key_tick) < 10U)
        {
            return KEY_NONE;
        }

        key_tick = HAL_GetTick();
        key_up = 0U;

        if (HAL_GPIO_ReadPin(Key0_GPIO_Port, Key0_Pin) == GPIO_PIN_RESET)
        {
            return KEY0_PRESS;
        }

        if (HAL_GPIO_ReadPin(Key1_GPIO_Port, Key1_Pin) == GPIO_PIN_RESET)
        {
            return KEY1_PRESS;
        }

        if (HAL_GPIO_ReadPin(Key2_GPIO_Port, Key2_Pin) == GPIO_PIN_RESET)
        {
            return KEY2_PRESS;
        }

        if (HAL_GPIO_ReadPin(Key3_GPIO_Port, Key3_Pin) == GPIO_PIN_RESET)
        {
            return KEY3_PRESS;
        }
    }
    else if (key_pressed == 0U)
    {
        key_up = 1U;
    }

    return KEY_NONE;
}
