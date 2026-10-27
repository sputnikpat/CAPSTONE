/*
 * reconfig.c — string-level reconfiguration state machine
 *
 *  M1 Step 6. API and guarantees: reconfig.h. Tuning values and their
 *  sources: reconfig_cfg.h. Full state diagram: STEP6_NOTES.md §3.
 *
 *  STATES
 *
 *   WAIT ──ready──► CONNECT(X) ──200 ms──► SINGLE(X) ──dV gate, cooldown──► PARALLEL
 *    │              string X on,           X + load on     (RCF_PARALLEL_   X + Y + load
 *    │              load OFF                 │              ENABLED only)    │
 *    │                 │ safety opened X      │ safety opened X               │ safety opened Y
 *    │                 ▼                      ▼                               ▼
 *    │             SWAP_OPEN(X) ◄─────────────┘                          ISOLATE(Y)
 *    │             confirm |I_X| ~ 0,                                    confirm |I_Y| ~ 0,
 *    │             then load OFF                                         X keeps the load
 *    │                 │                                                     │
 *    │                 ▼                                                     ▼
 *    │             SWAP_WAIT ──cooldown + dead time──► CONNECT(Y)        SINGLE(X)
 *    │
 *    └── any unexplained state, readback failure, nothing left ──► LOCKOUT (until reset)
 *
 *  HOW A STEP WORKS
 *    1. Global checks: safety global fault / no string left -> LOCKOUT.
 *    2. Reconcile: compare what we commanded with the GPIO output
 *       register. A string that safety opened is a FACT we follow. Any
 *       other difference means some other code moved a switch -> LOCKOUT.
 *    3. Run the current state.
 *    4. Bookkeeping: string set changed? -> event, restart cooldown.
 *       Load lost power? -> event (main.c stops the motors).
 *
 *  TIMING NOTE. The snapshot passed in was sampled at the START of this
 *  loop, before Safety_Step() acted on it. So in the cycle where safety
 *  opens a string, the snapshot still shows that string's pre-trip
 *  current. That sample is used as "current just before opening" and is
 *  NOT counted towards open confirmation.
 */

#include "reconfig.h"
#include "reconfig_cfg.h"
#include "bms_safety.h"
#include "bms_switch.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ------------------------------------------------------------------------ */

typedef enum {
    RS_WAIT = 0,     /* all off, waiting for Safety_Ready()             */
    RS_CONNECT,      /* one string closed, load OFF, pre-load hold      */
    RS_SINGLE,       /* one string + load                               */
    RS_PARALLEL,     /* both strings + load                             */
    RS_ISOLATE,      /* parallel lost one string; confirming it is open */
    RS_SWAP_OPEN,    /* only string opened by safety; confirming open   */
    RS_SWAP_WAIT,    /* all strings + load off; waiting to close other  */
    RS_LOCKOUT       /* all off until MCU reset                         */
} RcfState;

static const SwitchId sw_of[BMS_NUM_STR]  = { SW_STRING_A, SW_STRING_B };
static const uint8_t  blk_of[BMS_NUM_STR] = { SBLK_STR_A, SBLK_STR_B };
static const int32_t  dsg_sign[BMS_NUM_STR] = { SAFETY_DSG_SIGN_A,
                                                SAFETY_DSG_SIGN_B };
static const char     name_of[BMS_NUM_STR] = { 'A', 'B' };

static RcfState state;
static uint32_t t_state;           /* tick when the current state was entered */
static uint32_t t_change;          /* tick of the last string-set change      */
static uint8_t  any_change;        /* 0 until the first change (no cooldown)  */
static uint8_t  cmd[SW_COUNT];     /* what WE commanded                       */
static uint8_t  x;                 /* active / surviving / old string         */
static uint8_t  failed_open[BMS_NUM_STR]; /* did not conduct when it should   */

static uint8_t  gate_cnt;          /* consecutive samples passing the dV gate */
#if RCF_PARALLEL_ENABLED
static int32_t  prev_i_x;          /* previous I_X sample (load stability)    */
#endif
static uint8_t  prev_i_valid;
static uint8_t  open_cnt;          /* consecutive |I| < I_MIN samples         */
static int32_t  pre_open_i;        /* I of the opened string just before trip */
static int32_t  pre_total_i;       /* total I just before a parallel trip     */

