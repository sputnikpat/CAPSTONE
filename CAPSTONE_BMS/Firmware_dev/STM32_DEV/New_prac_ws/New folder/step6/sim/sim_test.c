/*
 * sim_test.c — HOST simulation of bms_safety.c + reconfig.c (unmodified
 * firmware sources) against a simple electrical plant model.
 *
 *  [SIMULATED] Everything this program prints is simulation. It checks
 *  decision logic and sequencing only: no HAL, no real INA260 timing, no
 *  I2C, no switching transients, no wiring inductance.
 *
 *  PLANT (per string): OCV source + R_int, a back-to-back pair R_SW, all
 *  meeting at BUS; load = current sink (L298N quiescent + motor current)
 *  behind the load switch. Faults: pair stuck closed, pair failed open,
 *  sensor dead. Sensor VBUS is string-side when the firmware is built
 *  with SAFETY_VBUS_SEES_OPEN_STRING=1, bus-side otherwise (worst case).
 *
 *  INVARIANTS checked on EVERY switch-on command (hook in sim_switch.c):
 *    G1/G2  closing a string while the other one physically conducts is
 *           allowed only if the TRUE driving dV (OCV_y - V_bus, from the
 *           plant, not from the sensors) is below 100 mV
 *    G3     never close a switch that Safety_BlockMask() blocks
 *    G4     a string close is >= 500 ms after the last string-set change
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bms_switch.h"
#include "bms_safety.h"
#include "bms_safety_cfg.h"
#include "reconfig.h"
#include "reconfig_cfg.h"

/* ---------------- plant ---------------- */
typedef struct { double ocv, rint; int stuck, fopen, dead; } Str;
static Str    P[2];
static double R_SW   = 0.030;   /* actual pair resistance in the plant (< 50 mOhm max) */
static double I_QUIES = 0.020;  /* L298N with motors stopped [ASSUMPTION] */
static double I_MOTOR = 1.5;
static int    motors;
static double accel = 1.0;      /* discharge speed-up for long scenarios  */
static const double K_OCV = 0.33e-3;  /* V per As per string, ~3.6 V / 3 Ah [CALCULATED] */

static uint32_t now;
static int verbose;
static uint32_t rng = 1;
static double noise(double a) { rng = rng * 1103515245u + 12345u; return ((double)((rng >> 8) & 0xFFFF) / 65535.0 * 2.0 - 1.0) * a; }

static int phys_on(int s) { return P[s].stuck || (sim_odr[s] && !P[s].fopen); }

static double I_s[2], Vbus;
static void solve(void)
{
    double Il = sim_odr[SW_LOAD] ? (I_QUIES + (motors ? I_MOTOR : 0.0)) : 0.0;
    double RA = P[0].rint + R_SW, RB = P[1].rint + R_SW;
    int a = phys_on(0), b = phys_on(1);
    I_s[0] = I_s[1] = 0;
    if (a && b) {
        double G = 1/RA + 1/RB;
        Vbus = (P[0].ocv/RA + P[1].ocv/RB - Il) / G;
        I_s[0] = (P[0].ocv - Vbus)/RA; I_s[1] = (P[1].ocv - Vbus)/RB;
    } else if (a || b) {
        int s = a ? 0 : 1; double R = s ? RB : RA;
        I_s[s] = Il; Vbus = P[s].ocv - Il*R;
    } else Vbus = 0;
}

static void measure(BmsStringSnap sn[2])
{
    solve();
    for (int s = 0; s < 2; s++) {
        double v;
#if SAFETY_VBUS_SEES_OPEN_STRING
        v = P[s].ocv - I_s[s]*P[s].rint;          /* string-side terminal */
#else
        v = Vbus;                                  /* bus-side: reads the bus */
#endif
        sn[s].valid = !P[s].dead;
        sn[s].v_mV  = (int32_t)lround(v*1000 + noise(3));
        sn[s].i_mA  = (int32_t)lround(I_s[s]*1000 + noise(5));
        sn[s].last_ok_ms = now; sn[s].fail_count = 0;
    }
}

static void integrate(double dt)
{
    solve();
    for (int s = 0; s < 2; s++) P[s].ocv -= I_s[s]*dt*K_OCV*accel;
}

