/*
 * bench_debug.h — manual switch commands over UART, FOR BENCH TESTS ONLY
 *
 *  M1 Step 4, updated in Step 5:
 *    - takes the safety layer's block mask instead of one fault flag, so a
 *      fault on String B no longer stops you selecting healthy String A
 *    - adds fault injection ("f" then "a"/"b")
 *
 *  THIS IS NOT THE RECONFIGURATION LOGIC. It is a deliberately crippled
 *  manual selector:
 *    - it can NEVER have string A and string B on at the same time
 *      (so the dV < 0.1 V parallel rule is never in play)
 *    - every string change is break-before-make: all strings off, wait
 *      BENCH_BBM_MS, then turn the selected one on
 *    - 500 ms minimum between accepted switch commands (anti-chatter)
 *    - a command is refused if Safety_BlockMask() blocks that switch
 *      (string faulted, global fault, or safety not yet ready after boot)
 *    - it does NOT validate that a MOSFET actually moved; the safety
 *      layer's STUCK check covers the OFF direction, Step 6 the ON one.
 *
 *  Compile it out by setting BMS_BENCH_DEBUG to 0 (main.c). It must be
 *  removed or disabled once Step 6 owns the switches.
 *
 *  Commands (single character, terminal at 115200 8N1):
 *    0   all switches off (always accepted, never rate-limited)
 *    a   string A only   (B forced off first)
 *    b   string B only   (A forced off first)
 *    l   toggle load-disconnect switch
 *    f a inject an over-current on String A (safety layer only)
 *    f b inject an over-current on String B
 *        'f' arms injection for 3 s; the next 'a'/'b' picks the string.
 *        Only present when BMS_FAULT_INJECTION == 1 (bms_safety_cfg.h).
 *    Faults clear only with the Nucleo RESET button.
 */

#ifndef BENCH_DEBUG_H
#define BENCH_DEBUG_H

#include "main.h"

/** Poll USART2 for one command character and act on it.
 *  @param block_mask  Safety_BlockMask() for this cycle (SBLK_* bits)
 *  Non-blocking except for the BENCH_BBM_MS dead time on a string change. */
void BenchDebug_Poll(UART_HandleTypeDef *huart, uint8_t block_mask);

#endif /* BENCH_DEBUG_H */
