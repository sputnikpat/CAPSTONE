/*
 * bms_display.c — BMS screens for the 128x64 OLED
 *
 *  M1 Step 4.
 *
 *  Main screen layout (21 chars x 8 rows, one row per OLED page):
 *
 *    row 0   A 11.12V +0.85A OK      string A: V, I, sensor status
 *    row 1   B 11.09V +0.82A OK      string B
 *    row 2   DV  0.03V IT +1.67A     |VA-VB| and total current
 *    row 3   TOPO A+B  CMD           label + CMD (commanded) / CONF
 *    row 4   SW A1 B1 L1  MTR 0      raw GPIO command states
 *    row 5   FLT NONE                inverted when a fault is latched
 *    row 6   FA0 FB0 RC0/0 OE0       read fails, bus recovers, OLED errs
 *    row 7   LOOP 001234 00:12:34    heartbeat + uptime
 *
 *  The values above are a layout example, not measurements.
 *
 *  STALE DATA RULE: if a string's snapshot is not valid THIS cycle, its
 *  numbers are replaced with dashes and LOST. A frozen last-good value is
 *  never shown as if it were live. DV and IT are only computed when BOTH
 *  strings are valid.
 *
 *  >> WHAT "DV" MEANS DEPENDS ON WHERE EACH INA260's VBUS PIN SITS. If the
 *     sensor is on the bus side of its string switch, then with that switch
 *     OPEN it reads the common bus, not its own string. See Step 4 notes,
 *     bench test T4. Do not use this DV for anything but observation.
 */

#include "bms_display.h"
#include "oled.h"
#include <stdio.h>

/* ---------------------------------------------------------------------- */
/* Formatting (integer only — no float printf, no libm)                     */
/* ---------------------------------------------------------------------- */

/* Round to hundredths (10 mV or 10 mA), half away from zero.
 * e.g. 11119 mV -> 1112 (11.12 V);  -505 mA -> -51 (-0.51 A) */
static int32_t to_hundredths(int32_t milli)
{
    return (milli >= 0) ? (milli + 5) / 10 : -((-milli + 5) / 10);
}

void Display_FmtVolt(char *out, size_t n, int32_t mV)
{
    int32_t cv = to_hundredths(mV);
    if (cv < 0) cv = 0;               /* INA260 bus voltage is unsigned    */
    snprintf(out, n, "%ld.%02ld", (long)(cv / 100), (long)(cv % 100));
}

void Display_FmtAmp(char *out, size_t n, int32_t mA)
{
    /* Sign handled separately: the old formatter printed -0.50 A as 0.50
       because (int)-0.5 == 0 loses the sign. */
    int32_t ca  = to_hundredths(mA);
    char    sgn = (ca < 0) ? '-' : '+';
    if (ca < 0) ca = -ca;
    snprintf(out, n, "%c%ld.%02ld", sgn, (long)(ca / 100), (long)(ca % 100));
}

static uint32_t cap999(uint32_t v) { return (v > 999u) ? 999u : v; }

/* ---------------------------------------------------------------------- */
/* Screens                                                                  */
/* ---------------------------------------------------------------------- */

void Display_Boot(uint8_t sensor_a_ok, uint8_t sensor_b_ok)
{
    char line[40];

    OLED_Clear();
    OLED_PrintRow(1, "  RECONFIG BMS", 0);
    OLED_PrintRow(2, "  SENSOR PROBE", 0);
    snprintf(line, sizeof(line), "  INA A: %s", sensor_a_ok ? "OK" : "MISSING");
    OLED_PrintRow(4, line, 0);
    snprintf(line, sizeof(line), "  INA B: %s", sensor_b_ok ? "OK" : "MISSING");
    OLED_PrintRow(5, line, 0);
    OLED_PrintRow(6, "  SWITCHES: ALL OFF", 0);

    /* Border LAST: it ORs into rows 0 and 7 and overwrites columns 0/127.
       If any edge is missing or doubled, OLED_CONTROLLER / column offset
       is wrong — see oled.h. */
    OLED_DrawBorder();

    (void)OLED_FlushBlocking(500u);  /* 8 pages x ~12 ms = ~100 ms [CALC] */
}

