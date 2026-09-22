/*
 * motor.h
 *
 * L298N motor driver interface for 2-wheel rover
 * Direction control only (ENA/ENB jumpered to 5V on L298N module)
 *
 * Pin assignments (from spare GPIO list in STM32F446RE_GPIO_Pin_Map):
 *   PA9  (D8)  -> L298N IN1  (Motor A direction 1)
 *   PA10 (D2)  -> L298N IN2  (Motor A direction 2)
 *   PC7  (D9)  -> L298N IN3  (Motor B direction 1)
 *   PB3  (D3)  -> L298N IN4  (Motor B direction 2)
 *
 * NOTE: PB3 is also SWO trace pin. Safe as GPIO if SWO is unused.
 *       Verify no conflict in CubeMX.
 *
 * Forward:  IN1=HIGH, IN2=LOW,  IN3=HIGH, IN4=LOW
 * Stop:     IN1=LOW,  IN2=LOW,  IN3=LOW,  IN4=LOW   (coast stop)
 * Brake:    IN1=HIGH, IN2=HIGH, IN3=HIGH, IN4=HIGH   (short brake)
 *
 * IMPORTANT: "Forward" depends on your motor wiring polarity.
 *            If a wheel spins backward, swap IN1/IN2 or IN3/IN4 at
 *            the L298N terminal, or invert the defines below.
 */

#ifndef MOTOR_H
#define MOTOR_H

#include "main.h"

/* ---- Pin definitions ---- */
/* Must match MX_GPIO_Init() configuration in main.c */

#define MOTOR_A_IN1_PIN    GPIO_PIN_9
#define MOTOR_A_IN1_PORT   GPIOA

#define MOTOR_A_IN2_PIN    GPIO_PIN_10
#define MOTOR_A_IN2_PORT   GPIOA

#define MOTOR_B_IN3_PIN    GPIO_PIN_7
#define MOTOR_B_IN3_PORT   GPIOC

#define MOTOR_B_IN4_PIN    GPIO_PIN_3
#define MOTOR_B_IN4_PORT   GPIOB

/* ---- Function prototypes ---- */

void Motor_GPIO_Init(void);   /* Configure IN1-IN4 as push-pull outputs */
void Motor_Forward(void);     /* Both motors forward                    */
void Motor_Stop(void);        /* Both motors coast stop (all LOW)       */
void Motor_Brake(void);       /* Both motors short brake (all HIGH)     */

#endif /* MOTOR_H */
