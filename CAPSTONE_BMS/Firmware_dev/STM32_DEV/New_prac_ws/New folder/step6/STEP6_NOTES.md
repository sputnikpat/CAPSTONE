# M1 Step 6 — Reconfiguration state machine

Labels: `[DATASHEET]` (project file, page given), `[CALCULATED]` (math shown),
`[SIMULATED]` (host build, plant model), `[ASSUMPTION]` (needs bench data),
`[VERIFY]` (from memory, not checked against a source). **Nothing here is
`[BENCH-MEASURED]` yet.** Tests S6-T1 to S6-T10 produce those numbers.

> **IP note.** This file describes the reconfiguration mechanism in full. It
> is an internal working document. Decide separately how much of §3–§4 goes
> into the public report.

---

## 1. Scope and decisions

| Item | Decision | Who |
|---|---|---|
| Precharge | **Skipped.** Not on the PCB (PB6 unconnected). Paralleling relies on the ΔV gate alone. | You |
| Charging | **Skipped.** No charger path on the netlist. | You |
| Demo | String B is cut off by an injected (fake) over-current; the system carries on. | You |
| Paralleling | Compiled in **only** when `SAFETY_VBUS_SEES_OPEN_STRING = 1` (Step 4 T4 passed). No separate override. | Me, default (your Q1 open) |
| After a single-string swap | Motors stop; press PC13 to drive again (same as Step 5). | Me, default (Q3 open) |
| SOC split / rejoin / alternation | Not in Step 6. Step 7 provides SOC. | Me, default (Q5 open) |

**What you get depends on one flag.**

| `SAFETY_VBUS_SEES_OPEN_STRING` | Build | Demo behaviour |
|---|---|---|
| `0` (shipped default; T4 not yet done) | SINGLE-ONLY | Runs on B. `f b` → B cut off → break-before-make swap to A. **Rover stops ~0.7 s, then PC13 to drive on A.** |
| `1` (only after T4 proves string-side VBUS) | PARALLEL | Runs on A+B. `f b` → B cut off → **A already carries the load; the rover never stops.** |

The parallel demo is the stronger one, so running T4 is now the single most
valuable bench task.

---

## 2. Files

| File | Status | Purpose |
|---|---|---|
| `reconfig_cfg.h` | NEW | Every reconfig threshold, each labelled with its source |
| `reconfig.h` / `reconfig.c` | NEW | The state machine. The only code that turns a switch ON. |
| `main.c` | REPLACES Step 5 | Reconfig slot after safety; new motor policy; at most one event line per loop |
| `bench_debug.h` / `.c` | REPLACES Step 5 | Manual `a`/`b`/`l` compiled out (`BENCH_MANUAL_SWITCHING 0`). Keeps `0` (emergency stop) and `f a` / `f b`. |
| `sim/*` | NEW | Host simulation: plant model, 18 scenarios, fuzzing, invariant hooks |
| `bms_safety.*`, `bms_switch.*`, drivers, display, telemetry | **UNCHANGED** | — |

**Dependency direction:** `reconfig` → `bms_safety`, `bms_switch`. Nothing
includes `reconfig.h` except `main.c`. Display and telemetry receive only
`topo_label` (text) and `topo_confirmed` through the existing `BmsStatus`
fields, so `telemetry.c` and `bms_display.c` need no changes.

---

## 3. State machine