/* ---------------- invariants ---------------- */
static int fails, checks, scen_fail;
static uint32_t last_change; static int any_change; static uint8_t prev_bits;
static double max_join_dv; static int joins;

static void fail(const char *m) { printf("    !! INVARIANT %s at t=%lu\n", m, (unsigned long)now); scen_fail++; }

static void on_hook(SwitchId id)
{
    uint8_t m = Safety_BlockMask();
    if ((id == SW_STRING_A && (m & SBLK_STR_A)) || (id == SW_STRING_B && (m & SBLK_STR_B))
        || (id == SW_LOAD && (m & SBLK_LOAD))) fail("G3 closed a blocked switch");
    if (id == SW_LOAD) return;
    int s = (int)id, o = 1 - s;
    if (phys_on(o)) {
        solve();
        double dv = P[s].ocv - Vbus;
        joins++;
        if (fabs(dv) > max_join_dv) max_join_dv = fabs(dv);
        if (verbose) printf("    [plant] close %c onto %c: true dV = %+.1f mV, peak I ~ %.2f A\n",
                            'A'+s, 'A'+o, dv*1000, fabs(dv)/(P[0].rint+P[1].rint+2*R_SW));
        if (fabs(dv) >= 0.100) fail("G1/G2 closed onto a conducting string with dV >= 100 mV");
        if (!RCF_PARALLEL_ENABLED) fail("G2 single-only build closed a string while the other conducts (break-before-make broken)");
    }
    if (any_change && (now - last_change) < RCF_COOLDOWN_MS) fail("G4 cooldown violated");
}
static void change_hook(void)
{
    uint8_t b = (uint8_t)(sim_odr[0] | (sim_odr[1] << 1));
    if (b != prev_bits) { last_change = now; any_change = 1; prev_bits = b; }
}

/* ---------------- loop ---------------- */
static int load_lost, lockouts; static uint8_t load_min; static int ever_on[2];
static char last_msg[96];

static void step(void)
{
    BmsStringSnap sn[2];
    measure(sn);
    (void)Safety_Step(sn, now);
    uint8_t ev = Reconfig_Step(sn, now);
    if (ev & RCF_EVT_LOAD_LOST) { motors = 0; load_lost++; }   /* main.c policy */
    if (ev & RCF_EVT_LOCKOUT) lockouts++;
    if (!Reconfig_MotorsAllowed()) motors = 0;                 /* main.c policy */
    if (sim_odr[0]) ever_on[0] = 1;
    if (sim_odr[1]) ever_on[1] = 1;
    const char *m;
    while ((m = Reconfig_TakeMessage()) != NULL) {
        snprintf(last_msg, sizeof last_msg, "%s", m);
        if (verbose) printf("    %6lu ms  %s\n", (unsigned long)now, m);
    }
    if (sim_odr[SW_LOAD] < load_min) load_min = sim_odr[SW_LOAD];
    integrate(0.020);
    now += 20;
}
static void run_ms(uint32_t ms) { uint32_t end = now + ms; while ((int32_t)(now - end) < 0) step(); }
static int until_label(const char *l, uint32_t max_ms)
{ uint32_t end = now + max_ms; while ((int32_t)(now - end) < 0) { step(); if (!strcmp(Reconfig_TopoLabel(), l)) return 1; } return 0; }

static void reset(double va, double vb)
{
    now = 1000; rng = 12345;
    memset(P, 0, sizeof P);
    P[0].ocv = va; P[1].ocv = vb; P[0].rint = P[1].rint = 3*0.026 + 0.015;
    motors = 0; I_MOTOR = 1.5; accel = 1.0;
    Switch_Init(); sim_on_hook = on_hook; sim_change_hook = change_hook;
    any_change = 0; prev_bits = 0; max_join_dv = 0; joins = 0;
    load_lost = 0; lockouts = 0; load_min = 1; ever_on[0] = ever_on[1] = 0; scen_fail = 0; last_msg[0] = 0;
    Safety_Init(now); Reconfig_Init(now);
    const char *m; while ((m = Reconfig_TakeMessage()) != NULL) if (verbose) printf("    %6lu ms  %s\n", (unsigned long)now, m);
}

