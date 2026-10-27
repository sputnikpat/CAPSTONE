/*
 * bms_safety.c — string-level SOA protection
 *
 *  M1 Step 5. Policy and API: see bms_safety.h. Thresholds and their
 *  sources: see bms_safety_cfg.h.
 *
 *  ORDER INSIDE Safety_Step(), per string:
 *    1. track ON/OFF command edges (for settle timers)
 *    2. sensor valid?  no -> latch SENSOR (global), skip the rest
 *    3. readiness counter
 *    4. evaluate OV, UV, OC-dsg, OC-chg, STUCK -> debounce -> latch
 *  then, for the whole pack:
 *    5. ready flag
 *    6. ENFORCE: global fault -> all off; else open each faulted string.
 *       Enforcement runs EVERY call, not only on the edge, so a switch
 *       that something else turned back on is forced off again within
 *       one loop.
 */

#include "bms_safety.h"
#include "bms_safety_cfg.h"
#include "bms_switch.h"
#include <stdio.h>

/* Debounce slots per string */
enum { CHK_OV = 0, CHK_UV, CHK_OCD, CHK_OCC, CHK_STUCK, CHK_COUNT };

static const uint32_t chk_bit[CHK_COUNT] = {
    [CHK_OV]    = SFLT_OV,
    [CHK_UV]    = SFLT_UV,
    [CHK_OCD]   = SFLT_OCD,
    [CHK_OCC]   = SFLT_OCC,
    [CHK_STUCK] = SFLT_STUCK,
};

static const SwitchId sw_of[BMS_NUM_STR]    = { SW_STRING_A, SW_STRING_B };
static const int32_t  dsg_sign[BMS_NUM_STR] = { SAFETY_DSG_SIGN_A,
                                                SAFETY_DSG_SIGN_B };

static uint32_t flags;                          /* latched fault bits    */
static uint8_t  deb[BMS_NUM_STR][CHK_COUNT];    /* consecutive-bad count */
static uint8_t  ready_cnt[BMS_NUM_STR];
static uint8_t  ready;
static uint8_t  prev_on[BMS_NUM_STR];
static uint32_t t_on[BMS_NUM_STR];              /* tick of last OFF->ON  */
static uint32_t t_off[BMS_NUM_STR];             /* tick of last ON->OFF  */
static uint8_t  injected[BMS_NUM_STR];
static char     label[24];

/* ------------------------------------------------------------------------ */

static uint32_t str_bits(uint8_t s)
{
    return (flags >> SFLT_SHIFT(s)) & 0xFFu;
}

static uint8_t any_global(void)
{
    for (uint8_t s = 0; s < BMS_NUM_STR; s++)
        if (str_bits(s) & SFLT_GLOBAL_MASK) return 1u;
    return 0u;
}

static int32_t iabs(int32_t x) { return (x < 0) ? -x : x; }

/* One debounce step. Returns 1 when the check has been bad for N
   consecutive samples. Any good sample resets the count to 0. */
static uint8_t debounce(uint8_t *cnt, uint8_t bad)
{
    if (!bad) { *cnt = 0u; return 0u; }
    if (*cnt < 255u) (*cnt)++;
    return (*cnt >= SAFETY_DEBOUNCE_N) ? 1u : 0u;
}

/* ------------------------------------------------------------------------ */

void Safety_Init(uint32_t now_ms)
{
    flags = 0u;
    ready = 0u;
    for (uint8_t s = 0; s < BMS_NUM_STR; s++)
    {
        for (uint8_t k = 0; k < CHK_COUNT; k++) deb[s][k] = 0u;
        ready_cnt[s] = 0u;
        injected[s]  = 0u;
        /* Read the real commanded state rather than assuming OFF, so the
           settle timers are right even if Init is called late. */
        prev_on[s] = (Switch_Get(sw_of[s]) == SW_ON) ? 1u : 0u;
        t_on[s]    = now_ms;
        t_off[s]   = now_ms;
    }
}

void Safety_LatchSensorAtBoot(uint8_t s)
{
    if (s >= BMS_NUM_STR) return;
    flags |= SFLT_STR(s, SFLT_SENSOR);
    Switch_AllOff();
}

