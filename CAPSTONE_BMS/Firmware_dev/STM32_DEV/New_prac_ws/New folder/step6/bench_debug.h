/*
 * bench_debug.h — UART bench commands
 *
 *  M1 Step 4, updated in Step 5 and Step 6.
 *
 *  STEP 6: reconfig.c now owns every switch-ON decision. Two code paths
 *  closing switches would be a design error, so the manual selector
 *  (a / b / l) is compiled out by BENCH_MANUAL_SWITCHING = 0. If it were
 *  left in, reconfig would see a switch it did not command and go to
 *  LOCKOUT (reconfig.h, G5) — safe, but useless.
 *
 *  Commands kept (terminal at 115200 8N1):
 *    0   all switches off = EMERGENCY STOP. Reconfig sees switches it did
 *        not open and enters LOCKOUT until RESET. Always accepted.
 *    f a inject an over-current on String A (safety layer only)
 *    f b inject an over-current on String B  <- the demo trigger
 *        'f' arms injection for 3 s; the next 'a'/'b' picks the string.
 *        Only present when BMS_FAULT_INJECTION == 1 (bms_safety_cfg.h).
 *    Faults and LOCKOUT clear only with the Nucleo RESET button.
 *
 *  BENCH_MANUAL_SWITCHING = 1 restores the Step 5 manual selector for
 *  hardware bring-up ONLY. Do not run it together with reconfig.
 */

#ifndef BENCH_DEBUG_H
#define BENCH_DEBUG_H

#include "main.h"

#define BENCH_MANUAL_SWITCHING   0

/** Poll USART2 for one command character and act on it.
 *  @param block_mask  Safety_BlockMask() for this cycle (SBLK_* bits);
 *                     used only by the manual selector. Non-blocking. */
void BenchDebug_Poll(UART_HandleTypeDef *huart, uint8_t block_mask);

#endif /* BENCH_DEBUG_H */
