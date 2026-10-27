#ifndef FAKE_MAIN_H
#define FAKE_MAIN_H
/* FAKE HAL for a syntax check only. Not the real STM32 HAL. */
#include <stdint.h>
typedef struct { volatile uint32_t BSRR, ODR; } GPIO_TypeDef;
typedef struct { volatile uint32_t DR; } USART_TypeDef;
typedef struct { USART_TypeDef *Instance; struct { uint32_t BaudRate,WordLength,StopBits,Parity,Mode,HwFlowCtl,OverSampling; } Init; } UART_HandleTypeDef;
typedef struct { void *Instance; struct { uint32_t ClockSpeed,DutyCycle,OwnAddress1,AddressingMode,DualAddressMode,OwnAddress2,GeneralCallMode,NoStretchMode; } Init; } I2C_HandleTypeDef;
typedef struct { uint32_t Pin,Mode,Pull,Speed; } GPIO_InitTypeDef;
typedef struct { uint32_t OscillatorType,HSIState,HSICalibrationValue; struct { uint32_t PLLState,PLLSource,PLLM,PLLN,PLLP,PLLQ,PLLR; } PLL; } RCC_OscInitTypeDef;
typedef struct { uint32_t ClockType,SYSCLKSource,AHBCLKDivider,APB1CLKDivider,APB2CLKDivider; } RCC_ClkInitTypeDef;
typedef enum { HAL_OK } HAL_StatusTypeDef;
#define GPIOA ((GPIO_TypeDef*)0x40020000u)
#define GPIOB ((GPIO_TypeDef*)0x40020400u)
#define GPIOC ((GPIO_TypeDef*)0x40020800u)
 extern void *I2C1,*I2C3; extern USART_TypeDef *USART2;
typedef struct { uint32_t DEMCR; } CD_t; extern CD_t *CoreDebug; typedef struct { uint32_t CYCCNT, CTRL; } DWT_t; extern DWT_t *DWT; typedef struct { uint32_t APB1FZ; } DBG_t; extern DBG_t *DBGMCU; typedef struct { uint32_t KR,PR,RLR,SR; } IW_t; extern IW_t *IWDG;
extern uint32_t SystemCoreClock;
#define CoreDebug_DEMCR_TRCENA_Msk 1u
#define DWT_CTRL_CYCCNTENA_Msk 1u
#define DBGMCU_APB1_FZ_DBG_IWDG_STOP 1u
#define IWDG_SR_PVU 1u
#define IWDG_SR_RVU 2u
#define GPIO_PIN_4 0x10u
#define GPIO_PIN_5 0x20u
#define GPIO_PIN_8 0x100u
#define GPIO_PIN_10 0x400u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_RESET 0
#define GPIO_MODE_IT_RISING 0
#define GPIO_MODE_OUTPUT_PP 0
#define GPIO_NOPULL 0
#define GPIO_SPEED_FREQ_LOW 0
#define LD2_GPIO_Port GPIOA
#define LD2_Pin 0x20u
#define EXTI15_10_IRQn 40
#define RCC_FLAG_IWDGRST 1
#define RCC_FLAG_WWDGRST 2
#define RCC_FLAG_LPWRRST 3
#define RCC_FLAG_SFTRST 4
#define RCC_FLAG_PORRST 5
#define RCC_FLAG_BORRST 6
#define RCC_FLAG_PINRST 7
#define __HAL_RCC_GET_FLAG(f) ((f)==0)
#define __HAL_RCC_CLEAR_RESET_FLAGS() ((void)0)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOH_CLK_ENABLE() ((void)0)
#define __HAL_PWR_VOLTAGESCALING_CONFIG(x) ((void)0)
#define __HAL_UART_GET_FLAG(h,f) (((h)->Instance->DR & (f)) != 0)
#define __HAL_UART_CLEAR_OREFLAG(h) ((void)0)
#define UART_FLAG_ORE 8u
#define UART_FLAG_RXNE 32u
#define __disable_irq() ((void)0)
#define PWR_REGULATOR_VOLTAGE_SCALE3 0
#define RCC_OSCILLATORTYPE_HSI 0
#define RCC_HSI_ON 0
#define RCC_HSICALIBRATION_DEFAULT 0
#define RCC_PLL_ON 0
#define RCC_PLLSOURCE_HSI 0
#define RCC_PLLP_DIV4 0
#define RCC_CLOCKTYPE_HCLK 1
#define RCC_CLOCKTYPE_SYSCLK 2
#define RCC_CLOCKTYPE_PCLK1 4
#define RCC_CLOCKTYPE_PCLK2 8
#define RCC_SYSCLKSOURCE_PLLCLK 0
#define RCC_SYSCLK_DIV1 0
#define RCC_HCLK_DIV2 0
#define RCC_HCLK_DIV1 0
#define FLASH_LATENCY_2 0
#define I2C_DUTYCYCLE_2 0
#define I2C_ADDRESSINGMODE_7BIT 0
#define I2C_DUALADDRESS_DISABLE 0
#define I2C_GENERALCALL_DISABLE 0
#define I2C_NOSTRETCH_DISABLE 0
#define UART_WORDLENGTH_8B 0
#define UART_STOPBITS_1 0
#define UART_PARITY_NONE 0
#define UART_MODE_TX_RX 0
#define UART_HWCONTROL_NONE 0
#define UART_OVERSAMPLING_16 0
void HAL_Init(void); uint32_t HAL_GetTick(void); void HAL_Delay(uint32_t);
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef*); HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef*, uint32_t);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef*); HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef*);
void HAL_GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*); void HAL_GPIO_WritePin(GPIO_TypeDef*, uint32_t, int); void HAL_GPIO_TogglePin(GPIO_TypeDef*, uint32_t);
void HAL_NVIC_SetPriority(int,int,int); void HAL_NVIC_EnableIRQ(int);
void Error_Handler(void);
#endif
