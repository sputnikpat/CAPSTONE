/*
 * reconfig_cfg.h — tuning values for the string reconfiguration module
 *
 *  M1 Step 6. Every value carries a label:
 *    [DATASHEET]  a file in this project, section/table given
 *    [CALCULATED] math shown here or in STEP6_NOTES.md
 *    [DESIGN]     our choice, reason given
 *    [ASSUMPTION] must be confirmed on the bench (test id given)
 *
 *  Loop period is 20 ms (main.c LOOP_PERIOD_MS). Sample counts below
 *  are converted to time with that period.
 */

#ifndef RECONFIG_CFG_H
#define RECONFIG_CFG_H

#include "bms_safety_cfg.h"   /* SAFETY_VBUS_SEES_OPEN_STRING, sign macros */

/* ---------------- Parallel operation ----------------
 * Paralleling needs a TRUSTWORTHY dV between the two strings. That is
 * only true if each INA260 VBUS reads its own string while that string's
 * switch is open (Step 4 test T4). With bus-side VBUS an open string's
 * sensor reads the bus, dV reads ~0 V whatever the real dV is, and the
 * gate would pass falsely (M0: 218.8 A uncontrolled connect [SIMULATED]).
 *
 * So parallel is compiled in ONLY when the safety config says T4 passed.
 * There is deliberately no separate switch to force it on.     [DESIGN] */
#define RCF_PARALLEL_ENABLED        (SAFETY_VBUS_SEES_OPEN_STRING != 0)

/* Single-string mode (parallel disabled): which string to start on.
 * Open-string voltages cannot be compared in that mode, so "start on the
 * higher string" is not available. B is the default because the demo is
 * "String B is cut off by a (simulated) fault".                [DESIGN]
 * 0 = String A, 1 = String B (BMS_STR_A / BMS_STR_B). */
#define RCF_SINGLE_START_STR        1u

/* ---------------- dV gate (the rule: never parallel unless dV < 0.1 V) -
 * Gate value 100 mV                                             [DESIGN]
 *
 * WHY TWO dV VALUES ARE CHECKED. String X is ON and carrying load I_X;
 * string Y is OFF. Both sensors read string-side terminal voltages
 * (guaranteed by RCF_PARALLEL_ENABLED). The voltage that will actually
 * drive the join current is V_Y - V_bus, and
 *      V_bus = V_X - I_X * R_path,   0 <= R_path <= RCF_R_PATH_MAX_MOHM
 * so the real driving dV lies between
 *      dV_lo = V_Y - V_X                       (R_path = 0)
 *      dV_hi = V_Y - V_X + I_X * R_PATH_MAX    (R_path = max)
 * It is linear in R_path, so |real dV| <= max(|dV_lo|, |dV_hi|).
 * Requiring BOTH below 100 mV bounds the real dV below 100 mV without
 * knowing the exact path resistance.                          [CALCULATED]
 *
 * R_PATH_MAX = 50 mOhm:
 *   2 x 20 mOhm RDS(on) max, one pair    [DATASHEET IRF4905 p.2, VGS=-10 V]
 *   + 2 mOhm INA260 shunt (only if it sits between sense point and bus)
 *   + 8 mOhm margin for wiring / lower VGS at a low string    [ASSUMPTION]
 *   NOTE: RDS(on) max is specified at VGS = -10 V. A string near 7.5 V
 *   gives VGS ~ -7 V, so RDS(on) is higher there. The margin is a guess;
 *   S6-T4 measures the real drop. */
#define RCF_DV_GATE_MV              100
#define RCF_R_PATH_MAX_MOHM          50
#define RCF_DV_GATE_SAMPLES           5u   /* 5 x 20 ms = 100 ms in a row  */

/* Load must be steady while dV is judged, otherwise a motor transient
 * between the last sample and the close invalidates the measurement.
 * Max change of I_X between consecutive samples.             [ASSUMPTION]
 * S6-T5 checks it does not block joining forever with motors running. */
#define RCF_DI_STABLE_MA            200

/* ---------------- Readback thresholds ----------------
 * I_MIN: current that proves a string is conducting / proves it is open.
 *   Must be above INA260 offset noise and above the off-state reading
 *   from S5-T1, and below the smallest real load.          [ASSUMPTION]
 * I_VERIFY_TOTAL: a "this string did NOT conduct" verdict is only
 *   allowed when the total load is at least this. Below it, a
 *   legitimately near-zero share is possible in parallel:
 *     I_Y = I_L/2 - dV/R_loop  (equal R)                  [CALCULATED]
 *     dV = 0.10 V, R_loop = 0.266 Ohm  -> I_Y = I_L/2 - 0.376 A
 *     I_L = 1.5 A -> I_Y = 0.37 A  > I_MIN (0.10 A)
 *   With a pessimistic R_loop = 0.080 Ohm the zero crossing moves to
 *   I_L = 2.5 A, so a false "open" verdict is still possible. Its
 *   response is opening that string, which is the safe direction.
 * I_PRE_OPEN: before a string can be called "confirmed open" in a swap,
 *   it must have been carrying at least this much just before it opened.
 *   Otherwise "0 A afterwards" proves nothing.                 [DESIGN] */
#define RCF_I_MIN_MA                100
#define RCF_I_VERIFY_TOTAL_MA      1500
#define RCF_I_PRE_OPEN_MA           300

/* ---------------- Timing ----------------
 * Cooldown: minimum time between ANY two topology changes, including a
 * safety-forced opening. Prevents oscillation.      [ASSUMPTION, brief]
 *
 * Pre-load hold: after a string is closed with the load switch open,
 * wait this long before closing the load switch. Safety needs
 * SAFETY_V_SETTLE_MS (100 ms) + 3 samples (60 ms) = 160 ms to latch OV/UV
 * on a freshly closed string [CALCULATED from bms_safety_cfg.h]. 200 ms
 * means a bad string is caught BEFORE it is put on the motors.
 *
 * Dead time: gate turn-off is the 10k pull-up charging BOTH gates of the
 * pair (one net, Gate_Q1 / Gate_Q5 / Gate_Q8 in the netlist):
 *   t ~ 2 x Qg x R / V = 2 x 180 nC x 10 kOhm / 10 V = 0.36 ms
 *   [Qg max 180 nC, DATASHEET IRF4905 p.2; upper bound, Qg is given at
 *    38 A / -44 V]
 * One loop (20 ms) is ~55x margin. The swap also waits for current
 * readback, which is the real break-before-make proof.   [CALCULATED]
 *
 * Verify windows:
 *   OPEN_CONFIRM: |I| < I_MIN for this many consecutive samples
 *   OPEN_TIMEOUT: give up and lock out. Safety's STUCK check
 *                 (100 ms settle + 3 x 20 ms) fires at ~160 ms, so 300 ms
 *                 lets safety act first.                   [CALCULATED]
 *   JOIN_VERIFY:  how long a freshly joined string may show no current
 *                 (under real load) before it is declared not conducting
 *   LOST_COND:    same, continuously, while paralleled            [DESIGN] */
#define RCF_COOLDOWN_MS             500u
#define RCF_PRE_LOAD_MS             200u
#define RCF_DEAD_TIME_MS             20u
#define RCF_OPEN_CONFIRM_SAMPLES      3u
#define RCF_OPEN_TIMEOUT_MS         300u
#define RCF_JOIN_VERIFY_MS          500u
#define RCF_LOST_COND_MS           1000u

#endif /* RECONFIG_CFG_H */