```
 RESET ─► WAIT ── Safety_Ready()? (3 clean samples, 60 ms)
            │
            │ pick start string X
            │   PARALLEL build: the HIGHER string (see 4.3)
            │   SINGLE build:   RCF_SINGLE_START_STR (= B)
            ▼
        CONNECT(X)  X on, LOAD OFF, hold 200 ms ── safety judges X's OV/UV first
            │
            ▼ load on
        SINGLE(X) ─────── dV gate 5 samples in a row + load stable ────► PARALLEL
            │             + cooldown 500 ms  (PARALLEL build only)          │
            │                                                               │ join readback:
            │ safety opened X                                               │ Y must carry
            ▼ (the ONLY string)                    safety opened Y          │ current under
        SWAP_OPEN(X)   motors still commanded    ◄──── (DEMO PATH) ─────────┤ real load
            │ |I_X| < 100 mA for 3 samples                                  │
            │ AND I_X was ≥ 300 mA before the trip                          ▼
            │ (else LOCKOUT: cannot prove open)                        ISOLATE(Y)
            ▼                                                    no switch closes;
        load OFF, motors stopped (EVT_LOAD_LOST)                 X already has the load;
            ▼                                                    confirm |I_Y| < 100 mA,
        SWAP_WAIT  dead bus, ≥ 20 ms AND cooldown 500 ms         3 samples
            ▼                                                           │
        CONNECT(Y) ──► SINGLE(Y)                                        ▼
                                                                    SINGLE(X)

 Any of: global fault, both strings down, a switch moved by other code,
 open not provable, opened string still conducting after 300 ms,
 survivor not carrying the load           ──────────────►  LOCKOUT
                                                    all off, every cycle,
                                                    until MCU reset
```

**Why the parallel demo is reliable on this hardware.** On `PARALLEL → ISOLATE`
nothing has to close. The healthy string is already conducting, so the load
never sees a gap. The only switching action is the opening, and safety performs
it.

### Guarantees (each one is a host-sim assertion, §8)

| | Guarantee | Enforced by |
|---|---|---|
| G1 | A and B are both commanded on only after the ΔV gate passed on the 5 samples right before the close | `st_single()` + compile-time `RCF_PARALLEL_ENABLED` |
| G2 | Break-before-make: the old string is **proven** open by current readback before a new one closes | `st_swap_open()` |
| G3 | Never closes a switch that safety blocks | `available()` and again in `close_sw()` |
| G4 | ≥ 500 ms between topology changes, including safety-forced openings | `cooldown_over()` |
| G5 | Any unexplained state → LOCKOUT, reset-only | reconcile step + `lockout()` |

---

## 4. Math

### 4.1 ΔV gate, including the load term `[CALCULATED]`

With X on and carrying I_X, and Y off, both sensors read string-side terminal
voltages (guaranteed by the build flag). The voltage that drives the join
current is V_Y − V_bus, where

```
V_bus = V_X − I_X · R_path,     0 ≤ R_path ≤ R_PATH_MAX = 50 mΩ

ΔV_real = V_Y − V_X + I_X · R_path          (linear in R_path)

ΔV_lo = V_Y − V_X                    (R_path = 0)
ΔV_hi = V_Y − V_X + I_X · 50 mΩ      (R_path = max)

|ΔV_real| ≤ max(|ΔV_lo|, |ΔV_hi|)
```

Requiring both bounds below 100 mV therefore guarantees a real ΔV below
100 mV without knowing R_path exactly.

R_PATH_MAX = 2 × 20 mΩ RDS(on) max `[DATASHEET IRF4905 p.2, at VGS = −10 V, ID = −38 A]`
+ 2 mΩ INA260 shunt + 8 mΩ margin `[ASSUMPTION]`.

At a low string (≈ 7.5 V) VGS is only about −7 V. RDS(on) is not specified
there and will be higher. S6-T4 measures the real drop.

**Consequence: the gate window closes under heavy load.** At I_X = 2 A,
I_X · R_max = 100 mV, so ΔV_lo must lie inside (−100, 0) mV. This means
String X's loaded terminal voltage must be 0–100 mV above String Y.
- In sim P4b, A sagged below B and correctly never joined while driving. It joined once the motors stopped `[SIMULATED]`.
- In sim P4, at 0.8 A the load itself pulled A into the window and the join happened while driving `[SIMULATED]`.

### 4.2 Join inrush `[CALCULATED]`

```
I_join ≤ ΔV / R_loop,   ΔV < 0.10 V

R_loop typical:     6 × 26 mΩ (cells)   = 156 mΩ   [DATASHEET 30Q §7.5: ≤ 26 mΩ, AC 1 kHz]
                  + 4 × 20 mΩ (2 pairs) =  80 mΩ   [DATASHEET IRF4905 RDS(on) max]
                  + wiring              ≈  30 mΩ   [ASSUMPTION]
                                        = 266 mΩ  →  I_join ≤ 0.38 A
R_loop pessimistic: MOSFETs only        =  80 mΩ  →  I_join ≤ 1.25 A
```

