/*
 * telemetry.c — CSV telemetry over USART2
 *
 *  M1 Step 4. See telemetry.h for format and timing.
 */

#include "telemetry.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define TELEM_TX_TIMEOUT_MS   20u   /* > 9.5 ms worst line, never MAX_DELAY */

static UART_HandleTypeDef *tm_uart = NULL;
static char tm_buf[160];

static void tm_tx(const char *txt)
{
    if (tm_uart == NULL || txt == NULL) return;
    /* HAL wants a non-const uint8_t*; the buffer is not modified. */
    (void)HAL_UART_Transmit(tm_uart, (uint8_t *)txt, (uint16_t)strlen(txt),
                            TELEM_TX_TIMEOUT_MS);
    /* Telemetry failure is deliberately ignored: losing a log line must
       never affect the BMS. */
}

void Telemetry_Print(const char *text)
{
    tm_tx(text);
    tm_tx("\r\n");
}

void Telemetry_Init(UART_HandleTypeDef *huart)
{
    tm_uart = huart;
    Telemetry_Print("# RECONFIG BMS - M1 STEP 4 - sensing/display only");
    Telemetry_Print("t_ms,loop,a_ok,va_mV,ia_mA,b_ok,vb_mV,ib_mA,"
                    "sw_a,sw_b,sw_l,topo,topo_conf,fault,fail_a,fail_b,"
                    "rec1,rec3,oled_err,overruns,work_us_max,oled_us_max");
}

/* Bounded append: once the buffer is full, further appends are no-ops,
   so an unexpectedly long line truncates instead of overrunning. */
static size_t tm_len;
static void ap(const char *fmt, ...)
{
    if (tm_len >= sizeof(tm_buf) - 1u) return;
    va_list args;
    va_start(args, fmt);
    int w = vsnprintf(tm_buf + tm_len, sizeof(tm_buf) - tm_len, fmt, args);
    va_end(args);
    if (w < 0) return;
    tm_len += (size_t)w;
    if (tm_len > sizeof(tm_buf) - 1u) tm_len = sizeof(tm_buf) - 1u;
}

/* "<v>," when valid, just "," when not — no stale values in the log. */
static void ap_field(uint8_t valid, int32_t v)
{
    if (valid) ap("%ld,", (long)v); else ap(",");
}

void Telemetry_Send(const BmsStatus *s)
{
    if (tm_uart == NULL || s == NULL) return;

    const BmsStringSnap *a = &s->str[BMS_STR_A];
    const BmsStringSnap *b = &s->str[BMS_STR_B];

    tm_len = 0u;
    tm_buf[0] = '\0';

    ap("%lu,%lu,", (unsigned long)s->tick_ms, (unsigned long)s->loop_count);
    ap("%u,", (unsigned)a->valid);  ap_field(a->valid, a->v_mV);  ap_field(a->valid, a->i_mA);
    ap("%u,", (unsigned)b->valid);  ap_field(b->valid, b->v_mV);  ap_field(b->valid, b->i_mA);
    ap("%u,%u,%u,", (unsigned)s->sw_a, (unsigned)s->sw_b, (unsigned)s->sw_load);
    ap("%s,%u,", (s->topo_label != NULL) ? s->topo_label : "?",
                 (unsigned)s->topo_confirmed);
    ap("%08lX,", (unsigned long)s->fault_flags);
    ap("%lu,%lu,", (unsigned long)a->fail_count, (unsigned long)b->fail_count);
    ap("%lu,%lu,", (unsigned long)s->i2c1_recover, (unsigned long)s->i2c3_recover);
    ap("%lu,%lu,", (unsigned long)s->oled_err, (unsigned long)s->overruns);
    ap("%lu,%lu\r\n", (unsigned long)s->work_us_max, (unsigned long)s->oled_us_max);

    tm_tx(tm_buf);
}
