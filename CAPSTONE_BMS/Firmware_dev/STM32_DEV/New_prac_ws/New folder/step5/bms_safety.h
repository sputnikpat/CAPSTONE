/*
 * bms_safety.h — string-level SOA protection (OV / UV / OC / sensor / stuck)
 *
 *  M1 Step 5.
 *
 *  ROLE
 *    Runs every loop, immediately after sensing and BEFORE anything else.
 *    It has override authority: it opens switches itself through
 *    bms_switch.h. Nothing else may close a string it has blocked.
 *
 *  IP GUARDRAIL
 *    This module is generic protection and contains NO reconfiguration
 *    logic: no precharge, no dV gate, no cooldown, no topology choice.
 *    Step 6 (reconfig.c) consumes only Safety_BlockMask() and must not be
 *    included from here.
 *
 *  POLICY (locked with the user, M1 Step 5)
 *    String fault (OV, UV, OC-discharge, OC-charge)
 *        -> open THAT string only, latch. The other string may continue.
 *    Sensor lost (I2C retries exhausted, or missing at boot)
 *        -> ALL switches off, latch.  An unsensed pack is an unsafe pack.
 *    Stuck switch (current through a string commanded OFF)
 *        -> ALL switches off incl. load switch, latch. The stuck pair
 *           cannot be opened; the load switch is the only remaining break.
 *    Every latch clears ONLY by MCU reset (Nucleo black RESET button,
 *    power cycle, or watchdog). There is no software clear path.
 *    After reset, nothing may be switched on until every sensor has given
 *    SAFETY_READY_SAMPLES clean samples, so a still-present fault re-latches
 *    before any connection.
 */

#ifndef BMS_SAFETY_H
#define BMS_SAFETY_H

#include <stdint.h>
#include "bms_status.h"

/* ---- Fault bits: 8 per string. String A = bits 0..7, B = bits 8..15 ---- */
#define SFLT_OV        (1u << 0)   /* over-voltage                        */
#define SFLT_UV        (1u << 1)   /* under-voltage                       */
#define SFLT_OCD       (1u << 2)   /* over-current, discharge direction   */
#define SFLT_OCC       (1u << 3)   /* over-current, charge direction      */
#define SFLT_STUCK     (1u << 4)   /* current flowing while commanded OFF */
#define SFLT_SENSOR    (1u << 5)   /* sensor unreadable                   */
#define SFLT_INJECTED  (1u << 7)   /* the trip came from fault injection  */

#define SFLT_SHIFT(s)        ((uint32_t)(s) * 8u)
#define SFLT_STR(s, bit)     ((uint32_t)(bit) << SFLT_SHIFT(s))

/* Faults that take the WHOLE pack down, not just their own string. */
#define SFLT_GLOBAL_MASK     (SFLT_STUCK | SFLT_SENSOR)

/* Block mask bits returned by Safety_BlockMask() */
#define SBLK_STR_A     (1u << 0)
#define SBLK_STR_B     (1u << 1)
#define SBLK_LOAD      (1u << 2)

/** Reset all state. Call once, after Switch_Init(), before the loop. */
void Safety_Init(uint32_t now_ms);

/** Latch a sensor fault found by the boot-time ID probe. */
void Safety_LatchSensorAtBoot(uint8_t str);

/**
 * @brief  Evaluate one loop's sensor snapshot and enforce the result.
 *         Opens faulted strings / all switches itself, every call
 *         (re-asserted, so nothing can sneak a faulted string back on).
 * @param  snap  st.str[] from main.c, both strings, filled THIS cycle
 * @param  now_ms HAL tick of this cycle
 * @return 1 if at least one NEW fault latched in this call, else 0
 *         (main.c uses it to stop the motors and log the event once).
 */
uint8_t Safety_Step(const BmsStringSnap snap[BMS_NUM_STR], uint32_t now_ms);

/** All latched fault bits (layout above). 0 = healthy. */
uint32_t Safety_Flags(void);

/** 1 once every sensor has given SAFETY_READY_SAMPLES clean samples. */
uint8_t Safety_Ready(void);

/** 1 if no string may run (both faulted, or a global fault). */
uint8_t Safety_AllStringsDown(void);

/** What may NOT be switched on right now (SBLK_* bits). Any caller that
 *  closes a switch — bench_debug now, reconfig.c in Step 6 — must check
 *  this first. Not-ready counts as blocked. */
uint8_t Safety_BlockMask(void);

/** Short label for OLED row 5 / UART, e.g. "A:-- B:OCD*" (<= 17 chars). */
const char *Safety_Label(void);

/** Demo: override this string's current as seen by safety only.
 *  No-op if BMS_FAULT_INJECTION == 0. Persists until reset. */
void Safety_InjectOC(uint8_t str);

#endif /* BMS_SAFETY_H */