Both values are far below the 12 A OC trip and the 15 A cell rating. The worst
join in the sim was a true ΔV of 92.5 mV, at the P5a gate edge `[SIMULATED]`.

### 4.3 Why start on the higher string

There is no precharge, so ΔV can only shrink if the load pulls the higher
string down toward the idle one. Starting on the lower string makes ΔV grow,
and the join can never happen.

### 4.4 Dead time `[CALCULATED]`

Turn-off is set by the 10 kΩ pull-up charging **both** gates on one net
(Gate_Q1, Gate_Q5, Gate_Q8 in the netlist):

```
t ≈ 2 · Qg · R / V = 2 × 180 nC × 10 kΩ / 10 V = 0.36 ms
```

Qg max is 180 nC `[DATASHEET IRF4905 p.2]`. That figure is given at 38 A and
−44 V, so this is an upper bound. One loop (20 ms) gives about 55× margin.

The real break-before-make proof is the current readback, not the timer. This
corrects Step 5's 0.17 ms figure, which counted one gate only.

### 4.5 Timing of the cutoff

These timings are `[SIMULATED]` and match `[CALCULATED]`.
`bench_debug` runs after `Safety_Step`, so an injection takes effect on the
next loop's sample.

| Event | Parallel build | Single build |
|---|---|---|
| B switch opens (3 bad samples) | 60 ms after the loop that read the keystroke | 60 ms |
| B open confirmed (3 samples < 100 mA) | 120 ms | 120 ms (then load off) |
| A closes | — (already on) | 560 ms (cooldown from the trip) |
| Load back on | never interrupted | 760 ms (200 ms pre-load hold) |
| Motors | keep running | stopped; PC13 to resume |

Add 0–20 ms for when the keystroke arrives within the loop. That gives the
Step 5 figure of 60–80 ms for the trip.

### 4.6 Load switch and the L298N input capacitor (no precharge)

This is a residual risk. It is not new: Step 5's `l` command had it too.

Closing the load switch charges the L298N module's input capacitor from about 0 V:

```
R = 78 mΩ (cells) + 40 mΩ (string pair) + 40 mΩ (load pair) + 30 mΩ (wiring) ≈ 188 mΩ
I_peak ≤ 11.1 V / 0.188 Ω ≈ 59 A        (ignores wiring inductance and capacitor ESR: upper bound)
IDM = 260 A pulsed                      [DATASHEET IRF4905 p.1]  → within rating
τ = R·C; C = 470 µF [ASSUMPTION — read the capacitor on your module]  → τ ≈ 88 µs
I²t = I_peak² · τ / 2 ≈ 59² × 88e-6 / 2 ≈ 0.15 A²s
```

**Check this I²t against your fuse's datasheet melting I²t.** The firmware
cannot see an 88 µs pulse: it samples every 20 ms.

**What opening the load during a swap does and does not do.**
- It does **not** keep the capacitor charged. The module's 5 V regulator is probably fed from the 12 V input (the 5V+ pin is unconnected in the netlist) `[ASSUMPTION]`. At about 20 mA, a 470 µF capacitor would drop more than its full voltage in the 640 ms dead time (ΔV = I·t/C).
- It **does** two things:
  1. The new string meets an isolated bus first, so safety judges its OV/UV for 160 ms before it touches the motors.
  2. The motors restart only on PC13, never by themselves.

### 4.7 VIN on String A helps the swap

The netlist ties Nucleo VIN to `STR_A`, upstream of Q1/Q2. The MCU therefore
stays powered through every dead-bus gap, whichever string is selected. The
cost: A is always slightly drained by the MCU, so A tends to be the lower
string.

---

## 5. Edge cases handled (decisions made on your behalf)

