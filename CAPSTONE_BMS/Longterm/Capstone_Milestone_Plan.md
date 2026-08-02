# Capstone Milestone Plan — Reconfigurable BMS for Rover

> Timeline assumes a standard two-semester capstone (~28 working weeks). Durations are estimates — adjust to your academic calendar. Each milestone has a **Definition of Done (DoD)**: do not move forward until it's met. Milestones 1–2 and the rover platform work (M5a) can run in parallel with other tracks.

---

## Milestone 0 — Simulation Validation (Weeks 1–4)

**Goal:** Prove the concept in MATLAB/Simulink before spending money on hardware.

**Tasks:**
- Build a 6-cell battery model using first-order RC (Thevenin) equivalent circuit per cell (parameters from INR18650-30Q datasheet: 3Ah, ~20mΩ)
- Model the 3S2P topology with ideal switches for Q1/Q2 (string connect/disconnect) and the precharge path
- Implement the rule-based decision engine as a Simulink state machine: SOA triggers, SOC triggers, precharge sequence, cooldown timer
- Introduce deliberate cell parameter mismatch (±10% capacity, ±20% internal resistance) to reflect published cell-to-cell variation data
- Run three comparison scenarios, each as fixed-topology vs reconfigurable:
  1. Normal operation with matched cells (expect: near-identical performance — the honest baseline)
  2. One cell fault at 50% mission time (expect: fixed = full shutdown; ours = continue on one string)
  3. 15% SOC imbalance between strings (expect: 15–30% more energy extracted with reconfiguration)
- Simulate the precharge transient: verify equalization time (~100–200ms) and peak current (<0.15A) with 10Ω resistor
- Simulate 300+ cycle wear-leveling comparison (alternation vs fixed) showing SOH divergence

**Deliverables:** Simulink model files, comparison plots (runtime, per-cell SOC curves, energy extracted), threshold values validated for firmware (voltage/temp/current triggers, cooldown timing)

**DoD:** Plots clearly show fault-tolerance and imbalance advantages; simulated thresholds documented in a table ready for firmware use. If simulation shows no meaningful advantage, STOP and re-scope before buying anything.

---

## Milestone 1 — Procurement + Switching Circuit Prototype (Weeks 4–8)

**Goal:** All components in hand; switching circuit proven on breadboard/zero PCB.

**Tasks:**
- Order full BOM (see checklist v2) — Nucleo from Mouser India first (longest lead time)
- **Apply the body-diode correction:** build Q1 and Q2 as back-to-back IRF4905 pairs (common-source); order 8 MOSFETs total (6 + 2 spares)
- Breadboard one NPN level shifter + back-to-back MOSFET pair; verify with multimeter:
  - GPIO high → pair conducts; GPIO low → pair blocks BOTH directions (test with reversed supply)
  - Confirm blocking with a bench supply set above and below "string" voltage — this is the body-diode test
- Solder the full switching stage on zero PCB: Q1 pair, Q2 pair, Q3 pair, 3 level shifters, precharge resistor, bulk capacitor, fuse, kill switch
- Test with bench supply + resistive load (NOT cells yet): switching sequences, precharge timing, no shoot-through (oscilloscope)

**Deliverables:** Working switching stage on zero PCB; bench test log with scope captures of switching transients and body-diode blocking verification

**DoD:** All three switch pairs open/close cleanly on command; reverse-current blocking confirmed in both directions; precharge path limits current as designed.

---

## Milestone 2 — Sensing Subsystem + Base Firmware (Weeks 7–11, overlaps M1)

**Goal:** STM32 reads all 14 sensor channels accurately.

**Tasks:**
- Resolve GPIO pin conflicts in STM32CubeMX (PA2/PA3 USART2 clash noted in checklist)
- Wire 6 voltage dividers (33k/10k, 1%) to ADC; implement tap-subtraction cell voltage calculation in firmware
- Calibrate: compare firmware cell voltages against multimeter across 3.0–4.2V range; error must be <10mV per cell
- Verify INA226 module shunt value; replace onboard shunt if it is R100 (100mΩ) — required before any 10A test
- Bring up I2C for both INA226s (distinct addresses); validate current readings against bench meter at 1A, 5A
- Wire 6 NTC dividers; build lookup table; validate at room temp, ice water, and warm (heat gun at distance)
- Implement 100ms sensor loop + UART streaming to laptop; write Python logger/plotter

**Deliverables:** Firmware repo (sensor drivers + logging), calibration report table, live plotting script

**DoD:** All 14 channels within stated error bounds; 100ms loop timing verified; data logs cleanly to CSV.

---

## Milestone 3 — Full BMS Logic on Bench (Weeks 11–16)

**Goal:** Complete BMS running on zero PCB with real cells and resistive load — the core of the project.