#define EXPECT(c, d) do { checks++; if (!(c)) { printf("    !! EXPECT failed: %s\n", d); scen_fail++; } } while (0)
static void begin(const char *n) { printf("\n=== %s\n", n); }
static void end(void) { if (scen_fail) { fails++; printf("    -> FAIL\n"); } else printf("    -> PASS\n"); }
static int both_odr(void) __attribute__((unused));
static int both_odr(void) { return sim_odr[0] && sim_odr[1]; }

/* ---------------- scenarios ---------------- */
#if RCF_PARALLEL_ENABLED
static void parallel_scenarios(void)
{
    begin("P1 boot, dV 40 mV at rest -> start on higher (A), join after cooldown");
    reset(11.10, 11.06);
    EXPECT(until_label("A+B", 3000), "reached A+B");
    EXPECT(joins == 1 && max_join_dv < 0.1, "one join, true dV < 100 mV");
    motors = 1; run_ms(1000);
    EXPECT(strstr(last_msg, "JOIN VERIFIED") != NULL || Reconfig_TopoConfirmed(), "join verified under motor load");
    printf("    I_A=%.2f A  I_B=%.2f A  topo_conf=%u\n", I_s[0], I_s[1], Reconfig_TopoConfirmed());
    end();

    begin("P2 DEMO: parallel + motors, inject OC on B -> B cut off, A keeps the load");
    /* t_inj = the loop in which bench_debug would process the keystroke
       (it runs AFTER Safety_Step), i.e. one period before the first bad sample. */
    { uint32_t t_inj = now - 20; uint32_t t_off = 0, t_iso = 0;
      load_min = 1; int ll0 = load_lost;
      Safety_InjectOC(BMS_STR_B);
      for (int k = 0; k < 50; k++) { step();
          if (!t_off && !sim_odr[1]) t_off = now - 20;
          if (!t_iso && strstr(last_msg, "ISOLATED")) t_iso = now - 20; }
      printf("    B switch opened %lu ms after injection; open confirmed %lu ms after injection\n",
             (unsigned long)(t_off - t_inj), (unsigned long)(t_iso - t_inj));
      EXPECT(t_off && (t_off - t_inj) == 60, "B opened on the 3rd bad sample (60 ms)");
      EXPECT(t_iso && (t_iso - t_off) <= 80, "B open confirmed by readback within 4 samples");
      EXPECT(load_min == 1, "load switch never opened");
      EXPECT(load_lost == ll0 && motors == 1, "no LOAD_LOST, motors kept running");
      EXPECT(!strcmp(Reconfig_TopoLabel(), "A") && sim_odr[0], "running on A");
      printf("    after: I_A=%.2f A I_B=%.3f A  fault=%s\n", I_s[0], I_s[1], Safety_Label());
      run_ms(10000);
      EXPECT(!sim_odr[1], "B stays open (latched) for 10 s");
    }
    end();

    begin("P3 dV 150 mV at rest -> single A, never joins (10 s)");
    reset(11.20, 11.05);
    run_ms(10000);
    EXPECT(!strcmp(Reconfig_TopoLabel(), "A") && joins == 0, "single A, no join");
    end();

    begin("P4 dV 250 mV, 0.8 A drive, discharge x200 -> load pulls A down, join when gated");
    reset(11.30, 11.05); I_MOTOR = 0.8; accel = 200;
    run_ms(500); motors = 1;
    int ok = until_label("A+B", 120000);
    printf("    joined=%d after %lu ms, OCV_A=%.3f OCV_B=%.3f, worst true join dV=%.1f mV\n",
           ok, (unsigned long)(now - 1500), P[0].ocv, P[1].ocv, max_join_dv*1000);
    EXPECT(ok && max_join_dv < 0.1, "joined under load with true dV < 100 mV");
    end();

    begin("P4b dV 120 mV, 2.0 A drive: terminal sag puts A BELOW B -> no join while driving; joins at rest");
    reset(11.17, 11.05); I_MOTOR = 2.0; accel = 20;
    run_ms(500); motors = 1; run_ms(10000);
    EXPECT(joins == 0, "no join while A's terminal is below B");
    motors = 0; ok = until_label("A+B", 5000);
    printf("    after stop: joined=%d OCV_A=%.3f OCV_B=%.3f true dV=%.1f mV\n", ok, P[0].ocv, P[1].ocv, max_join_dv*1000);
    EXPECT(ok && max_join_dv < 0.1, "joins at rest, true dV < 100 mV");
    end();

    begin("P5a gate edge: dV 95 mV at rest -> join");
    reset(11.150, 11.055); EXPECT(until_label("A+B", 3000), "joined"); end();
    begin("P5b gate edge: dV 105 mV at rest -> no join in 10 s");
    reset(11.160, 11.055); run_ms(10000); EXPECT(joins == 0, "no join"); end();

    begin("P6 B pair failed OPEN: join, motors 2 A, B carries 0 -> dropped, never rejoins");
    reset(11.10, 11.08); P[1].fopen = 1;
    EXPECT(until_label("A+B", 3000), "join commanded");
    motors = 1; I_MOTOR = 2.0; run_ms(1500);
    EXPECT(!strcmp(Reconfig_TopoLabel(), "A") && !sim_odr[1], "B opened, running on A");
    motors = 0; run_ms(10000);
    EXPECT(!sim_odr[1], "B never re-closed");
    end();

    begin("P8 foreign code turns B on while single A -> LOCKOUT");
    reset(11.20, 11.05); run_ms(2000);
    sim_odr[1] = 1;                      /* bypasses hooks: a bug elsewhere */
    step();
    EXPECT(!strcmp(Reconfig_TopoLabel(), "LOCK") && !sim_odr[0] && !sim_odr[1] && !sim_odr[2], "locked out, all off");
    end();

    begin("P9 parallel, sensor B dies -> all off, LOCKOUT");
    reset(11.10, 11.06); until_label("A+B", 3000); motors = 1; run_ms(500);
    P[1].dead = 1; step(); step();
    EXPECT(!strcmp(Reconfig_TopoLabel(), "LOCK") && !sim_odr[0] && !sim_odr[1] && !sim_odr[2], "locked out");
    end();

    begin("P10 parallel, A and B both trip -> LOCKOUT");
    reset(11.10, 11.06); until_label("A+B", 3000); motors = 1; run_ms(500);
    Safety_InjectOC(0); Safety_InjectOC(1); run_ms(200);
    EXPECT(!strcmp(Reconfig_TopoLabel(), "LOCK"), "locked out");
    end();

    begin("P11 single A (dV 350 mV, no join possible), motors on, A trips -> swap to B");
    reset(11.40, 11.05); run_ms(2000); motors = 1; run_ms(500);
    Safety_InjectOC(0);
    EXPECT(until_label("B", 3000), "now on B");
    run_ms(500);
    EXPECT(!sim_odr[0] && sim_odr[1] && sim_odr[2] && load_lost == 1, "B + load on, one LOAD_LOST");
    end();
}
#else
static void single_scenarios(void)
{
    begin("S1 single-only build: start on B, load after 200 ms hold, A never on");
    reset(11.10, 11.05);
    EXPECT(until_label("B", 2000), "on B");
    motors = 1; run_ms(2000);
    EXPECT(sim_odr[1] && !sim_odr[0] && sim_odr[2], "B + load, A off");
    end();

    begin("S2 DEMO (single): driving on B, inject OC on B -> break-before-make swap to A");
    { uint32_t t_inj = now - 20, t_b_off = 0, t_l_off = 0, t_a_on = 0, t_l_on = 0;
      int both = 0;
      Safety_InjectOC(BMS_STR_B);
      for (int k = 0; k < 100; k++) { step();
          if (both_odr()) both = 1;
          if (!t_b_off && !sim_odr[1]) t_b_off = now - 20;
          if (t_b_off && !t_l_off && !sim_odr[2]) t_l_off = now - 20;
          if (!t_a_on && sim_odr[0]) t_a_on = now - 20;
          if (t_a_on && !t_l_on && sim_odr[2]) t_l_on = now - 20; }
      printf("    after injection: B off %lu ms, load off %lu ms, A on %lu ms, load on %lu ms\n",
             (unsigned long)(t_b_off - t_inj), (unsigned long)(t_l_off - t_inj),
             (unsigned long)(t_a_on - t_inj), (unsigned long)(t_l_on - t_inj));
      EXPECT(!both, "A and B never commanded on together");
      EXPECT(t_b_off && t_l_off > t_b_off && t_a_on > t_l_off && t_l_on > t_a_on, "order: B off, load off, A on, load on");
      EXPECT(t_a_on - t_b_off >= RCF_COOLDOWN_MS, "A closed >= cooldown after B opened");
      EXPECT(t_l_on - t_a_on >= RCF_PRE_LOAD_MS, "load waited for pre-load hold");
      EXPECT(load_lost == 1 && motors == 0, "one LOAD_LOST, motors stopped (PC13 to resume)");
    }
    end();

    begin("S3 inject B with motors STOPPED -> cannot prove B open -> LOCKOUT, A never closed");
    reset(11.10, 11.05); until_label("B", 2000); run_ms(500);
    Safety_InjectOC(1); run_ms(1000);
    EXPECT(!strcmp(Reconfig_TopoLabel(), "LOCK") && !ever_on[0], "locked out, A never closed");
    end();

    begin("S4 B pair STUCK closed, inject B while driving -> LOCKOUT, A never closed");
    reset(11.10, 11.05); until_label("B", 2000); motors = 1; run_ms(500);
    P[1].stuck = 1; Safety_InjectOC(1); run_ms(1000);
    EXPECT(!strcmp(Reconfig_TopoLabel(), "LOCK") && !ever_on[0], "locked out, A never closed");
    printf("    safety: %s\n", Safety_Label());
    end();

    begin("S6 manual all-off (bench '0') -> LOCKOUT");
    reset(11.10, 11.05); until_label("B", 2000); run_ms(300);
    sim_odr[0] = sim_odr[1] = sim_odr[2] = 0; step();
    EXPECT(!strcmp(Reconfig_TopoLabel(), "LOCK"), "locked out");
    end();

    begin("S7 idle A faults, B keeps running; then B faults -> LOCKOUT");
    reset(11.10, 11.05); until_label("B", 2000); motors = 1; run_ms(300);
    Safety_InjectOC(0); run_ms(500);
    EXPECT(!strcmp(Reconfig_TopoLabel(), "B") && motors == 1, "still on B, motors running");
    Safety_InjectOC(1); run_ms(500);
    EXPECT(!strcmp(Reconfig_TopoLabel(), "LOCK") && !ever_on[0], "locked out, A never closed");
    end();
}
#endif