1. **The fault demo must be injected while driving.** "0 A after opening"
   proves nothing if no current flowed before. If I_X < 300 mA before the trip
   (motors stopped), a single-build swap goes to **LOCKOUT**, because a stuck X
   plus a closed Y would be an ungated parallel. Parallel builds have a fallback:
   if ΔV is inside the gate, even a stuck X would make a legal join, so the swap
   proceeds.
2. **Motors stay commanded while the open is being proven.** This is the bug
   found in sim S4. Stopping the motors first made a stuck pair read about 20 mA,
   which looked "open", and A was then closed onto it. It is now fixed and
   mutation-tested (§8.3).
3. **A joined string that carries no current under real load is opened, not left closed.**
   A failed-open Y is harmless while it stays failed. An intermittent contact,
   however, could reconnect later with an unchecked ΔV. Y is marked
   failed-open and never rejoins until reset.
4. **A near-zero share in parallel can be legitimate.** I_Y = I_L/2 − ΔV/R_loop
   can be about 0. So a "not conducting" verdict needs a total load ≥ 1.5 A,
   sustained for 500 ms (new join) or 1 s (steady state). A false verdict
   opens the string, which is the safe direction. It costs the parallel state,
   not safety.
5. **The idle string faults.** Nothing switches. That string is simply
   unavailable from then on.
6. **`0` on the terminal** is now an emergency stop. Reconfig sees switches it did not open and goes to LOCKOUT.
7. **A fault during the 200 ms pre-load hold.** The load is off, so there is no current and the open cannot be proven. A single build therefore goes to LOCKOUT.
8. **Faults never clear in software.** There is no rejoin after a fault, and no
   retry of a failed-open string, until reset. This also bounds the number of
   topology changes per boot, which is a second guard against oscillation on
   top of the cooldown.
9. **Flag set to 1 by mistake with bus-side VBUS.** All switches are off at
   boot, so the bus node is floating. Its only capacitance is about 3 nF of
   MOSFET Coss per pair, and it discharges into the INA260 VBUS inputs during
   the 1.5 s boot screen `[ASSUMPTION; VBUS input impedance VERIFY]`.
   Safety (flag = 1 judges OV/UV always) would then see about 0 V, latch UV on
   both strings, and lock out before anything closes. This is a fail-safe
   argument, not a substitute for T4.

---

## 6. Integration

1. Copy `reconfig_cfg.h` and `reconfig.h` to `Core/Inc`, and `reconfig.c` to `Core/Src`.
2. Replace `main.c`, `bench_debug.h` and `bench_debug.c`. If CubeMX regenerates,
   merge the USER CODE blocks; don't overwrite.
3. Build. The new code uses no HAL calls (it only uses `bms_switch` and
   `bms_safety`). `main.c` and `bench_debug.c` were syntax-checked only
   against a **fake** HAL. The first CubeIDE build is their real compile check.
4. Leave `SAFETY_VBUS_SEES_OPEN_STRING 0` until T4 passes. Set it to `1`
   **only** after T4, then rebuild. The UART banner says which mode is running:
   `# RCF: mode SINGLE-ONLY ...` or `# RCF: mode PARALLEL ...`.

UART lines you will see: `# RCF: CONNECT B`, `# RCF: LOAD ON via B`,
`# RCF: JOIN ... dV=lo..hi mV`, `# RCF: JOIN VERIFIED`,
`# RCF: SAFETY OPENED B ...`, `# RCF: ISOLATED B ...`, `# RCF LOCKOUT: <reason>`.
The OLED's TOPO row shows `WAIT`, `B-NOLD` (string on, load off), `B`, `A+B`,
`SWAP` or `LOCK`, with `CONF` when the current readback agrees.

---

## 7. Bench tests

Run them in order, always behind the fuse and kill switch, with
`BMS_FAULT_INJECTION 1`. Prerequisites: S5-T1 (sensors in the power path),
Step 4 T6 (current sign), Step 4 T1 (PB4 at reset).