**Tasks:**
- Connect 6 cells (in holders) to the switching + sensing stages; ALWAYS with fuse and kill switch in circuit
- Port simulation state machine to firmware: normal mode, fault isolation, SOC disconnect, charging mode, alternation mode
- Implement EKF SOC estimation per cell (coulomb counting predict + OCV correct); tune against a full discharge log
- Implement all firmware safety rules: ΔV check before paralleling, 500ms cooldown, MOSFET state readback verification, all-off default
- Bench-test every scenario from the simulation matrix:
  - Precharge + parallel reconnection with deliberately mismatched string SOC
  - Thermal fault: heat one cell → string disconnects, load continues on other string
  - SOC disconnect: run one string down → automatic switchover
  - Alternation: 2-min swaps under load; scope the bus during the gap (capacitor sag vs prediction)
- Compare bench measurements to Milestone 0 simulation; document deviations

**Deliverables:** Full firmware, bench validation report (scenario × result table), sim-vs-real comparison

**DoD:** Every scenario passes on bench without manual intervention; no unsafe state reachable in testing; kill switch never needed for a logic fault.

---

## Milestone 4 — Custom PCB (Weeks 15–20, overlaps M3 tail)

**Goal:** Design, fabricate, and assemble the final PCB.

**Tasks:**
- KiCad schematic from the validated zero PCB design (including back-to-back pairs and all v2 corrections)
- Layout: 2-layer, 2oz copper, ≥3mm power traces, thermal vias under TO-220 pads, ground pour, test points on every tap and bus
- Design review against checklist v2 PCB section; peer/faculty review before ordering
- Order from JLCPCB (5 boards); assemble one; bring-up test repeating key Milestone 3 scenarios on the new board
- Assemble a second board as demo spare

**Deliverables:** KiCad project, fabricated + assembled boards, PCB bring-up test log

**DoD:** PCB passes the same scenario suite as the zero PCB; thermal check at sustained 5A shows MOSFET case temps within predicted range.

---

## Milestone 5 — Rover Integration (Weeks 19–23)

**Track 5a (can start as early as Week 8, parallel):** Assemble chassis, motors, BTS7960, RC control; verify rover drives on a bench supply.

**Track 5b (needs M4):**
- Mount BMS PCB + cell holders on chassis; wire load path through fuse + kill switch to motor driver
- Verify BMS handles real motor load: startup inrush, direction-change spikes, incline current
- Confirm no false triggers from motor transients (tune thresholds if needed — transient vs sustained overcurrent)
- Full mission runs on flat ground + ramp with live UART telemetry

**Deliverables:** Integrated rover, telemetry logs from real driving

**DoD:** Rover completes a 10-minute mixed-terrain run with zero unintended topology changes and correct responses to induced events.

---

## Milestone 6 — Validation Experiments + Demo Prep (Weeks 23–26)

**Goal:** The controlled comparison data that proves the thesis.

**Tasks:**
- Precondition pack with a deliberate 15% SOC imbalance (bench supply, per-cell)
- Run A: BMS locked in fixed 3S2P (traditional emulation). Run B: BMS fully active. Same route, same load profile
- Metrics: total runtime, total Wh extracted, max cell temp, end-of-run SOC spread, faults survived
- Live-fault demo rehearsal: heat gun on one cell mid-run → string isolates → rover keeps driving
- Prepare demo display: laptop dashboard showing per-cell V/T/SOC + active topology in real time
- Build the demo ramp; script the 10–15 min demo sequence

**Deliverables:** Comparison dataset + charts, rehearsed demo, dashboard

**DoD:** Reconfigurable run shows measurable runtime/energy gain on the imbalanced pack; fault demo works reliably 3 times in a row.

---

## Milestone 7 — Documentation + Final Report (Weeks 25–28, overlaps M6)

**Tasks:**
- Final report: problem, literature (incl. body-diode/RS-pair discussion citing Tang et al.), design, corrections journey (v1→v2 is good content), sim + bench + rover results, honest limitations, future scope (module-level scaling, 3S3P, second-life cells)
- Presentation deck + poster (if required)
- Clean and publish firmware/KiCad/Simulink repo with README
- Optional: draft IEEE conference paper from the validation data

**DoD:** Report submitted; demo delivered; repo reproducible by a stranger.

---

## Timeline Summary

| Weeks | Milestone | Parallel work |
|---|---|---|
| 1–4 | M0 Simulation | — |
| 4–8 | M1 Switching prototype | Order everything Week 4 |
| 7–11 | M2 Sensing + firmware | Overlaps M1 |
| 8+ | M5a Rover chassis build | Anytime after ordering |
| 11–16 | M3 Full BMS on bench | — |
| 15–20 | M4 Custom PCB | Overlaps M3 tail |
| 19–23 | M5b Rover integration | — |
| 23–26 | M6 Validation + demo | — |
| 25–28 | M7 Documentation | Overlaps M6 |

## Risk Buffer Rules

- If M0 shows weak results → re-scope before spending (this is the cheapest exit)
- If M4 PCB fails bring-up → demo on zero PCB (M3 board is a valid fallback demo)
- If M5b integration slips → bench demo with resistive load still proves every claim except motor transients
- Single most schedule-critical item: Nucleo board delivery — order Week 4, day one