static uint8_t  joined;            /* string closed by the last join          */
static uint8_t  join_verified;
static uint8_t  low_run[BMS_NUM_STR];
static uint32_t t_low[BMS_NUM_STR];/* start of a low-current run under load   */

static uint8_t  confirmed;
static uint8_t  prev_mask;         /* string set at the end of the last step  */
static uint8_t  prev_powered;
static uint8_t  events;

/* Small message queue so two events in one cycle are not lost. */
#define MSG_Q   3u
#define MSG_LEN 96u
static char     msg_q[MSG_Q][MSG_LEN];
static uint8_t  msg_head, msg_count;

/* ------------------------------------------------------------------------ */

static int32_t iabs(int32_t v) { return (v < 0) ? -v : v; }

static int32_t i_dsg(const BmsStringSnap *snap, uint8_t s)
{
    return dsg_sign[s] * snap[s].i_mA;          /* + = discharging */
}

static void post(const char *fmt, ...);

static void enter(RcfState s, uint32_t now)
{
    state    = s;
    t_state  = now;
    open_cnt = 0u;
    gate_cnt = 0u;
}

static uint8_t str_mask(void)
{
    return (uint8_t)((cmd[SW_STRING_A] ? 1u : 0u) | (cmd[SW_STRING_B] ? 2u : 0u));
}

static uint8_t available(uint8_t s)
{
    return (uint8_t)(((Safety_BlockMask() & blk_of[s]) == 0u) && !failed_open[s]);
}

static uint8_t cooldown_over(uint32_t now)
{
    return (uint8_t)(!any_change || (uint32_t)(now - t_change) >= RCF_COOLDOWN_MS);
}

/* The ONLY place in the firmware that turns a switch ON (G3). */
static uint8_t close_sw(SwitchId id)
{
    uint8_t m = Safety_BlockMask();
    if (id == SW_STRING_A && (m & SBLK_STR_A)) return 0u;
    if (id == SW_STRING_B && (m & SBLK_STR_B)) return 0u;
    if (id == SW_LOAD     && (m & SBLK_LOAD))  return 0u;
    Switch_Set(id, SW_ON);
    cmd[id] = 1u;
    return 1u;
}

static void open_sw(SwitchId id)
{
    Switch_Set(id, SW_OFF);
    cmd[id] = 0u;
}

static void lockout(uint32_t now, const char *why)
{
    Switch_AllOff();
    for (uint8_t k = 0; k < SW_COUNT; k++) cmd[k] = 0u;
    if (state != RS_LOCKOUT)
    {
        events |= RCF_EVT_LOCKOUT;
        post("# RCF LOCKOUT: %s (RESET to clear)", why);
    }
    enter(RS_LOCKOUT, now);
}

#if RCF_PARALLEL_ENABLED
/* dV gate, one sample (G1). x = string carrying the load, y = candidate.
 * Both bounds of the real driving dV must be inside the gate; see cfg. */
static uint8_t dv_gate_sample(const BmsStringSnap *snap, uint8_t sx, uint8_t sy,
                              int32_t *lo_out, int32_t *hi_out)
{
    int32_t lo = snap[sy].v_mV - snap[sx].v_mV;
    int32_t hi = lo + (i_dsg(snap, sx) * RCF_R_PATH_MAX_MOHM) / 1000;  /* mA*mOhm = uV */
    if (lo_out) *lo_out = lo;
    if (hi_out) *hi_out = hi;
    return (uint8_t)(iabs(lo) < RCF_DV_GATE_MV && iabs(hi) < RCF_DV_GATE_MV);
}
#endif

/* ------------------------------------------------------------------------ */