| # | Test | Method | Pass |
|---|---|---|---|
| S6-T1 | Boot sequence | Scope PB5 (or PB4) and PB10. Reset. | String pin rises ≥ 60 ms after the loop starts. PB10 rises ≥ 200 ms after it. OLED: `WAIT` → `B-NOLD` → `B`. |
| S6-T2 | Readback thresholds | All off: note `ia`/`ib`. String on, motors stopped: note the idle current. Motors on: note the running current. | Off < 100 mA (`RCF_I_MIN_MA`). Running > 300 mA (`RCF_I_PRE_OPEN_MA`). Otherwise retune them in `reconfig_cfg.h`. |
| **S6-T3** | **Single-build demo swap** | Drive on B with PC13. Type `f` then `b`. Scope PB5, PB10, PB4. | PB5 falls ~60–80 ms after the keystroke. PB10 falls, **then** PB4 rises ≥ 500 ms after PB5 fell, **then** PB10 rises 200 ms later. PB4 and PB5 are never high together. PC13 drives on A. Pass 3× in a row. |
| **S6-T4** | **Stuck pair must block the swap** | Bridge B's pair drain-to-drain with a **fused** jumper, as in S5-T10. Drive on B, `f b`. | **PB4 never rises.** `STUCK B`, then `RCF LOCKOUT`. This is the most important Step 6 test. |
| S6-T5 | Injection with motors stopped | On B, motors stopped, `f b`. | LOCKOUT with "cannot prove old string open". Expected behaviour, not a bug. |
| S6-T6 | Emergency stop | `0` while driving. | All off, `LOCK`, motors stop. |
| S6-T7 | Loop timing | Run 5 min, including events. | `overruns` rises by at most 1 per event line; `work_us_max` noted. |
| **After T4, flag = 1 only:** | | | |
| S6-T8 | Gate refuses | Precondition ΔV ≈ 150 mV at rest. | Stays `A` (or `B`); no `JOIN` line. |
| S6-T9 | Join inrush | Precondition ΔV ≈ 50–90 mV. Current probe or a scope shunt on String B (the INA260 is too slow). | Peak ≤ 1.25 A (pessimistic calc). Record the peak and compare it with §4.2. |
| **S6-T10** | **Parallel demo** | `A+B`, drive, `f b`. | B opens; **rover keeps driving**; `ia` rises to about the total current; `ISOLATED B` line; load switch never drops (scope PB10). Pass 3× in a row. |

---

## 8. Host simulation evidence `[SIMULATED]`

**Setup.** `reconfig.c` and the **unmodified** Step 5 `bms_safety.c` are
compiled on a PC (`gcc -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow`,
zero warnings in both configurations). They run against a plant model:
- OCV + R_int per string; a 30 mΩ pair; a current-sink load (20 mA idle + motor)
- faults: stuck pair, failed-open pair, dead sensor
- ±3 mV and ±5 mA noise
- VBUS string-side for the flag = 1 build, bus-side (worst case) for flag = 0

The switch stub checks G1–G4 on **every** switch-on command, using the
plant's **true** ΔV, not the sensors' ΔV.

### 8.1 Scenarios: 18 of 18 pass

| Build | Scenarios |
|---|---|
| PARALLEL | P1 boot + join at rest (true ΔV 37.5 mV) · **P2 demo: B opens at 60 ms, confirmed at 120 ms, load never interrupted, motors keep running, A carries 1.52 A** · P3 ΔV 150 mV never joins · P4 0.8 A load pulls A into the gate (true ΔV 68.7 mV) · P4b 2 A: no join while driving, joins at rest · P5a 95 mV joins / P5b 105 mV refused · P6 failed-open B dropped, never rejoins · P8 foreign switch-on → LOCKOUT · P9 sensor lost → LOCKOUT · P10 both trip → LOCKOUT · P11 single → swap |
| SINGLE | S1 boot on B · **S2 demo swap (order and timing in §4.5)** · S3 stopped motors → LOCKOUT · **S4 stuck B → STUCK → LOCKOUT, A never closed** · S6 `0` → LOCKOUT · S7 idle fault ignored, then both down → LOCKOUT |

### 8.2 Fuzzing

400 random runs × 30 s per build, with random:
- start voltages (10.6–11.8 V)
- motor on/off and current (0.3–3.3 A)
- injections
- failed-open and stuck pairs

**No invariant was violated in either build.**

