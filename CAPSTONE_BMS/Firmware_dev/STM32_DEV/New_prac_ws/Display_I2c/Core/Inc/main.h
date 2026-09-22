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
#include "stm32f4xx_hal.h"

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

/* ---- Existing Nucleo defaults ---- */
#define USART_TX_Pin        GPIO_PIN_2
#define USART_TX_GPIO_Port  GPIOA
#define USART_RX_Pin        GPIO_PIN_3
#define USART_RX_GPIO_Port  GPIOA
#define LD2_Pin             GPIO_PIN_5
#define LD2_GPIO_Port       GPIOA
#define TMS_Pin             GPIO_PIN_13
#define TMS_GPIO_Port       GPIOA
#define TCK_Pin             GPIO_PIN_14
#define TCK_GPIO_Port       GPIOA
#define SWO_Pin             GPIO_PIN_3
#define SWO_GPIO_Port       GPIOB

/* USER CODE BEGIN Private defines */

/* ---- L298N Motor Driver pins ----
 * PA9  (D8)  -> IN1  Motor A
 * PA10 (D2)  -> IN2  Motor A
 * PC7  (D9)  -> IN3  Motor B
 * PB3  (D3)  -> IN4  Motor B
 *
 * NOTE: PB3 overlaps SWO_Pin define above.
 *       SWO trace is unused in this project (debug via USART2).
 *       CubeMX will reconfigure PB3 from SWO to GPIO_Output.
 *       The SWO_Pin define above is CubeMX boilerplate — harmless
 *       as long as we don't reference it in our code.
 */
#define MOTOR_A_IN1_Pin     GPIO_PIN_9
#define MOTOR_A_IN1_Port    GPIOA
#define MOTOR_A_IN2_Pin     GPIO_PIN_10
#define MOTOR_A_IN2_Port    GPIOA
#define MOTOR_B_IN3_Pin     GPIO_PIN_7
#define MOTOR_B_IN3_Port    GPIOC
#define MOTOR_B_IN4_Pin     GPIO_PIN_3
#define MOTOR_B_IN4_Port    GPIOB

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
