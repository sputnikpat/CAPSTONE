# Capstone Milestone Plan v2 — Reconfigurable BMS for Rover (8-Week Compressed)

> **Updated post-M0.** All simulation findings incorporated. Thresholds locked. Thesis reframed.
>
> **Thesis (revised):** String-level reconfiguration provides fault isolation, graceful degradation, safe reconnection of mismatched strings, and independent charging paths — at the cost of 2×R_DS(on) per string and reduced soft-degradation tolerance. The energy-extraction hypothesis was tested and not supported at 2P; the value is safety and availability, with benefits that strengthen at higher parallel counts.
>
> **The one rule:** Protect the fault-isolation demo. Sacrifice alternation, then EKF, before touching it.

---

## Architecture Summary (Locked Decisions)

- **MCU:** STM32F446RE (Nucleo-64)
- **Cells:** Samsung INR18650-30Q, 3S2P (6 cells)
- **Switching:** Back-to-back IRF4905 pairs + 2N2222A NPN level shifters (no gate driver ICs)
- **Sensing:** Discrete — 33k/10k voltage dividers, INA226 current sensors, 10K NTC thermistors
- **No DC-DC converter, no per-cell bypass, no ML, no dedicated battery-monitor IC**

## Locked Thresholds (from M0)

| Parameter | Value | Source |
|---|---|---|
| OV trip | 4.20 V/cell | Verified, SPEC 3.3 |
| UV trip | 2.50 V/cell | Verified, pack design guideline |
| OC trip | 12 A/string | Design — below 15 A cell rating, drive-cycle peak ~7.5 A/string |
| OT trip | 60 °C | Design — Samsung recommended derate point |
| Charge-full | 4.18 V/cell | Design — 20 mV below OV trip |
| R_precharge | 10 Ω | Design — validated Day 3 sweep |
| C_bus | 1000 µF | Assumption — verify against BTS7960 input caps |
| Precharge timeout | 500 ms | Calculated — 10× time constant |
| Cooldown | 500 ms | Assumption |
| Debounce | 3 samples | Design |
| SOC split threshold | 15% | Design |
| SOC rejoin threshold | 5% | Design — hysteresis band |
| Load disconnect switch | REQUIRED | M0 Day 3 — without it, best handover = 36.4 A |

## M0 Key Results (Reference for All Future Milestones)

| Scenario | Result | Implication |
|---|---|---|
| S1: Matched baseline | 28.35 Wh identical | Zero overhead when not needed — credibility anchor |
| S2: Thermal fault | Isolated at 60 °C vs 94.4 °C fixed | Core thesis — decisive |
| S3: SOC imbalance | 28.33 Wh identical | Energy hypothesis disproved — value is safe joining |
| S4: Charging | Both reach 99.4% at 2P | Self-balancing masks advantage at 2P; 7 A uncontrolled inter-string current is the safety concern at scale |
| Drive cycle | 15 A peak, 0 false trips, 4 transitions | State machine survives real transients |
| Body-diode test | Single MOSFET: 168,571 mA leak; Pair: 0.0004 mA | Back-to-back pair is mandatory |
| Cold connect | 218.8 A without precharge; 1.09 A with precharge + load switch | Load disconnect switch is mandatory |

## M0 Bugs Found (Document in Report)

1. Bleed resistor set to 1 Ω instead of 1 MΩ — drained bus during precharge
2. Current source polarity + sign — injected current instead of sinking
3. Current source not gated by cmd_load — drew current with load switch open
4. Open-wire false positive — shared counter + no check that current was expected
5. Duplicate params files (BMS_Params.m vs BMSM0.m) with different struct names
6. InitFcn not set — model used whatever was in workspace

---

## Milestone 1 — Switching + Sensing (Weeks 2–3)

**Goal:** Physical switching and sensing subsystems working on breadboard/zero PCB. No cells yet — bench supply + resistive load only.

### Track A — Switching Hardware

- Breadboard one NPN level shifter + one back-to-back IRF4905 pair
- **Body-diode blocking test** — confirm 0 mA both directions with pair open. If this fails, nothing downstream works.
- Solder full switching stage on zero PCB:
  - Q1 pair (String A main) + level shifter
  - Q2 pair (String B main) + level shifter
  - Q3 pair (precharge) + level shifter
  - **Q4 pair (load disconnect) + level shifter** ← NEW, mandatory from M0
  - Precharge resistor (10 Ω), bulk cap (1000 µF), fuse, kill switch
- Scope switching transients with resistive load — check ringing, clean edges
- **BOM note:** 8 IRF4905 total (6 for Q1/Q2/Q3 + 2 for Q4 = 8, zero spares). 5 × 2N2222A (4 needed + 1 spare). Handle with care.

