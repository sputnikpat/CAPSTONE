/*
 * motor.c
 *
 * L298N motor driver — direction control for 2-wheel rover
 *
 * ENA/ENB are assumed jumpered to 5V on the L298N module (full speed).
 * Only IN1–IN4 are driven by GPIO.
 *
 * Pin mapping:
 *   PA9  -> IN1 (Motor A)
 *   PA10 -> IN2 (Motor A)
 *   PC7  -> IN3 (Motor B)
 *   PB3  -> IN4 (Motor B)
 */

#include "motor.h"

/**
 * @brief  Configure IN1–IN4 as push-pull GPIO outputs, all LOW (motors off).
 *         Call this once during init, AFTER HAL_Init() and clock setup.
 *
 *         NOTE: If you are using CubeMX-generated MX_GPIO_Init(), you can
 *         configure these pins there instead and skip calling this function.
 *         This manual init is provided so motor.c is self-contained.
 */
void Motor_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable clocks for all ports used by motor pins */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Start with all outputs LOW — motors off */
    HAL_GPIO_WritePin(MOTOR_A_IN1_PORT, MOTOR_A_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_A_IN2_PORT, MOTOR_A_IN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_B_IN3_PORT, MOTOR_B_IN3_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_B_IN4_PORT, MOTOR_B_IN4_PIN, GPIO_PIN_RESET);

    /* PA9 — IN1 */
    GPIO_InitStruct.Pin   = MOTOR_A_IN1_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MOTOR_A_IN1_PORT, &GPIO_InitStruct);

    /* PA10 — IN2 */
    GPIO_InitStruct.Pin = MOTOR_A_IN2_PIN;
    HAL_GPIO_Init(MOTOR_A_IN2_PORT, &GPIO_InitStruct);

    /* PC7 — IN3 */
    GPIO_InitStruct.Pin = MOTOR_B_IN3_PIN;
    HAL_GPIO_Init(MOTOR_B_IN3_PORT, &GPIO_InitStruct);

    /* PB3 — IN4 */
    GPIO_InitStruct.Pin = MOTOR_B_IN4_PIN;
    HAL_GPIO_Init(MOTOR_B_IN4_PORT, &GPIO_InitStruct);
}

/**
 * @brief  Drive both motors forward.
 *         Motor A: IN1=HIGH, IN2=LOW
 *         Motor B: IN3=HIGH, IN4=LOW
 *
 *         If a wheel spins the wrong way, swap the two IN pins
 *         for that motor at the L298N screw terminal (easiest fix)
 *         or invert the logic here.
 */
void Motor_Forward(void)
{
    /* Motor A forward */
    HAL_GPIO_WritePin(MOTOR_A_IN1_PORT, MOTOR_A_IN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_A_IN2_PORT, MOTOR_A_IN2_PIN, GPIO_PIN_RESET);

    /* Motor B forward */
    HAL_GPIO_WritePin(MOTOR_B_IN3_PORT, MOTOR_B_IN3_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_B_IN4_PORT, MOTOR_B_IN4_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  Coast stop — all inputs LOW, motors spin down freely.
 */
void Motor_Stop(void)
{
    HAL_GPIO_WritePin(MOTOR_A_IN1_PORT, MOTOR_A_IN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_A_IN2_PORT, MOTOR_A_IN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_B_IN3_PORT, MOTOR_B_IN3_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_B_IN4_PORT, MOTOR_B_IN4_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  Short brake — all inputs HIGH, motor terminals shorted through
 *         the H-bridge low-side. Motors stop quickly but draws current
 *         briefly. Use for fast stop; use Motor_Stop() for gentle coast.
 */
void Motor_Brake(void)
{
    HAL_GPIO_WritePin(MOTOR_A_IN1_PORT, MOTOR_A_IN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_A_IN2_PORT, MOTOR_A_IN2_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_B_IN3_PORT, MOTOR_B_IN3_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_B_IN4_PORT, MOTOR_B_IN4_PIN, GPIO_PIN_SET);
}