void Reconfig_Init(uint32_t now)
{
    Switch_AllOff();
    for (uint8_t k = 0; k < SW_COUNT; k++) cmd[k] = 0u;
    for (uint8_t s = 0; s < BMS_NUM_STR; s++)
    {
        failed_open[s] = 0u;
        low_run[s]     = 0u;
        t_low[s]       = now;
    }
    x = 0u;
    any_change = 0u;
    t_change = now;
    prev_i_valid = 0u;
    joined = 0u;
    join_verified = 0u;
    confirmed = 0u;
    prev_mask = 0u;
    prev_powered = 0u;
    msg_head = 0u;
    msg_count = 0u;
    enter(RS_WAIT, now);
#if RCF_PARALLEL_ENABLED
    post("# RCF: mode PARALLEL (VBUS string-side, T4 passed): start on higher string");
#else
    post("# RCF: mode SINGLE-ONLY (T4 not set): start on %c, no paralleling",
         name_of[RCF_SINGLE_START_STR]);
#endif
}

/* ---- state handlers ---- */

static void st_wait(const BmsStringSnap *snap, uint32_t now)
{
    if (!Safety_Ready()) return;

    uint8_t a = available(BMS_STR_A), b = available(BMS_STR_B);
    uint8_t pick;

    if (!a && !b) { lockout(now, "no string available at start"); return; }

#if RCF_PARALLEL_ENABLED
    /* Start on the HIGHER string. With no precharge the only way to reach
       dV < 0.1 V is for the load to pull the higher string down to the
       idle one. Starting low would make dV grow and never allow a join.
       Valid here because parallel builds require string-side VBUS. */
    if (a && b) pick = (uint8_t)((snap[BMS_STR_A].v_mV >= snap[BMS_STR_B].v_mV) ? BMS_STR_A : BMS_STR_B);
    else        pick = (uint8_t)(a ? BMS_STR_A : BMS_STR_B);
#else
    /* Open-string voltages are not trustworthy in this build: fixed start. */
    (void)snap;
    pick = RCF_SINGLE_START_STR;
    if (!available(pick)) pick = (uint8_t)(1u - pick);
#endif

    if (!close_sw(sw_of[pick])) { lockout(now, "start string blocked"); return; }
    x = pick;
    post("# RCF: CONNECT %c (load off, %u ms hold)", name_of[x], (unsigned)RCF_PRE_LOAD_MS);
    enter(RS_CONNECT, now);
}

static void st_connect(uint32_t now)
{
    /* Hold with load OFF so safety can judge OV/UV on the new string
       before it is put on the motors (cfg: RCF_PRE_LOAD_MS). */
    if ((uint32_t)(now - t_state) < RCF_PRE_LOAD_MS) return;

    if (!close_sw(SW_LOAD)) { lockout(now, "load switch blocked"); return; }
    post("# RCF: LOAD ON via %c", name_of[x]);
    prev_i_valid = 0u;
    enter(RS_SINGLE, now);
}

static void st_single(const BmsStringSnap *snap, uint32_t now)
{
#if RCF_PARALLEL_ENABLED
    uint8_t y = (uint8_t)(1u - x);
    int32_t ix = i_dsg(snap, x);
    uint8_t stable = (uint8_t)(prev_i_valid && iabs(ix - prev_i_x) < RCF_DI_STABLE_MA);
    prev_i_x = ix;
    prev_i_valid = 1u;

    int32_t lo, hi;
    if (available(y) && stable && dv_gate_sample(snap, x, y, &lo, &hi))
    {
        if (gate_cnt < 255u) gate_cnt++;
    }
    else
    {
        gate_cnt = 0u;
    }

    if (gate_cnt >= RCF_DV_GATE_SAMPLES && cooldown_over(now))
    {
        (void)dv_gate_sample(snap, x, y, &lo, &hi);
        if (!close_sw(sw_of[y])) { gate_cnt = 0u; return; }
        joined = y;
        join_verified = 0u;
        low_run[BMS_STR_A] = low_run[BMS_STR_B] = 0u;
        post("# RCF: JOIN %c onto %c  dV=%ld..%ld mV  I_%c=%ld mA",
             name_of[y], name_of[x], (long)lo, (long)hi, name_of[x], (long)ix);
        enter(RS_PARALLEL, now);
    }
#else
    (void)snap; (void)now;
#endif
}

