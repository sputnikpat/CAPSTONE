/*
 * motor.h
 *
 * L298N motor driver interface for 2-wheel rover
 * Direction control only (ENA/ENB jumpered to 5V on the L298N module)
 *
 * Pin assignments — taken from the PCB netlist (Switching_PCB.net,
 * nets PA9 / PA10 / PB3 / PC7 -> U4 pins 7 / 8 / 9 / 10):
 *   PA9  (D8)  -> L298N IN1  (Motor A direction 1)
 *   PA10 (D2)  -> L298N IN2  (Motor A direction 2)
 *   PB3  (D3)  -> L298N IN3  (Motor B direction 1)
 *   PC7  (D9)  -> L298N IN4  (Motor B direction 2)
 *
 * M1 Step 5 CHANGE: IN3 and IN4 were swapped relative to the board
 * (old file: IN3 = PC7, IN4 = PB3). The netlist routes PB3 to IN3 and PC7
 * to IN4. With the old mapping Motor_Forward() drove Motor B in REVERSE.
 * Motor_Stop() (all LOW) was unaffected, so this was never a safety issue.
 *
 * NOTE: PB3 is the SWO trace pin and JTDO at reset. CubeMX must be
 *       SYS -> Debug -> Serial Wire (not "Trace Asynchronous Sw"), or PB3
 *       is not a GPIO. Its level between reset and Motor_GPIO_Init() is
 *       the debug-pin reset state [VERIFY: RM0390, GPIO "after reset"];
 *       bench test S5-T2 checks that the motor does not move during reset.
 *
 * Forward:  IN1=HIGH, IN2=LOW,  IN3=HIGH, IN4=LOW
 * Stop:     IN1=LOW,  IN2=LOW,  IN3=LOW,  IN4=LOW   (coast stop)
 * Brake:    IN1=HIGH, IN2=HIGH, IN3=HIGH, IN4=HIGH   (short brake)
 *
 * IMPORTANT: "Forward" still depends on motor wiring polarity at the
 *            L298N output terminals. If a wheel spins backward, swap that
 *            motor's two output wires.
 */

#ifndef MOTOR_H
#define MOTOR_H

#include "main.h"

#define MOTOR_A_IN1_PIN    GPIO_PIN_9
#define MOTOR_A_IN1_PORT   GPIOA

#define MOTOR_A_IN2_PIN    GPIO_PIN_10
#define MOTOR_A_IN2_PORT   GPIOA

#define MOTOR_B_IN3_PIN    GPIO_PIN_3      /* PB3 -> IN3 (netlist) */
#define MOTOR_B_IN3_PORT   GPIOB

#define MOTOR_B_IN4_PIN    GPIO_PIN_7      /* PC7 -> IN4 (netlist) */
#define MOTOR_B_IN4_PORT   GPIOC

void Motor_GPIO_Init(void);   /* Configure IN1-IN4 as push-pull outputs */
void Motor_Forward(void);     /* Both motors forward                    */
void Motor_Stop(void);        /* Both motors coast stop (all LOW)       */
void Motor_Brake(void);       /* Both motors short brake (all HIGH)     */

#endif /* MOTOR_H */
