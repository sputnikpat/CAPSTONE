# Capstone Milestone Plan — Reconfigurable BMS for Rover (8-Week Compressed)

> Timeline: 8 working weeks. Experienced builder, aggressive but achievable. Custom PCB dropped (future scope). Everything runs on zero PCB. Each milestone has a **Definition of Done (DoD)** — do not advance until met. Hardware and rover tracks run in parallel wherever possible.

> **The one rule that governs everything:** Protect the fault-isolation demo. If time slips, sacrifice wear-leveling alternation and full EKF before you touch the fault demo. That is the undeniable claim — traditional BMS dies, ours keeps running.

---

## Week 0 (Day 1) — Procurement, before anything else

- **Order the NUCLEO-F446RE from Mouser India TODAY.** Longest lead time, single most schedule-critical item. If it arrives late, weeks 2-5 all slip.
- Order MOSFETs (8x IRF4905), 2N2222A, all resistors, INA226 x3, NTCs, cells, buck, connectors — most are local/fast.
- Order rover chassis + BTS7960 + RC in parallel.
- Pull the **real INR18650-30Q datasheet** — capacity, internal resistance, OCV-vs-SOC curve. Do not model from memory.

---

## Milestone 0 — Simulation (Week 1)

**Goal:** Prove the concept in MATLAB/Simulink before committing to hardware logic. One week — you're experienced, don't gold-plate it.

**Tasks:**
- 6-cell first-order RC (Thevenin) model using real INR18650-30Q datasheet parameters
- Model 3S2P with ideal string switches (Q1/Q2) + precharge path
- Rule-based state machine: SOA triggers, SOC triggers, precharge sequence, cooldown timer
- Inject cell mismatch (±10% capacity, ±20% internal resistance)
- Three comparison scenarios, fixed vs reconfigurable:
  1. Matched cells, normal load — **honest baseline (expect near-identical or slightly worse for ours due to switching overhead)**
  2. One cell fault at 50% mission — fixed shuts down; ours continues on one string
  3. 15% SOC imbalance — ours extracts more usable energy
- **SOC method: coulomb counting only.** Skip EKF here — it's not needed to prove topology advantage. Add EKF later only if time allows.

**Deliverables:** Simulink model, comparison plots, validated firmware thresholds (voltage/temp/current triggers, cooldown timing) in a table.

**DoD:** Plots clearly show fault-tolerance and imbalance advantages. Baseline shows NO magic improvement (if it does, the model is wrong). **Exit gate: if no meaningful advantage appears, stop and re-scope before building.**

---

## Milestone 1 — Switching + Sensing in Parallel (Weeks 2-3)

**Goal:** Both subsystems working on breadboard/zero PCB. Two tracks run together — you're comfortable with the hardware.

### Track A — Switching
- Breadboard one NPN level shifter + **back-to-back IRF4905 pair** (common-source, body-diode blocking)
- **Body-diode test (critical):** bench supply set above AND below "string" voltage — confirm the pair blocks BOTH directions when off. A single MOSFET fails this; the pair must pass.
- Solder full switching stage on zero PCB: Q1 pair, Q2 pair, Q3 pair, 3 level shifters, precharge resistor, bulk cap, fuse, kill switch
- Scope switching transients with resistive load (no cells yet) — check for shoot-through/ringing

### Track B — Sensing
- Resolve GPIO pin conflicts in STM32CubeMX (PA2/PA3 USART2 clash)
- 6 voltage dividers (33k/10k, 1%) → ADC; implement **tap-subtraction** cell voltage calc in firmware
- Calibrate against multimeter across 3.0-4.2V; error < 10mV per cell
- **Verify INA226 shunt value — replace if R100 (100mΩ)** before any 10A test
- Bring up I2C for both INA226s; validate current at 1A, 5A
- 6 NTC dividers + lookup table; validate at room temp, ice water, warm
- 100ms sensor loop + UART streaming to laptop + Python logger

**DoD:** All 3 switch pairs open/close cleanly; reverse-current blocking confirmed both directions; all 14 sensor channels within error bounds; 100ms loop logs clean CSV.

---

## Milestone 2 — Full BMS on Bench (Weeks 4-5)

**Goal:** Complete BMS on zero PCB with real cells and resistive load. This is the core — give it the most time.