static void render_string_row(uint8_t row, char tag, const BmsStringSnap *s)
{
    char line[40], v[12], i[12];

    if (s->valid)
    {
        Display_FmtVolt(v, sizeof(v), s->v_mV);
        Display_FmtAmp (i, sizeof(i), s->i_mA);
        snprintf(line, sizeof(line), "%c %5sV %6sA OK", tag, v, i);
    }
    else
    {
        snprintf(line, sizeof(line), "%c --.--V --.--A LOST", tag);
    }
    /* Invert a LOST string row so it is visible across a room. */
    OLED_PrintRow(row, line, (uint8_t)(s->valid ? 0u : 1u));
}

void Display_Render(const BmsStatus *s)
{
    char line[40], a[12], b[12];

    if (s == NULL) return;

    OLED_Clear();

    /* rows 0, 1 — strings */
    render_string_row(0, 'A', &s->str[BMS_STR_A]);
    render_string_row(1, 'B', &s->str[BMS_STR_B]);

    /* row 2 — derived values, only when both inputs are live */
    if (s->str[BMS_STR_A].valid && s->str[BMS_STR_B].valid)
    {
        int32_t dv = s->str[BMS_STR_A].v_mV - s->str[BMS_STR_B].v_mV;
        if (dv < 0) dv = -dv;
        Display_FmtVolt(a, sizeof(a), dv);
        Display_FmtAmp (b, sizeof(b),
                        s->str[BMS_STR_A].i_mA + s->str[BMS_STR_B].i_mA);
        snprintf(line, sizeof(line), "DV %5sV IT %6sA", a, b);
    }
    else
    {
        snprintf(line, sizeof(line), "DV --.--V IT --.--A");
    }
    OLED_PrintRow(2, line, 0);

    /* row 3 — topology label, and whether it is proven or just commanded */
    snprintf(line, sizeof(line), "TOPO %-6s %s",
             (s->topo_label != NULL) ? s->topo_label : "?",
             s->topo_confirmed ? "CONF" : "CMD");
    OLED_PrintRow(3, line, 0);

    /* row 4 — raw commanded switch states */
    snprintf(line, sizeof(line), "SW A%u B%u L%u  MTR %u",
             (unsigned)s->sw_a, (unsigned)s->sw_b, (unsigned)s->sw_load,
             (unsigned)s->motor_on);
    OLED_PrintRow(4, line, 0);

    /* row 5 — fault, inverted when anything is latched */
    snprintf(line, sizeof(line), "FLT %s",
             (s->fault_flags == 0u) ? "NONE"
             : ((s->fault_label != NULL) ? s->fault_label : "UNKNOWN"));
    OLED_PrintRow(5, line, (uint8_t)((s->fault_flags != 0u) ? 1u : 0u));

    /* row 6 — comms health. FA/FB = INA read failures, RC = bus recoveries
       I2C1/I2C3, OE = OLED errors. Capped at 999 for width. */
    snprintf(line, sizeof(line), "FA%lu FB%lu RC%lu/%lu OE%lu",
             (unsigned long)cap999(s->str[BMS_STR_A].fail_count),
             (unsigned long)cap999(s->str[BMS_STR_B].fail_count),
             (unsigned long)cap999(s->i2c1_recover),
             (unsigned long)cap999(s->i2c3_recover),
             (unsigned long)cap999(s->oled_err));
    OLED_PrintRow(6, line, 0);

    /* row 7 — heartbeat. If this row stops changing, the MCU is stuck and
       NOTHING else on the screen can be trusted. */
    uint32_t sec = s->tick_ms / 1000u;
    snprintf(line, sizeof(line), "LOOP %06lu %02lu:%02lu:%02lu",
             (unsigned long)(s->loop_count % 1000000u),
             (unsigned long)((sec / 3600u) % 100u),
             (unsigned long)((sec / 60u) % 60u),
             (unsigned long)(sec % 60u));
    OLED_PrintRow(7, line, 0);

    OLED_Commit();
}