/* Random fuzz: invariants only. */
static void fuzz(int seeds, uint32_t ms)
{
    int bad = 0;
    for (int sd = 1; sd <= seeds; sd++) {
        srand((unsigned)sd);
        reset(10.6 + (rand() % 120) / 100.0, 10.6 + (rand() % 120) / 100.0);
        if (rand() % 6 == 0) P[rand() % 2].fopen = 1;
        int stuck_at = (rand() % 8 == 0) ? (int)((uint32_t)rand() % ms) : -1;
        accel = 50;
        uint32_t t0 = now;
        while (now - t0 < ms) {
            int r = rand() % 1000;
            if (r < 8) motors = !motors;
            else if (r < 10) I_MOTOR = 0.3 + (rand() % 30) / 10.0;
            else if (r == 10) Safety_InjectOC((uint8_t)(rand() % 2));
            if (stuck_at >= 0 && (int)(now - t0) == stuck_at) P[rand() % 2].stuck = 1;
            step();
        }
        if (scen_fail) { bad++; printf("    fuzz seed %d FAILED\n", sd); }
    }
    printf("\n=== FUZZ %d seeds x %lu ms: %s\n", seeds, (unsigned long)ms, bad ? "FAIL" : "PASS (no invariant violated)");
    if (bad) fails++;
}

int main(int argc, char **argv)
{
    (void)argv;
    verbose = (argc > 1);
    printf("[SIMULATED] build: RCF_PARALLEL_ENABLED=%d (SAFETY_VBUS_SEES_OPEN_STRING=%d)\n",
           RCF_PARALLEL_ENABLED, SAFETY_VBUS_SEES_OPEN_STRING);
#if RCF_PARALLEL_ENABLED
    parallel_scenarios();
#else
    single_scenarios();
#endif
    int v = verbose; verbose = 0; fuzz(400, 30000); verbose = v;
    printf("\nSCENARIO FAILURES: %d  (expects checked: %d)\n", fails, checks);
    return fails ? 1 : 0;
}