/* One string declared not conducting while paralleled: open it (an
   intermittent connection must not be allowed to re-join with an
   unchecked dV later) and continue on the other one. */
static void drop_nonconducting(uint8_t s, uint32_t now, int32_t i_tot)
{
    open_sw(sw_of[s]);
    failed_open[s] = 1u;
    x = (uint8_t)(1u - s);
    post("# RCF: %c NOT CONDUCTING (I_tot=%ld mA) -> opened, run on %c",
         name_of[s], (long)i_tot, name_of[x]);
    prev_i_valid = 0u;
    enter(RS_SINGLE, now);
}

static void st_parallel(const BmsStringSnap *snap, uint32_t now)
{
    int32_t i_tot = i_dsg(snap, BMS_STR_A) + i_dsg(snap, BMS_STR_B);

    for (uint8_t s = 0; s < BMS_NUM_STR; s++)
    {
        int32_t is = iabs(snap[s].i_mA);

        if (s == joined && !join_verified && is >= RCF_I_MIN_MA)
        {
            join_verified = 1u;
            post("# RCF: JOIN VERIFIED  I_%c=%ld mA  I_%c=%ld mA",
                 name_of[BMS_STR_A], (long)snap[BMS_STR_A].i_mA,
                 name_of[BMS_STR_B], (long)snap[BMS_STR_B].i_mA);
        }

        /* "Not conducting" needs a real load, otherwise a near-zero share
           is legitimate (cfg: RCF_I_VERIFY_TOTAL_MA). */
        if (is < RCF_I_MIN_MA && i_tot >= RCF_I_VERIFY_TOTAL_MA)
        {
            if (!low_run[s]) { low_run[s] = 1u; t_low[s] = now; }
            uint32_t lim = (s == joined && !join_verified) ? RCF_JOIN_VERIFY_MS
                                                           : RCF_LOST_COND_MS;
            if ((uint32_t)(now - t_low[s]) >= lim)
            {
                drop_nonconducting(s, now, i_tot);
                return;
            }
        }
        else
        {
            low_run[s] = 0u;
        }
    }
}

static void st_isolate(const BmsStringSnap *snap, uint32_t now)
{
    uint8_t y = (uint8_t)(1u - x);   /* the string safety opened */

    if (iabs(snap[y].i_mA) < RCF_I_MIN_MA) { if (open_cnt < 255u) open_cnt++; }
    else open_cnt = 0u;

    if (open_cnt >= RCF_OPEN_CONFIRM_SAMPLES)
    {
        int32_t ix = i_dsg(snap, x);
        if (pre_total_i >= RCF_I_VERIFY_TOTAL_MA && ix < RCF_I_MIN_MA)
        {
            lockout(now, "survivor not carrying the load");
            return;
        }
        post("# RCF: ISOLATED %c, open confirmed %lu ms after trip, I_%c=%ld mA (pre-trip total %ld)",
             name_of[y], (unsigned long)(now - t_state), name_of[x], (long)ix,
             (long)pre_total_i);
        prev_i_valid = 0u;
        enter(RS_SINGLE, now);
        return;
    }
    if ((uint32_t)(now - t_state) >= RCF_OPEN_TIMEOUT_MS)
        lockout(now, "opened string still carries current");
}

static void st_swap_open(const BmsStringSnap *snap, uint32_t now)
{
    uint8_t y = (uint8_t)(1u - x);

    if (!available(y)) { lockout(now, "no other string to swap to"); return; }

    if (iabs(snap[x].i_mA) < RCF_I_MIN_MA) { if (open_cnt < 255u) open_cnt++; }
    else open_cnt = 0u;

    if (open_cnt >= RCF_OPEN_CONFIRM_SAMPLES)
    {
        /* "0 A now" proves the switch opened only if current was flowing
           before. Otherwise, closing Y could parallel it with a stuck X.
           Fallback (parallel builds only): if dV is inside the gate,
           even a stuck X would make a legal join. */
        uint8_t proven = (uint8_t)(pre_open_i >= RCF_I_PRE_OPEN_MA);
#if RCF_PARALLEL_ENABLED
        if (!proven && dv_gate_sample(snap, x, y, NULL, NULL)) proven = 2u;
#endif
        if (!proven)
        {
            lockout(now, "cannot prove old string open (no load before trip)");
            return;
        }
        open_sw(SW_LOAD);   /* isolate the motor driver's input capacitance */
        post("# RCF: %c OPEN confirmed (%s), load off, swap to %c in >= %u ms",
             name_of[x], (proven == 1u) ? "current fell" : "dV gate",
             name_of[y], (unsigned)RCF_COOLDOWN_MS);
        enter(RS_SWAP_WAIT, now);
        return;
    }
    if ((uint32_t)(now - t_state) >= RCF_OPEN_TIMEOUT_MS)
        lockout(now, "opened string still carries current");
}

