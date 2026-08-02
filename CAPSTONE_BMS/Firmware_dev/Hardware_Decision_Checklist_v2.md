# Hardware Decision Checklist — Reconfigurable BMS Project (v2 — Corrected)

> **How to use this:** Work top to bottom. Each priority level depends on decisions made above. Do not purchase anything from a lower priority until all decisions in higher priorities are locked.

---

## PRIORITY 0 — Core Decisions (LOCKED)

### Battery Cell Selection
- [x] Chemistry: Li-ion (18650)
- [x] Cell count: 6 cells (3S2P nominal)
- [x] Cell model: Samsung INR18650-30Q (3Ah, 15A continuous, 20mΩ)
- [x] Pack voltage: 11.1V nominal (9.0V depleted, 12.6V fully charged)
- [x] Pack capacity: 6Ah in 3S2P (66.6 Wh total)
- [x] **LOCKED DECISION:** Samsung INR18650-30Q

### Sensing Architecture
- [x] Approach: Fully discrete
- [x] Voltage: Resistor dividers into STM32 ADC
- [x] Current: INA226 modules (I2C)
- [x] Temperature: NTC 10K thermistors into STM32 ADC
- [x] Protection: All in firmware + hardware comparator backup (optional)
- [x] **LOCKED DECISION:** Discrete sensing (no battery monitor IC)

### MCU Selection
- [x] MCU: STM32F446RE (Nucleo-64 board)
- [x] Built-in CAN peripheral
- [x] 3 ADC units, 16 channels each (12 needed: 6 voltage + 6 temperature)
- [x] 50+ GPIOs (only ~20 needed)
- [x] 180MHz clock (comfortable for EKF calculations)
- [x] **LOCKED DECISION:** STM32F446RE
- [ ] Purchase from: Mouser India (authorized STM distributor)

---

## PRIORITY 1 — Switching Hardware (LOCKED)

### P-Channel MOSFET Selection
- [x] MOSFET: IRF4905 (P-channel, TO-220)
- [x] Specs: Vds = -55V, Id = -74A, RDS(on) = 20mΩ
- [x] Thermal check at 5A cruise: I²R = 0.5W, rise = 31°C (fine without heatsink)
- [x] Thermal check at 10A peak: I²R = 2W, rise = 124°C WITHOUT heatsink (NEEDS heatsink)
- [x] With clip-on TO-220 heatsink (~20°C/W): rise = 40°C (comfortable)
- [x] **LOCKED DECISION:** IRF4905 + TO-220 clip-on heatsinks
- [x] Quantity needed: 3 (Q1 for String A + Q2 for String B + Q3 for precharge)
- [ ] Order: 5 total (2 spares)

### NPN Level Shifter (per P-MOSFET, 3 sets)
- [x] NPN transistor: 2N2222A (TO-92, through-hole)
- [x] R1 — Base resistor: 1kΩ 1/4W (limits GPIO current to ~3mA)
- [x] R2 — Gate pull-up resistor: 10kΩ 1/4W (pulls P-FET gate to battery+ when off)
- [x] **R3 — Base pull-down resistor: 100kΩ 1/4W (CRITICAL SAFETY: keeps NPN off during MCU boot/reset/brownout, prevents accidental MOSFET turn-on)**
- [x] Quantity: 3 sets of (1× 2N2222A + 1× 1kΩ + 1× 10kΩ + 1× 100kΩ)
- [ ] Order: 5 sets (2 spare)

### Precharge Circuit
- [x] Precharge MOSFET: Same IRF4905 (Q3), controlled by its own NPN level shifter
- [x] Precharge resistor: 10Ω, 2W wire-wound (handles 0.12A initial, max 1.2W briefly)
- [x] Placement: In parallel with Q2 (between + bus and String B positive)
- [x] Sequence: Close Q3 → current trickles through 10Ω → wait for ΔV < 0.1V → close Q2 → open Q3
- [x] Timeout: If ΔV doesn't drop below 0.1V within 5 seconds, abort and flag error
- [ ] Order: 2× 10Ω 2W resistors (1 spare)

