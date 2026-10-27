/*
 * bms_safety_cfg.h — every safety threshold in one place
 *
 *  M1 Step 5. Each value carries its source:
 *    [DATASHEET] Samsung INR18650-30Q spec v1.0 (project file
 *                INR1865030Q_datasheet.PDF, page 2/20, section 3.x)
 *    [DESIGN]    our choice, with the reason
 *    [ASSUMPTION] needs bench confirmation
 *
 *  STRING vs CELL: each string is 3S1P, so
 *    string current = cell current            (series path, KCL)
 *    string voltage = sum of 3 cell voltages  (cells NOT individually seen)
 *  Every voltage limit below is therefore an AVERAGE-per-cell limit.
 *  An imbalanced string can have one cell past the limit while the string
 *  sum is inside it. Known limitation, listed in the plan v3 report section.
 */

#ifndef BMS_SAFETY_CFG_H
#define BMS_SAFETY_CFG_H

/* ---------------- Voltage (per string, mV) ----------------
 * OV: 3 x 4.20 V = 12.60 V. Charge voltage 4.20 +/- 0.05 V   [DATASHEET 3.3]
 *     NOTE: a charger at the top of its tolerance (4.25 V/cell = 12.75 V)
 *     WILL trip this. Charge termination (12.54 V, plan v3) must stop the
 *     charger first. That is Step 6's job, not this file's.
 * UV: 3 x 2.50 V = 7.50 V. Discharge cut-off 2.5 V            [DATASHEET 3.8]
 *
 * Both are measured at the INA260 VBUS pin, downstream of the cells:
 *   discharging -> V_measured < V_cells (IR drop) -> UV trips EARLY (safe side)
 *   charging    -> V_measured > V_cells           -> OV trips EARLY (safe side)
 */
#define SAFETY_OV_MV                12600
#define SAFETY_UV_MV                 7500

/* ---------------- Current (per string, mA) ----------------
 * Discharge: 12 A design trip, below the 15 A max continuous
 *            discharge [DATASHEET 3.7]. 3 A margin           [DESIGN]
 * Charge:    4 A = rated (max) charge current                 [DATASHEET 3.4]
 * INA260 full scale is about +/-15 A [VERIFY INA260 datasheet]; both trips
 * are inside it.
 */
#define SAFETY_OC_DSG_MA            12000
#define SAFETY_OC_CHG_MA             4000

/* Which current sign means DISCHARGE, per string.
 * +1: INA260 reads positive while the string discharges into the load.
 * [ASSUMPTION] until bench test T6 (Step 4) confirms it. A wrong sign is a
 * SAFE-SIDE failure: discharge current is then judged against the 4 A
 * charge limit and trips early, it never trips late. */
#define SAFETY_DSG_SIGN_A           (+1)
#define SAFETY_DSG_SIGN_B           (+1)

/* ---------------- Timing ----------------
 * Debounce: 3 consecutive bad samples. Loop = 20 ms (main.c), so
 *   trip latency = 3 x 20 = 60 ms, + up to 20 ms phase = 60..80 ms [CALCULATED]
 * Filters motor transients shorter than ~60 ms.                    [DESIGN]
 */
#define SAFETY_DEBOUNCE_N               3u

/* Samples every sensor must deliver, all valid, after boot before any
 * string may be switched on. Gives a still-present fault (e.g. a string
 * that is still OV after a reset) the chance to re-latch BEFORE anything
 * connects. 3 x 20 ms = 60 ms.                                     [DESIGN] */
#define SAFETY_READY_SAMPLES            3u

/* ---------------- Stuck-switch readback ----------------
 * A string commanded OFF must carry ~0 A. The back-to-back pair blocks
 * both directions (bench-confirmed in M1: 0.0004 mA leak), so ANY real
 * current through an OFF string means a switch did not open.
 * 200 mA threshold: far above INA260 offset (a few mA [VERIFY]),
 * far below any real load.                                     [ASSUMPTION]
 * Settle: ignore the first 100 ms after the OFF command. Gate turn-off
 * is ~0.17 ms [CALCULATED, bench_debug.c]; 100 ms also covers several
 * INA260 conversions.                                              [DESIGN] */
#define SAFETY_STUCK_MA               200
#define SAFETY_STUCK_SETTLE_MS        100u

/* ---------------- Where is each INA260's VBUS pin? ----------------
 * Step 4 Finding B / bench test T4.
 *   0 = VBUS is on the BUS side of the string switch (or unknown).
 *       An OFF string's sensor then reads the bus, not its own cells, so
 *       OV/UV are only evaluated while that string is ON.
 *       This setting is also what avoids a boot deadlock: with all
 *       switches off the bus is ~0 V, and judging that as "UV" would latch
 *       both strings forever.
 *   1 = T4 proved each sensor sees its own string with the switch open.
 *       OV/UV are then evaluated all the time, including before connect.
 * Default 0 is correct for both wirings; 1 is only better, never safer
 * when wrong.                                                      [DESIGN] */
#define SAFETY_VBUS_SEES_OPEN_STRING    0

/* After an OFF->ON command, wait this long before trusting VBUS as this
 * string's voltage (bus-side wiring: the bus settles to this string).
 * Applies only when SAFETY_VBUS_SEES_OPEN_STRING == 0.             [DESIGN] */
#define SAFETY_V_SETTLE_MS            100u

/* ---------------- Fault injection (demo) ----------------
 * 1 = UART "f" then "a"/"b" overrides that string's current, as seen BY
 *     THE SAFETY LAYER ONLY, with SAFETY_INJECT_MA in the discharge
 *     direction. Debounce, latch, switch-off and display then run the real
 *     code path. The OLED/CSV still show the real measured current, and the
 *     fault label carries a '*' so an injected trip is never presented as
 *     a real one.
 * 0 = compiled out. Set to 0 for any run that is reported as a real fault. */
#define BMS_FAULT_INJECTION             1
#define SAFETY_INJECT_MA     (SAFETY_OC_DSG_MA + 1000)

#endif /* BMS_SAFETY_CFG_H */