static void st_swap_wait(uint32_t now)
{
    uint8_t y = (uint8_t)(1u - x);
    if ((uint32_t)(now - t_state) < RCF_DEAD_TIME_MS || !cooldown_over(now)) return;
    if (!available(y) || !close_sw(sw_of[y])) { lockout(now, "swap target unavailable"); return; }
    x = y;
    post("# RCF: CONNECT %c (swap)", name_of[x]);
    enter(RS_CONNECT, now);
}

/* ------------------------------------------------------------------------ */

uint8_t Reconfig_Step(const BmsStringSnap snap[BMS_NUM_STR], uint32_t now)
{
    events = 0u;

    if (state == RS_LOCKOUT)
    {
        Switch_AllOff();        /* re-asserted every cycle */
        goto bookkeeping;
    }

    /* 1. Global: an unsensed or fully faulted pack runs nothing. */
    if (Safety_AllStringsDown())
    {
        lockout(now, "safety: no string may run");
        goto bookkeeping;
    }

    /* 2. Reconcile commanded vs. actual output register. */
    {
        uint8_t safety_opened = 0u;
        for (uint8_t k = 0; k < SW_COUNT; k++)
        {
            uint8_t actual = (Switch_Get((SwitchId)k) == SW_ON) ? 1u : 0u;
            if (actual == cmd[k]) continue;

            if (actual && !cmd[k]) { lockout(now, "switch closed by other code"); goto bookkeeping; }

            /* actual OFF, we wanted ON */
            if (k == SW_LOAD) { lockout(now, "load opened unexpectedly"); goto bookkeeping; }
            uint8_t s = (uint8_t)((k == SW_STRING_A) ? BMS_STR_A : BMS_STR_B);
            if (!(Safety_BlockMask() & blk_of[s]))
            {
                lockout(now, "string opened unexpectedly (manual 0?)");
                goto bookkeeping;
            }
            cmd[k] = 0u;
            safety_opened |= (uint8_t)(1u << s);
        }

        if (safety_opened)
        {
            uint8_t s = (uint8_t)((safety_opened & 1u) ? BMS_STR_A : BMS_STR_B);

            switch (state)
            {
            case RS_PARALLEL:
                /* THE DEMO PATH. The other string is already conducting:
                   no switch has to close, the load is never interrupted. */
                x = (uint8_t)(1u - s);
                pre_total_i = i_dsg(snap, BMS_STR_A) + i_dsg(snap, BMS_STR_B);
                post("# RCF: SAFETY OPENED %c while parallel -> %c keeps the load",
                     name_of[s], name_of[x]);
                enter(RS_ISOLATE, now);
                break;

            case RS_CONNECT:
            case RS_SINGLE:
                if (s == x)
                {
                    pre_open_i = (state == RS_SINGLE) ? i_dsg(snap, x) : 0;
                    post("# RCF: SAFETY OPENED %c (only string, I was %ld mA) -> swap",
                         name_of[s], (long)pre_open_i);
                    enter(RS_SWAP_OPEN, now);
                }
                /* else: the idle string faulted. It is now unavailable;
                   nothing to switch. */
                break;

            case RS_ISOLATE:
            case RS_SWAP_OPEN:
            default:
                /* Survivor / new candidate faulted too. Step 1 catches
                   "both down" next cycle; nothing to close here. */
                break;
            }
        }
    }

    /* 3. State */
    switch (state)
    {
    case RS_WAIT:      st_wait(snap, now);      break;
    case RS_CONNECT:   st_connect(now);         break;
    case RS_SINGLE:    st_single(snap, now);    break;
    case RS_PARALLEL:  st_parallel(snap, now);  break;
    case RS_ISOLATE:   st_isolate(snap, now);   break;
    case RS_SWAP_OPEN: st_swap_open(snap, now); break;
    case RS_SWAP_WAIT: st_swap_wait(now);       break;
    default:           lockout(now, "bad state"); break;
    }

bookkeeping:
    {
        uint8_t m = str_mask();
        if (m != prev_mask)
        {
            t_change   = now;          /* restarts the cooldown (G4) */
            any_change = 1u;
            events    |= RCF_EVT_CHANGED;
            prev_mask  = m;
        }
        /* "Load held" includes SWAP_OPEN on purpose. While the old string
           is being proven open, the motors must stay commanded: if the
           pair is really open they have no power anyway, and if it is
           stuck closed the load keeps drawing current, so the open is
           never "confirmed" and safety's STUCK check trips instead.
           Stopping the motors first would make a stuck pair read ~0 A
           and look open (found in host sim, scenario S4). */
        uint8_t held = (uint8_t)(cmd[SW_LOAD] && (m || state == RS_SWAP_OPEN));
        if (prev_powered && !held) events |= RCF_EVT_LOAD_LOST;
        prev_powered = held;
        uint8_t powered = (uint8_t)(cmd[SW_LOAD] && m);

        /* Readback agreement for display (topo_conf column). */
        uint8_t on_a = (uint8_t)(iabs(snap[BMS_STR_A].i_mA) >= RCF_I_MIN_MA);
        uint8_t on_b = (uint8_t)(iabs(snap[BMS_STR_B].i_mA) >= RCF_I_MIN_MA);
        uint8_t want_a = (uint8_t)(powered && (m & 1u));
        uint8_t want_b = (uint8_t)(powered && (m & 2u));
        confirmed = (uint8_t)(snap[BMS_STR_A].valid && snap[BMS_STR_B].valid
                              && on_a == want_a && on_b == want_b);
    }
    return events;
}