### Bulk Capacitor
- [x] Value: 4700µF, 25V electrolytic (radial, through-hole)
- [x] Purpose: Holds bus voltage during 1-2ms string alternation switchover
- [x] Sag calculation: At 5A load, ΔV = I×t/C = 5 × 0.002 / 0.0047 = 2.1V sag (11.1V → 9.0V)
- [x] STM32 buck converter input range: 4.5-40V — sag to 9V is safe, MCU unaffected
- [ ] Order: 2 (1 spare)

### Heatsinks
- [x] Type: TO-220 clip-on aluminium heatsink
- [x] Thermal resistance: ~20°C/W target
- [x] Quantity: 3 (one per IRF4905)
- [ ] Order: 5 (2 spare)

---

## PRIORITY 2 — Sensing Hardware

### Voltage Sensing (6 cell taps)
- [x] Method: Resistor voltage dividers from each cell tap to STM32 ADC
- [x] **CORRECTED values: 33kΩ top + 10kΩ bottom (ratio = 10/43 = 0.233)**
- [x] Worst case: Tap 3 at 12.6V × 0.233 = 2.93V (within 3.3V ADC max)
- [x] Tolerance: 1% metal film resistors (NOT 5% carbon film)
- [x] ADC resolution: 12-bit = 3.3V/4096 = 0.8mV per step
- [x] **IMPORTANT: Dividers measure TAP voltage relative to ground, NOT individual cell voltage**
- [x] **Cell voltage computed in firmware: V_cell1 = V_tap3 - V_tap2, etc.**
- [x] Worst-case cell voltage error from subtraction: ~5mV (adequate for 50-100mV fault detection)
- [ ] Order: 6× 33kΩ 1% + 6× 10kΩ 1% metal film resistors (buy 10 of each for spares)

### Current Sensing (2 strings)
- [x] Sensor: INA226 breakout module (comes with onboard shunt resistor)
- [x] Module shunt: Typically R100 = 100mΩ on most breakout boards
- [ ] **CHECK: Verify shunt value on your specific module. If R100 (100mΩ), at 10A = 1V drop and 10W dissipation — TOO HIGH. May need to replace onboard shunt with 5mΩ for high current, or buy a module with appropriate shunt for your current range**
- [x] Interface: I2C (2 devices on same bus, different addresses via A0/A1 pins)
- [x] ADC channels used: 0 (I2C only, no ADC pins consumed)
- [x] Placement: One in each string's positive current path, between Q1/Q2 drain and + bus
- [ ] Order: 3× INA226 modules (1 spare)

### Temperature Sensing (6 cells)
- [x] Sensor: NTC 10K thermistor (bead type with wire leads)
- [x] Pull-up resistor: 10kΩ to 3.3V rail (one per thermistor)
- [x] ADC channels used: 6
- [x] Mounting: Thermistor bead taped/epoxied directly onto each cell body with Kapton tape
- [x] Firmware: Pre-calculated lookup table (resistance → temperature) for specific NTC model
- [x] Temperature thresholds: Warning at 45°C, disconnect string at 50°C, rate alarm at >2°C/min
- [ ] Order: 8× NTC 10K thermistors (2 spare) + 8× 10kΩ 1% resistors

---

## PRIORITY 3 — Power Supply and Safety