### Track B — Sensing Firmware

- **Resolve PA2/PA3 conflict first** — reassign Q1/Q2 GPIO to PB4/PB5 or similar. Verify in CubeMX. PA2/PA3 reserved for USART2 (ST-Link).
- 6 voltage dividers (33k/10k) → ADC. Calibrate tap subtraction against multimeter. Target: <10 mV error per cell.
- **INA226 bring-up:** Verify shunt resistor on module. If R100 (100 mΩ), replace with R010 or R020 BEFORE any >1A test. At 10A, R100 dissipates 10W and drops 1V.
- 6 NTC dividers + lookup table. Validate at room temp, ice water, warm water.
- 100 ms sensor loop + UART streaming — all 14 channels (6 voltage taps, 2 currents, 6 temperatures) as CSV at 10 Hz.

### Track A+B Integration

- Final GPIO pin map (locked in CubeMX, no further changes)
- All sensors reading while switches toggle — verify no cross-talk or ground bounce

**DoD:** All 4 switch pairs open/close cleanly. Reverse-current blocking confirmed both directions. All 14 sensor channels within error bounds. 100 ms loop logs clean CSV over UART. GPIO map finalized.

---

## Milestone 2 — Full BMS on Bench (Weeks 4–5)

**Goal:** Complete BMS with real cells and resistive load. Port simulation state machine to firmware. This is the core milestone.

### Tasks

- Connect 6 cells (holders) to switching + sensing stages. ALWAYS with fuse + kill switch.
- Port `bms_reconfig_pass5` state machine to STM32 firmware:
  - Discharge modes (MODE 0/1): INIT → PRE_A → SINGLE_A → PRE_B → BOTH → fault handling
  - Charging modes (MODE 2/3): sequential (reconfig) vs parallel (fixed)
  - All SOA checks, debounce, precharge, cooldown, open-wire/weld detection
- Implement safety rules in firmware:
  - ΔV < 100 mV or inrush < 6 A before paralleling
  - 500 ms cooldown between topology changes
  - MOSFET state readback (current confirms switch moved)
  - All-off default on power-up/reset (100k pull-downs enforce this in hardware)
  - Break-before-make on every transition

### Bench Test Scenarios

1. **Precharge + parallel reconnection** — mismatched string SOC, measure inrush, compare to M0 prediction (1.09 A)
2. **Thermal fault (THE demo)** — heat one cell with heat gun → string disconnects → load continues on healthy string. Practice until reliable 3× consecutive.
3. **SOC disconnect** — run one string low → controlled switchover. Do NOT claim energy advantage — the value is safe string management.
4. **Charging** — sequential charge with imbalanced strings. Measure inter-string current in fixed mode for the scaling argument.
5. **Compare bench results to M0 simulation** — document every deviation.

### EKF Decision Point

If comfortable by mid-week-5, add per-cell EKF (coulomb predict + OCV correct). M0 measured 5% → 5.06% drift after 10 min — acceptable for POC. If tight on time, ship coulomb counting and note EKF as future work. **Do NOT let EKF threaten the fault demo.**

### Stretch Goals (only if ahead of schedule)

- Wear-leveling alternation (2-min swaps, scope bus during gap)
- CAN bus telemetry through SN65HVD230

**DoD:** Every scenario passes without manual intervention. No unsafe state reachable. Kill switch never needed for a logic fault. Fault isolation works repeatably 3× consecutive. Bench results compared to simulation with deviations documented.

---

## Milestone 3 — Rover Integration (Week 6)

### Track 3a (Parallel During Weeks 2–5)

- Assemble chassis, motors, BTS7960, RC receiver
- **Order second BTS7960** — 4WD differential steering needs 2, BOM has 1
- **Confirm motor spec** — TT plastic gearmotors (~0.25 A each, ~1 A total) won't stress-test anything. Need 12V metal gearmotors (~1–1.5 A each, ~4–6 A total) for meaningful load.
- Verify rover drives on bench supply before BMS integration

### Track 3b (Week 6)

- Mount BMS zero PCB + cell holders on chassis
- Wire load path through fuse + kill switch to BTS7960 motor driver
- Verify BMS survives real motor load: startup inrush, direction-change spikes, incline current
- **Tune OC threshold if needed** — motor stall current may exceed 12 A per string briefly. Distinguish transient spikes from sustained overcurrent (increase debounce count or add a separate instantaneous limit). False triggers here ruin the demo.
- Full runs on flat ground + ramp with live UART telemetry

**DoD:** Rover completes a 10-min mixed-terrain run. Zero unintended topology changes. Correct response to induced fault (heat gun on cell mid-run).

---

## Milestone 4 — Validation + Demo Prep (Week 7)