uint8_t Safety_Step(const BmsStringSnap snap[BMS_NUM_STR], uint32_t now)
{
    const uint32_t before = flags;

    for (uint8_t s = 0; s < BMS_NUM_STR; s++)
    {
        /* 1. Command edges. Uses the COMMANDED state (ODR). */
        uint8_t on = (Switch_Get(sw_of[s]) == SW_ON) ? 1u : 0u;
        if ( on && !prev_on[s]) t_on[s]  = now;
        if (!on &&  prev_on[s]) t_off[s] = now;
        prev_on[s] = on;

        /* 2. No data = unsafe. Immediate (the I2C layer has already spent
              its retries and a bus recovery before reporting invalid). */
        if (!snap[s].valid)
        {
            flags |= SFLT_STR(s, SFLT_SENSOR);
            for (uint8_t k = 0; k < CHK_COUNT; k++) deb[s][k] = 0u;
            ready_cnt[s] = 0u;
            continue;
        }

        /* 3. Readiness */
        if (ready_cnt[s] < SAFETY_READY_SAMPLES) ready_cnt[s]++;

        /* 4. Checks */
        const int32_t v_mV   = snap[s].v_mV;
        const int32_t i_real = snap[s].i_mA;

        /* Injection replaces the current SAFETY sees, nothing else. */
        int32_t i_eval = i_real;
#if BMS_FAULT_INJECTION
        if (injected[s]) i_eval = dsg_sign[s] * SAFETY_INJECT_MA;
#endif
        /* Normalise so positive = discharge for both strings. */
        const int32_t i_dsg = dsg_sign[s] * i_eval;

        /* Is VBUS this string's own voltage right now? See cfg. */
#if SAFETY_VBUS_SEES_OPEN_STRING
        const uint8_t v_trust = 1u;
#else
        const uint8_t v_trust = (on && (uint32_t)(now - t_on[s]) >= SAFETY_V_SETTLE_MS)
                                ? 1u : 0u;
#endif

        uint8_t bad[CHK_COUNT];
        bad[CHK_OV]    = (v_trust && v_mV > SAFETY_OV_MV) ? 1u : 0u;
        bad[CHK_UV]    = (v_trust && v_mV < SAFETY_UV_MV) ? 1u : 0u;
        bad[CHK_OCD]   = ( i_dsg > SAFETY_OC_DSG_MA) ? 1u : 0u;
        bad[CHK_OCC]   = (-i_dsg > SAFETY_OC_CHG_MA) ? 1u : 0u;
        /* Stuck: judged on the REAL current, never the injected one, and
           only once the OFF command has had time to take effect. */
        bad[CHK_STUCK] = (!on
                          && (uint32_t)(now - t_off[s]) >= SAFETY_STUCK_SETTLE_MS
                          && iabs(i_real) > SAFETY_STUCK_MA) ? 1u : 0u;

        for (uint8_t k = 0; k < CHK_COUNT; k++)
        {
            if (debounce(&deb[s][k], bad[k]))
            {
                flags |= SFLT_STR(s, chk_bit[k]);
#if BMS_FAULT_INJECTION
                if (injected[s] && (k == CHK_OCD || k == CHK_OCC))
                    flags |= SFLT_STR(s, SFLT_INJECTED);
#endif
            }
        }
    }

    /* 5. Ready once every sensor delivered N clean samples in a row. Once
          set it stays set; a later sensor loss is a latched global fault,
          which blocks everything anyway. */
    if (!ready)
    {
        uint8_t all = 1u;
        for (uint8_t s = 0; s < BMS_NUM_STR; s++)
            if (ready_cnt[s] < SAFETY_READY_SAMPLES) all = 0u;
        ready = all;
    }

    /* 6. Enforce, every call. */
    if (any_global())
    {
        Switch_AllOff();                    /* strings AND load switch */
    }
    else
    {
        for (uint8_t s = 0; s < BMS_NUM_STR; s++)
            if (str_bits(s)) Switch_Set(sw_of[s], SW_OFF);
    }

    return (flags != before) ? 1u : 0u;
}

/* ------------------------------------------------------------------------ */

uint32_t Safety_Flags(void) { return flags; }
uint8_t  Safety_Ready(void) { return ready; }

uint8_t Safety_AllStringsDown(void)
{
    if (any_global()) return 1u;
    for (uint8_t s = 0; s < BMS_NUM_STR; s++)
        if (str_bits(s) == 0u) return 0u;
    return 1u;
}

uint8_t Safety_BlockMask(void)
{
    if (!ready || any_global())
        return (uint8_t)(SBLK_STR_A | SBLK_STR_B | SBLK_LOAD);

    uint8_t m = 0u;
    if (str_bits(BMS_STR_A)) m |= SBLK_STR_A;
    if (str_bits(BMS_STR_B)) m |= SBLK_STR_B;
    return m;
}

void Safety_InjectOC(uint8_t s)
{
#if BMS_FAULT_INJECTION
    if (s < BMS_NUM_STR) injected[s] = 1u;
#else
    (void)s;
#endif
}

/* Highest-priority code for one string's bits. */
static const char *code_of(uint32_t b)
{
    if (b & SFLT_OCD) return "OCD";
    if (b & SFLT_OCC) return "OCC";
    if (b & SFLT_OV)  return "OV";
    if (b & SFLT_UV)  return "UV";
    return "--";
}

const char *Safety_Label(void)
{
    uint32_t a = str_bits(BMS_STR_A);
    uint32_t b = str_bits(BMS_STR_B);

    if (flags == 0u)
        return "NONE";

    if ((a | b) & SFLT_SENSOR)
    {
        snprintf(label, sizeof(label), "SENSOR %s LOST",
                 ((a & b) & SFLT_SENSOR) ? "A+B" : ((a & SFLT_SENSOR) ? "A" : "B"));
        return label;
    }
    if ((a | b) & SFLT_STUCK)
    {
        snprintf(label, sizeof(label), "STUCK %s",
                 ((a & b) & SFLT_STUCK) ? "A+B" : ((a & SFLT_STUCK) ? "A" : "B"));
        return label;
    }

    /* e.g. "A:-- B:OCD*"  ('*' = injected) — max 13 chars */
    snprintf(label, sizeof(label), "A:%s%s B:%s%s",
             code_of(a), (a & SFLT_INJECTED) ? "*" : "",
             code_of(b), (b & SFLT_INJECTED) ? "*" : "");
    return label;
}