### BMS Board Power Supply
- [x] Buck converter: LM2596 module (input 4.5-40V, output adjustable, 3A)
- [x] Set output to 5V for CAN transceiver and any 5V peripherals
- [x] Secondary regulation: AMS1117-3.3 LDO (5V to 3.3V) for sensors and NPN pull-ups
- [x] OR: Power STM32 Nucleo via its VIN pin (accepts 7-12V from battery bus directly — has onboard regulator)
- [x] Input: Tapped directly from battery + bus (always powered when any string is connected)
- [ ] Order: 1× LM2596 module + 1× AMS1117-3.3 (if not using Nucleo's onboard regulator)

### Safety Hardware
- [x] Fuse: 15A resettable polyfuse on + bus output (before load)
- [x] Kill switch: XT60 loop key OR large toggle switch on main + bus
- [x] **Firmware safety checks (MANDATORY):**
  - [x] Never close both Q1 and Q2 simultaneously unless ΔV < 0.1V confirmed by sensors
  - [x] Default state at power-up: all MOSFETs OFF (guaranteed by 100kΩ base pull-downs)
  - [x] Verify MOSFET state after every switching operation via voltage/current sensor readback
  - [x] 500ms cooldown timer between any topology changes (anti-oscillation)
- [ ] Order: 1× polyfuse 15A + 1× XT60 male/female pair for kill switch loop

---

## PRIORITY 4 — Communication and Interface

### Data Logging (MANDATORY for demo)
- [x] USB-UART via STM32 Nucleo's onboard ST-Link (no extra hardware needed)
- [x] Connect Nucleo to laptop via USB, serial data streams to terminal/Python script
- [x] Log: timestamp, 6× cell voltage, 2× string current, 6× temperature, SOC per cell, active topology

### Optional Telemetry
- [ ] OLED display: SSD1306 128×64 I2C (shows cell voltages and active string on the rover itself)
- [ ] SD card module: SPI interface, for long-duration logging without laptop
- [ ] ESP32 as WiFi telemetry bridge: receives data from STM32 via UART, serves web dashboard

### CAN Bus (for vehicle integration, optional for POC)
- [ ] CAN transceiver: SN65HVD230 or MCP2551 module
- [ ] 120Ω termination resistors (2, one at each end of bus)
- [ ] Not needed for initial bench testing or basic demo — UART to laptop is sufficient

---

## PRIORITY 5 — PCB Design (after zero PCB prototype validates logic)

### PCB Specifications
- [ ] Tool: KiCad (free, open source)
- [ ] Layers: 2-layer (sufficient for this project)
- [ ] Copper weight: 2oz on both layers (for power traces)
- [ ] Power trace width: Minimum 3mm for 10A paths (use online calculator to verify)
- [ ] Signal trace width: 0.25mm for ADC, I2C, GPIO lines
- [ ] Board size: Estimate ~80mm × 100mm based on component count
- [ ] MOSFETs: TO-220 footprint with thermal pad connected to bottom copper pour via thermal vias
- [ ] Connectors:
  - [ ] Screw terminals or XT30 for each cell pair (battery connections)
  - [ ] XT60 for load output
  - [ ] JST-XH for thermistor connections
  - [ ] Pin headers for Nucleo board mounting or connection
  - [ ] SWD header for programming (if not using Nucleo's onboard ST-Link)
- [ ] Design checks before ordering:
  - [ ] No power trace narrower than 3mm
  - [ ] 100kΩ pull-down on every NPN base
  - [ ] Decoupling cap (100nF) on every IC power pin
  - [ ] Ground pour on bottom layer
  - [ ] Silkscreen labels on every component
  - [ ] Test points on: each cell tap, each string current path, + bus, - bus
- [ ] Fabrication: JLCPCB or PCBWay (5-7 day turnaround, ~₹500 for 5 boards)

---

## PRIORITY 6 — Rover Platform (can start in parallel with Priority 2-3)

### Chassis and Drive
- [ ] 4WD chassis kit with DC gear motors (verify motor voltage rating ~12V)
- [ ] Motor driver: BTS7960 module (43A max — robust, handles startup current spikes)
- [ ] Remote control: FlySky FS-i6 RC transmitter + receiver (simplest, most reliable)
- [ ] Alternative RC: ESP32 + Bluetooth joystick app (cheaper, less reliable range)
- [ ] Mounting: Drill holes or use standoffs to mount BMS board and battery holder on chassis
- [ ] Demo ramp: Piece of plywood at 15-30° angle for incline test

---

## PRIORITY 7 — Test Equipment and Consumables

### Test Equipment (borrow from lab if possible)
- [ ] Multimeter — voltage and current verification at every tap point
- [ ] Oscilloscope — switching transient verification, check for ringing/shoot-through (CRITICAL for Milestone 1)
- [ ] Bench power supply — for charging individual cells to specific SOC levels (to set up imbalance tests)
- [ ] Resistive load bank — 10Ω 50W power resistors for bench testing before rover integration
- [ ] Heat gun — for simulating thermal fault during demo
- [ ] 18650 cell holders with solder tabs — for prototyping (spot welder not needed)

### Consumables
- [ ] Silicone wire: 14 AWG red + black (power, 2m each), 22 AWG (signal, 3m)
- [ ] Solder (60/40 or lead-free), flux, solder wick
- [ ] Zero PCB / perfboard (for prototype, minimum 10cm × 15cm)
- [ ] Heat shrink tubing (assorted sizes)
- [ ] Kapton tape (for insulating cell connections and mounting thermistors)
- [ ] Zip ties, M3 standoffs and screws
- [ ] Breadboard (for testing NPN level shifter circuit before soldering)
- [ ] Header pins (male and female, for module connections)

---

## GPIO Pin Assignment (STM32F446RE)

| Pin | Function | Connection |
|---|---|---|
| PA0 | GPIO output | Q1 NPN base (String A switch) |
| PA1 | GPIO output | Q2 NPN base (String B switch) |
| PA2 | GPIO output | Q3 NPN base (precharge switch) |
| PA3 | ADC1_IN3 | Cell tap 1 voltage divider |
| PA4 | ADC1_IN4 | Cell tap 2 voltage divider |
| PA5 | ADC1_IN5 | Cell tap 3 voltage divider |
| PA6 | ADC1_IN6 | Cell tap 4 voltage divider |
| PA7 | ADC1_IN7 | Cell tap 5 voltage divider |
| PB0 | ADC1_IN8 | Cell tap 6 voltage divider |
| PC0 | ADC2_IN10 | NTC thermistor cell 1 |
| PC1 | ADC2_IN11 | NTC thermistor cell 2 |
| PC2 | ADC2_IN12 | NTC thermistor cell 3 |
| PC3 | ADC2_IN13 | NTC thermistor cell 4 |
| PA8 | ADC2_IN? | NTC thermistor cell 5 |
| PA9 | ADC2_IN? | NTC thermistor cell 6 |
| PB8 | I2C1_SCL | INA226 × 2 (shared bus) |
| PB9 | I2C1_SDA | INA226 × 2 (shared bus) |
| PA11 | CAN1_RX | CAN transceiver (optional) |
| PA12 | CAN1_TX | CAN transceiver (optional) |
| PA2/PA3 | USART2 TX/RX | USB-UART via ST-Link (built into Nucleo) |

**Note:** Verify exact ADC channel mapping against STM32F446RE datasheet before PCB layout. Some pins may conflict with alternate functions. The Nucleo board reserves some pins for ST-Link — check the Nucleo user manual for pin availability.

**Note:** PA2/PA3 are listed twice — once for Q1/Q2 GPIO and once for USART2. This is a conflict. Reassign GPIO outputs to unused pins like PB4, PB5, PB6 to avoid USART2 conflict. Verify in STM32CubeMX before committing.

---

## Complete BOM with Corrected Values

| # | Component | Specification | Qty | Unit ₹ | Total ₹ | Source |
|---|---|---|---|---|---|---|
| 1 | 18650 cells | Samsung INR18650-30Q | 6 | 250 | 1,500 | Amazon IN |
| 2 | 18650 holders | Single cell holder with tabs | 6 | 25 | 150 | Amazon IN |
| 3 | P-MOSFET | IRF4905 (TO-220) | 5 | 50 | 250 | Amazon IN / Robu |
| 4 | NPN transistor | 2N2222A (TO-92) | 5 | 5 | 25 | Local / Amazon |
| 5 | Heatsink | TO-220 clip-on aluminium | 5 | 15 | 75 | Amazon IN |
| 6 | R — 1kΩ 1% | Metal film 1/4W | 10 | 1 | 10 | Local |
| 7 | R — 10kΩ 1% | Metal film 1/4W (pull-up + NTC + divider) | 20 | 1 | 20 | Local |
| 8 | R — 33kΩ 1% | Metal film 1/4W (voltage divider top) | 10 | 1 | 10 | Local |
| 9 | R — 100kΩ 1% | Metal film 1/4W (NPN base pull-down) | 5 | 1 | 5 | Local |
| 10 | R — 10Ω 2W | Wire wound (precharge) | 2 | 8 | 16 | Local |
| 11 | Capacitor | 4700µF 25V electrolytic | 2 | 40 | 80 | Local / Amazon |
| 12 | INA226 | Breakout module | 3 | 120 | 360 | Amazon IN / Robu |
| 13 | NTC thermistor | 10K bead type with leads | 8 | 15 | 120 | Amazon IN |
| 14 | STM32 Nucleo | NUCLEO-F446RE | 1 | 1,600 | 1,600 | Mouser India |
| 15 | Buck converter | LM2596 module | 1 | 70 | 70 | Amazon IN |
| 16 | Polyfuse | 15A resettable | 2 | 30 | 60 | Amazon IN |
| 17 | XT60 connector | Male + female pair (kill switch) | 2 | 40 | 80 | Amazon IN |
| 18 | XT30 connector | Male + female pair (cell connections) | 6 | 25 | 150 | Amazon IN |
| 19 | Zero PCB | 10cm × 15cm perfboard | 2 | 40 | 80 | Local |
| 20 | Wire | 14 AWG silicone (2m red, 2m black) | 4m | 15/m | 60 | Amazon IN |
| 21 | Wire | 22 AWG signal wire (3m assorted) | 3m | 8/m | 24 | Amazon IN |
| 22 | Misc | Solder, flux, heat shrink, Kapton, zip ties | 1 set | 200 | 200 | Local |
| | | | | | | |
| 23 | BTS7960 | Motor driver module | 1 | 400 | 400 | Amazon IN |
| 24 | 4WD chassis | Chassis kit with DC gear motors | 1 | 1,500 | 1,500 | Amazon IN / Robu |
| 25 | RC controller | FlySky FS-i6 TX + RX | 1 | 3,500 | 3,500 | Amazon IN |
| | | | | | | |
| | | **BMS total (items 1-22)** | | | **~₹4,945** | |
| | | **Rover total (items 23-25)** | | | **~₹5,400** | |
| | | **PCB fabrication (later)** | | | **~₹500** | |
| | | **GRAND TOTAL** | | | **~₹10,845** | |

---

## Decision Lock Summary (FINAL)

| Decision | Locked Choice |
|---|---|
| Cell model | Samsung INR18650-30Q (3Ah, 15A, 20mΩ) |
| Cell count | 6 cells, 3S2P nominal |
| Sensing | Discrete: voltage dividers (33k/10k) + INA226 + NTC |
| MCU | STM32F446RE (Nucleo-64) |
| MOSFET | IRF4905 P-channel (TO-220) + heatsinks |
| Gate drive | NPN 2N2222A level shifter (no gate driver IC) |
| GPIO expansion | Not needed |
| Switching granularity | String-level (not per-cell) |
| Communication | UART via ST-Link (initial), CAN ready (later) |
| PCB | 2-layer, 2oz copper (after zero PCB prototype) |

---

## Critical Corrections Applied in v2

1. **Voltage divider values changed** from 20kΩ/10kΩ to 33kΩ/10kΩ — old values would feed 4.2V into 3.3V ADC and damage STM32
2. **100kΩ base pull-down resistors added** to every NPN — prevents accidental MOSFET turn-on during MCU boot/reset
3. **Heatsinks added** for IRF4905 — at 10A peak without heatsink, junction temperature hits 149°C (destruction threshold)
4. **Firmware note added** — cell voltage is computed by subtracting adjacent tap readings, NOT read directly
5. **Firmware safety rule added** — never close Q1 and Q2 simultaneously unless sensor-confirmed ΔV < 0.1V
6. **INA226 shunt resistor warning added** — verify module's onboard shunt value matches your current range
7. **GPIO pin conflict noted** — PA2/PA3 conflict between MOSFET control and USART2, needs reassignment