**Goal:** The controlled comparison data that proves the thesis, plus a rehearsed demo.

### Tasks

- Precondition pack with deliberate SOC imbalance (bench supply, per-cell charge)
- **Run A:** BMS locked in fixed 3S2P (MODE 0). Same route, same load.
- **Run B:** BMS fully active (MODE 1). Same route, same load.
- Metrics: runtime, Wh extracted, max cell temp, end SOC spread, faults survived
- **Run C:** Charging comparison — MODE 2 (fixed parallel) vs MODE 3 (sequential). Measure inter-string current.
- **Rehearse heat-gun fault demo until it works 3× in a row** — this is what the panel remembers
- Build laptop dashboard: per-cell V/T/SOC + active topology indicator, live
- Script the 10–15 min demo sequence; build the ramp if needed

### What to Compare (Revised from M0 Learnings)

| Claim | Metric | Expected |
|---|---|---|
| Zero overhead when healthy | Wh, matched cells | Identical (S1 confirmed) |
| Fault isolation | Peak cell temp, continued operation | Decisive advantage (S2 confirmed) |
| Safe reconnection | Inrush current at join | Controlled vs uncontrolled |
| Independent charging | Inter-string current, final SOC spread | Safety advantage, not energy |

**Do NOT claim:** measurable energy gain from SOC imbalance (S3 disproved), SOC uniformity advantage from charging at 2P (S4 — self-balancing masks it).

**DO claim:** fault isolation, graceful degradation, safe joining, elimination of uncontrolled inter-string charging current, and scaling argument for 4P+.

**DoD:** Fault demo reliable 3× consecutively. Comparison data collected with clear labels (bench-measured, not simulated). Dashboard working live.

---

## Milestone 5 — Report + Buffer (Week 8)

### Tasks

- Final report structure:
  - Problem statement + literature (cite Tang et al. for body-diode discussion)
  - Design: architecture, component selection with datasheet citations
  - **Corrections journey** — body-diode fix, load switch discovery, bleed resistor bug, current source polarity, open-wire false positive, duplicate params files. These are real engineering stories, not embarrassments.
  - Simulation results (S1–S4) with honest negative results prominently reported
  - Bench results — clearly separated from simulation
  - Rover results — clearly separated from bench
  - **Honest limitations:** soft degradation handled worse, coulomb counter drift, no thermal model, capacity-fade lockout gap, ADC channel limit prevents >2P scaling without external MUX
  - **Future scope:** custom PCB, EKF, module-level scaling (3S3P/4P), second-life cell testing, CAN integration
- Presentation deck + poster if required
- Publish firmware + Simulink repo with README
- **Absorb whatever slipped** — this week is deliberate buffer

### Result Labeling (Non-Negotiable)

Every number in the report must carry one of: `[SIMULATED]`, `[BENCH-MEASURED]`, `[ROVER-MEASURED]`, `[CALCULATED]`, `[CITED from source]`. No unlabeled numbers.

**DoD:** Report submitted. Demo delivered. All results labeled by source.

---

## Timeline Summary

| Week | Milestone | Parallel Track |
|---|---|---|
| 1 | M0 Simulation ✅ COMPLETE | Rover parts arriving |
| 2–3 | M1 Switching + Sensing | Rover chassis build (3a) |
| 4–5 | M2 Full BMS on bench | Rover drive-test on bench supply |
| 6 | M3 Rover integration | — |
| 7 | M4 Validation + demo | — |
| 8 | M5 Report + buffer | — |

## Risk Buffer Rules

- **Nucleo late → everything slips.** Have backup source identified.
- **Fault demo is sacred.** Sacrifice alternation → EKF → charging demo → SOC comparison, in that order.
- **If M2 slips past week 5:** Demo fault isolation on bench with resistive load. It proves every claim except motor transients. Still a valid capstone.
- **Zero MOSFET spares.** Handle IRF4905s carefully. One dead MOSFET = order and wait.

## BOM Action Items (Before M1 Starts)

1. ~~Order second BTS7960~~ — verify if ordered
2. Confirm motor spec — TT vs metal gearmotor
3. Verify INA226 shunt resistor value on the modules in hand
4. Confirm all 8 IRF4905 and 5 × 2N2222A are in hand
5. Have 10 Ω power resistor (precharge) and 1000 µF cap ready

## What Got Cut (State in Report as Future Scope)

- Custom PCB — zero PCB is valid demo platform
- Full EKF — coulomb counting proves the concept
- 300-cycle wear-leveling simulation — cite published data instead
- 3S3P/4P hardware — ADC channel limit; demonstrate in simulation only
- Deliberate wear-leveling alternation — SOC split provides incidental alternation
