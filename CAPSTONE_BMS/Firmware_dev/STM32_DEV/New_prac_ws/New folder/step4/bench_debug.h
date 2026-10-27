/*
 * bench_debug.h — manual switch commands over UART, FOR BENCH TESTS ONLY
 *
 *  M1 Step 4. Exists so the Step 4 bench tests can close one string at a
 *  time and watch the sensors respond, before any control logic exists.
 *
 *  THIS IS NOT THE RECONFIGURATION LOGIC. It is a deliberately crippled
 *  manual selector:
 *    - it can NEVER have string A and string B on at the same time
 *      (so the dV < 0.1 V parallel rule is never in play)
 *    - every string change is break-before-make: all strings off, wait
 *      BENCH_BBM_MS, then turn the selected one on
 *    - 500 ms minimum between accepted commands (anti-chatter)
 *    - refused entirely while a fault is latched (except '0')
 *    - it does NOT validate that a MOSFET actually moved. YOU validate by
 *      reading the current on the OLED / CSV. Automatic readback is Step 6.
 *
 *  Compile it out by setting BMS_BENCH_DEBUG to 0 (main.c). It must be
 *  removed or disabled once Step 6 owns the switches.
 *
 *  Commands (single character, terminal at 115200 8N1):
 *    0  all switches off
 *    a  string A only   (B forced off first)
 *    b  string B only   (A forced off first)
 *    l  toggle load-disconnect switch
 */

#ifndef BENCH_DEBUG_H
#define BENCH_DEBUG_H

#include "main.h"

/** Poll USART2 for one command character and act on it.
 *  Non-blocking except for the BENCH_BBM_MS dead time on a string change. */
void BenchDebug_Poll(UART_HandleTypeDef *huart, uint8_t fault_latched);

#endif /* BENCH_DEBUG_H */