/* ------------------------------------------------------------------------ */

uint8_t Reconfig_MotorsAllowed(void)
{
    /* SWAP_OPEN: keep the load applied while proving the old string open. */
    if (state == RS_SWAP_OPEN) return cmd[SW_LOAD];
    return (uint8_t)((state == RS_SINGLE || state == RS_PARALLEL || state == RS_ISOLATE)
                     && cmd[SW_LOAD] && str_mask());
}

uint8_t Reconfig_TopoConfirmed(void) { return confirmed; }

const char *Reconfig_TopoLabel(void)
{
    static char lbl[8];
    switch (state)
    {
    case RS_WAIT:      return "WAIT";
    case RS_LOCKOUT:   return "LOCK";
    case RS_SWAP_OPEN:
    case RS_SWAP_WAIT: return "SWAP";
    case RS_PARALLEL:  return "A+B";
    case RS_CONNECT:   snprintf(lbl, sizeof(lbl), "%c-NOLD", name_of[x]); return lbl;
    default:           snprintf(lbl, sizeof(lbl), "%c", name_of[x]);       return lbl;
    }
}

static void post(const char *fmt, ...)
{
    uint8_t slot;
    if (msg_count < MSG_Q) { slot = (uint8_t)((msg_head + msg_count) % MSG_Q); msg_count++; }
    else { slot = msg_head; msg_head = (uint8_t)((msg_head + 1u) % MSG_Q); } /* drop oldest */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_q[slot], MSG_LEN, fmt, ap);
    va_end(ap);
}

const char *Reconfig_TakeMessage(void)
{
    static char out[MSG_LEN];
    if (msg_count == 0u) return NULL;
    memcpy(out, msg_q[msg_head], MSG_LEN);
    msg_head = (uint8_t)((msg_head + 1u) % MSG_Q);
    msg_count--;
    return out;
}
