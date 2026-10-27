/*
 * bms_switch.h — low-level MOSFET switch control
 *
 *  SCOPE: this module is deliberately DUMB. It knows how to set and clear
 *  individual switches and how to force everything off. It does NOT know
 *  about break-before-make ordering, precharge, dV gating or cooldown —
 *  that sequencing is the project's novel contribution and lives in its
 *  own module (reconfig.c) so it stays separable from generic code.
 *
 *  HARDWARE (from STM32F446RE_Pin_Map_v3.md and the fabricated PCB):
 *    PB4  -> Q3 base -> Gate_Q1  -> Q1/Q2 pair  : String A main
 *    PB5  -> Q4 base -> Gate_Q5  -> Q5/Q6 pair  : String B main
 *    PB10 -> Q7 base -> Gate_Q8  -> Q8/Q9 pair  : Load disconnect
 *
 *  Logic sense: GPIO HIGH -> NPN saturates -> gate pulled to ~0.3 V ->
 *  VGS ~ -10.6 V -> P-channel pair conducts. GPIO LOW -> 10k pull-up
 *  holds gate at source -> VGS = 0 -> pair off, blocking both directions.
 *
 *  >> PRECHARGE IS NOT POPULATED ON THIS BOARD. The milestone plan
 *     specifies a 4th pair (PB6) plus a 10 ohm resistor and bulk cap, but
 *     the fabricated PCB has only three pairs. Any parallel-connect must
 *     therefore satisfy the dV < 0.1 V gate by waiting for natural
 *     settling, NOT by precharging. M0 measured 218.8 A on a cold connect
 *     without precharge — the state machine must never close a second
 *     string onto a live bus with an unverified dV.
 *
 *  >> VERIFY in CubeMX: SYS -> Debug -> Serial Wire. PB4 is NJTRST and
 *     PB3 is JTDO at reset; if debug mode is left at JTAG neither works
 *     as GPIO and String A simply will not switch.
 */

#ifndef BMS_SWITCH_H
#define BMS_SWITCH_H

#include "main.h"

/* ---- Pin definitions ---- */
#define SW_PORT             GPIOB

#define SW_STRING_A_PIN     GPIO_PIN_4
#define SW_STRING_B_PIN     GPIO_PIN_5
#define SW_LOAD_PIN         GPIO_PIN_10

/* All switch pins, for a single atomic all-off write */
#define SW_ALL_PINS         (SW_STRING_A_PIN | SW_STRING_B_PIN | SW_LOAD_PIN)

typedef enum {
    SW_STRING_A = 0,
    SW_STRING_B,
    SW_LOAD,
    SW_COUNT
} SwitchId;

typedef enum {
    SW_OFF = 0,
    SW_ON  = 1
} SwitchState;

/**
 * @brief  Configure all switch GPIOs as outputs and drive them LOW.
 *         MUST be called before anything else that could energise the bus.
 *         Safe to call repeatedly.
 */
void Switch_Init(void);

/**
 * @brief  Set one switch. No sequencing, no interlocks — the caller owns
 *         ordering. Returns 1 if the id was valid.
 */
uint8_t Switch_Set(SwitchId id, SwitchState state);

/**
 * @brief  Read back the commanded state of one switch (what the GPIO
 *         output register says, NOT what the MOSFET is actually doing).
 *         Real state validation needs a current/voltage readback and is
 *         done in the state machine.
 */
SwitchState Switch_Get(SwitchId id);

/**
 * @brief  Force every switch off in a single register write.
 *         Uses direct BSRR access, not HAL, so it still works if HAL
 *         itself is the failed subsystem. Interrupt-safe and atomic.
 *         This is the safe state: all pairs blocking in both directions.
 */
void Switch_AllOff(void);

/**
 * @brief  Human-readable name, for OLED/UART.
 */
const char *Switch_Name(SwitchId id);

#endif /* BMS_SWITCH_H */
