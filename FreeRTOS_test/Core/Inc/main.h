/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define G1_MQ2_Pin GPIO_PIN_0
#define G1_MQ2_GPIO_Port GPIOA
#define G2_MQ2_Pin GPIO_PIN_1
#define G2_MQ2_GPIO_Port GPIOA
#define G3_MQ2_Pin GPIO_PIN_2
#define G3_MQ2_GPIO_Port GPIOA
#define G4_MQ2_Pin GPIO_PIN_3
#define G4_MQ2_GPIO_Port GPIOA
#define G1_Light_Pin GPIO_PIN_4
#define G1_Light_GPIO_Port GPIOA
#define G2_Light_Pin GPIO_PIN_5
#define G2_Light_GPIO_Port GPIOA
#define G3_Light_Pin GPIO_PIN_6
#define G3_Light_GPIO_Port GPIOA
#define G4_Light_Pin GPIO_PIN_7
#define G4_Light_GPIO_Port GPIOA
#define G4_DHT22_DQ_Pin GPIO_PIN_2
#define G4_DHT22_DQ_GPIO_Port GPIOB
#define G4_DET_Pin GPIO_PIN_10
#define G4_DET_GPIO_Port GPIOB
#define G1_LED_Pin GPIO_PIN_11
#define G1_LED_GPIO_Port GPIOB
#define G2_LED_Pin GPIO_PIN_12
#define G2_LED_GPIO_Port GPIOB
#define G3_LED_Pin GPIO_PIN_13
#define G3_LED_GPIO_Port GPIOB
#define G4_LED_Pin GPIO_PIN_14
#define G4_LED_GPIO_Port GPIOB
#define OW_DQ_Pin GPIO_PIN_8
#define OW_DQ_GPIO_Port GPIOA
#define G1_DHT22_DQ_Pin GPIO_PIN_11
#define G1_DHT22_DQ_GPIO_Port GPIOA
#define G2_DHT22_DQ_Pin GPIO_PIN_12
#define G2_DHT22_DQ_GPIO_Port GPIOA
#define G3_DHT22_DQ_Pin GPIO_PIN_15
#define G3_DHT22_DQ_GPIO_Port GPIOA
#define OLED_SCL_Pin GPIO_PIN_3
#define OLED_SCL_GPIO_Port GPIOB
#define OLED_SDA_Pin GPIO_PIN_4
#define OLED_SDA_GPIO_Port GPIOB
#define Key0_Pin GPIO_PIN_5
#define Key0_GPIO_Port GPIOB
#define Key1_Pin GPIO_PIN_6
#define Key1_GPIO_Port GPIOB
#define G1_DET_Pin GPIO_PIN_7
#define G1_DET_GPIO_Port GPIOB
#define G1_DET_EXTI_IRQn EXTI9_5_IRQn
#define G2_DET_Pin GPIO_PIN_8
#define G2_DET_GPIO_Port GPIOB
#define G2_DET_EXTI_IRQn EXTI9_5_IRQn
#define G3_DET_Pin GPIO_PIN_9
#define G3_DET_GPIO_Port GPIOB
#define G3_DET_EXTI_IRQn EXTI9_5_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
