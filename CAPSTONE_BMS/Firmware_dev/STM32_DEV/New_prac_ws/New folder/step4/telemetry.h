/*
 * telemetry.h — CSV telemetry over USART2 (ST-Link virtual COM port)
 *
 *  M1 Step 4. Mirrors exactly the snapshot the OLED shows, so bench logs
 *  and the screen can never disagree.
 *
 *  Line format (one header line at boot, then one row per call):
 *    t_ms,loop,a_ok,va_mV,ia_mA,b_ok,vb_mV,ib_mA,sw_a,sw_b,sw_l,topo,
 *    topo_conf,fault,fail_a,fail_b,rec1,rec3,oled_err,overruns,
 *    work_us_max,oled_us_max
 *
 *  Stale-data rule applies here too: when a string is not valid, its
 *  va/ia fields are EMPTY, not the last good value. Log analysis then
 *  cannot mistake a dead sensor for a flat line.
 *
 *  Blocking cost [CALCULATED]: ~110 chars x 10 bits = 1100 bits
 *  / 115200 baud = ~9.5 ms. Called at 5 Hz, on loop iterations that do
 *  NOT also send an OLED page, so the two never stack.
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "main.h"
#include "bms_status.h"

void Telemetry_Init(UART_HandleTypeDef *huart);  /* prints banner + header */
void Telemetry_Send(const BmsStatus *s);
void Telemetry_Print(const char *text);          /* free text, adds CRLF   */

#endif /* TELEMETRY_H */