**Tasks:**
- Connect 6 cells (holders) to switching + sensing stages — ALWAYS with fuse + kill switch
- Port simulation state machine to firmware: normal, fault isolation, SOC disconnect, charging, alternation
- Implement all firmware safety rules: ΔV check before paralleling, 500ms cooldown, MOSFET state readback, all-off default
- **EKF decision point:** if you reach here comfortably by mid-week-5, add per-cell EKF (coulomb predict + OCV correct). If tight, ship coulomb counting and note EKF as future work. Do NOT let EKF threaten the fault demo.
- Bench-test every scenario:
  - Precharge + parallel reconnection with mismatched string SOC
  - **Thermal fault: heat one cell → string disconnects, load continues** (this is THE demo)
  - SOC disconnect: run one string down → switchover
  - Alternation: 2-min swaps, scope bus during gap (capacitor sag vs prediction)
- Compare bench results to M0 simulation, document deviations

**DoD:** Every scenario passes without manual intervention; no unsafe state reachable; kill switch never needed for a logic fault. **Fault isolation works repeatably — this is non-negotiable.**

---

## Milestone 3 — Rover Integration (Week 6)

**Track 3a (runs parallel during Weeks 2-5):** Assemble chassis, motors, BTS7960, RC. Verify rover drives on bench supply. Should be DONE before week 6 starts.

**Track 3b (Week 6):**
- Mount BMS zero PCB + cell holders on chassis; wire load path through fuse + kill switch to motor driver
- Verify BMS survives real motor load: startup inrush, direction-change spikes, incline current
- **Tune thresholds for motor transients** — distinguish transient spikes from sustained overcurrent (false triggers here will ruin the demo)
- Full runs on flat ground + ramp with live UART telemetry

**DoD:** Rover completes a 10-min mixed-terrain run, zero unintended topology changes, correct response to induced events.

---

## Milestone 4 — Validation + Demo Prep (Week 7)

**Goal:** The controlled comparison data that proves the thesis, plus a rehearsed demo.

**Tasks:**
- Precondition pack with deliberate 15% SOC imbalance (bench supply, per-cell)
- Run A: BMS locked in fixed 3S2P (traditional emulation). Run B: BMS fully active. Same route, same load.
- Metrics: runtime, Wh extracted, max cell temp, end SOC spread, faults survived
- **Rehearse the heat-gun fault demo until it works 3 times in a row** — this is what the panel remembers
- Build laptop dashboard: per-cell V/T/SOC + active topology, live
- Script the 10-15 min demo sequence; build the ramp

**DoD:** Reconfigurable run shows measurable runtime/energy gain on imbalanced pack; fault demo reliable 3x consecutively.

---

## Milestone 5 — Report + Buffer (Week 8)

**Tasks:**
- Final report: problem, literature (incl. body-diode/RS-pair discussion citing Tang et al.), design, **corrections journey (v1→v2 body-diode + divider fixes = strong content)**, sim + bench + rover results clearly separated, honest limitations, future scope (custom PCB, EKF, module-level scaling, 3S3P)
- Presentation deck + poster if required
- Publish firmware/Simulink repo with README
- **Absorb whatever slipped** from earlier weeks — this is deliberate buffer

**DoD:** Report submitted; demo delivered; results labeled by source (simulated / bench / rover / cited).

---

## Timeline Summary

| Week | Milestone | Parallel Track |
|---|---|---|
| 0 (Day 1) | Order Nucleo + all parts | — |
| 1 | M0 Simulation | Rover parts arriving |
| 2-3 | M1 Switching + Sensing | Rover chassis build (3a) |
| 4-5 | M2 Full BMS on bench | Rover drive-test on bench supply |
| 6 | M3 Rover integration | — |
| 7 | M4 Validation + demo | — |
| 8 | M5 Report + buffer | — |

---

## What Got Cut vs the 28-Week Plan (state honestly in report as future scope)

- **Custom PCB** — zero PCB is a valid demo platform; PCB is future work
- **Full EKF** — coulomb counting proves the concept; EKF is an enhancement (add if week 5 allows)
- **300-cycle wear-leveling simulation** — cite published data (Cârstoiu et al. 415%) instead of running it
- **Extended demo spare board** — one working setup, tested thoroughly

## Risk Buffer Rules

- **Nucleo late → everything slips.** Order day one. Have a backup source (Robu/Amazon) identified.
- **Fault demo is sacred.** Sacrifice alternation, then EKF, before touching it.
- **If M2 slips past week 5:** demo the fault isolation on bench with resistive load — it proves every claim except motor transients. Still a valid capstone.
- **Baseline sanity check in M0:** matched-cell scenario must NOT show big gains, or the model is lying.