### 8.3 Mutation tests (does the harness actually catch errors?)

| Deliberate bug | Caught? |
|---|---|
| ΔV gate 100 → 300 mV | yes (220 violations) |
| Gate on ΔV_lo only (no I·R term) | yes (15) |
| No cooldown before a join | yes (66) |
| Swap without the "current flowed before" proof | yes |
| Swap without waiting for open confirmation | yes |
| Stop motors before the open proof (the S4 bug) | yes |
| Remove the block-mask check in `close_sw()` alone | **no.** Every caller checks `available()` first, so this is defence in depth. |
| Remove the block-mask check in both places | yes (964) |

### 8.4 Limits of the simulation

- No HAL, no INA260 conversion timing, no I2C.
- No switching transients, wiring inductance or capacitor inrush.
- The plant resistances are assumptions, and OCV discharge is linear and accelerated (×20–×200) in the long scenarios.
- It verifies decision logic and sequencing only.

---

## 9. FMEA — Step 6 code

| Failure | Detection | Safe response |
|---|---|---|
| Close onto a string at large ΔV | ΔV gate: both bounds < 100 mV, 5 samples, load stable | Join refused; stay single |
| Sensor VBUS bus-side (false ΔV ≈ 0) | Not detectable by firmware | **Parallel compiled out** unless T4 passed (flag). Mis-set flag → UV at boot → LOCKOUT (§5.9) |
| Old pair stuck closed during a swap | Open never confirmed under load; safety STUCK at ~160 ms | LOCKOUT; new string **never** closed (S4) |
| Old pair state unprovable (no load) | I before trip < 300 mA | LOCKOUT (S3), or ΔV-gate fallback in parallel builds |
| Joined pair fails open / intermittent | I < 100 mA under ≥ 1.5 A total for 0.5–1 s | Open it, mark failed, no rejoin until reset |
| Survivor not carrying the load after isolation | I_X < 100 mA with pre-trip total ≥ 1.5 A | LOCKOUT |
| Opened string still conducting | No confirmation within 300 ms (and safety STUCK) | LOCKOUT, all off |
| Other code moves a switch (bug, leftover bench code) | Commanded ≠ output register | LOCKOUT, all off |
| Topology oscillation | 500 ms cooldown + faults latch until reset | Bounded number of changes per boot |
| Safety and reconfig disagree | Safety runs first; reconfig only closes if the mask allows | Safety always wins |
| Pair shorted **before** power-up, then the other string closes | **Not detectable** with no load current | Fuse; S5-T10/S6-T4 procedures. Accepted limitation |
| L298N capacitor inrush at load-on (no precharge) | Not visible (88 µs vs 20 ms sampling) | Within IDM; fuse I²t check (§4.6) |
| Event print overruns the loop | `overruns` counter | Switching already done earlier in the same loop; IWDG margin unaffected |
| Loop hangs inside reconfig | IWDG | Reset into all-off |

---

## 10. Changes to earlier documents

- **Step 5 §5.8 superseded.** Motors no longer stop on every new fault. They stop only when reconfig releases the load.
- **Step 5 §10:** the "final demo needs Step 6" item is now implemented. It is the parallel build.
- **Plan v3 thresholds:** "Precharge timeout 500 ms" and "R_precharge 10 Ω" do not apply to the fabricated board. Mark them as superseded.
- **Step 5 `bench_debug.c` dead-time comment** (0.17 ms) counted one gate. The corrected figure is 0.36 ms for the pair (§4.4).
- Step 5's 60–80 ms trip latency is confirmed by the sim's loop ordering.

## 11. Outstanding

- **T4 (VBUS location).** It unlocks the parallel demo.
- S6-T2, to tune `RCF_I_MIN_MA` and `RCF_I_PRE_OPEN_MA` against real noise and idle current.
- The fuse's I²t versus §4.6; the L298N input capacitor value.
- RDS(on) at a low string (VGS ≈ −7 V) versus `RCF_R_PATH_MAX_MOHM`.
- Your open answers on Q3 (auto-resume after a swap?) and Q5 (SOC hooks in Step 7).
