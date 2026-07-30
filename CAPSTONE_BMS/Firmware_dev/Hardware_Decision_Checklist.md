# Hardware Decision Checklist — Reconfigurable BMS Project

> **How to use this:** Work top to bottom. Each priority level depends on decisions made in the level above. Do not purchase anything from a lower priority until all decisions in higher priorities are locked.

---

## PRIORITY 0 — Core Decisions (Everything Depends on These)

These choices define your entire system's voltage, current, topology, and cost. Decide these first.

### Battery Cell Selection
- [ ] Decide cell chemistry: Li-ion (21700/18650) vs LiPo pouch
- [ ] Decide cell format: 21700 (recommended) vs 18650
- [ ] Decide cell count: 6 cells (recommended for 3S2P nominal)
- [ ] Select specific cell model based on:
  - [ ] Continuous discharge rating (minimum 10A recommended)
  - [ ] Capacity (Ah) — determines total pack energy
  - [ ] Internal resistance (lower = less heat, better efficiency)
  - [ ] Availability and cost in India
- [ ] **Candidate cells to evaluate:**
  - [ ] Samsung INR21700-50E (5Ah, 10A cont, 35mΩ)
  - [ ] Molicel P42A 21700 (4.2Ah, 30A cont, 20mΩ)
  - [ ] Samsung INR18650-30Q (3Ah, 15A cont, 20mΩ)
  - [ ] Sony VTC6 18650 (3Ah, 15A cont, 18mΩ)
- [ ] **LOCKED DECISION:** Cell model = Samsung INR18650 - 30Q

### Sensing Architecture
- [ ] Decide between two approaches:
  - [ ] **Option A: Dedicated battery monitor IC (BQ76952 or BQ76942)**
    - Handles per-cell voltage, temperature, coulomb counting, protections on one chip
    - Communicates to MCU via I2C
    - Simpler PCB, fewer components, more reliable sensing
    - Higher IC cost (~₹800–1200 per chip)
    - Built-in passive cell balancing
  - [ ] **Option B: Discrete sensing (INA226 + voltage dividers + NTC)**
    - More flexible, cheaper per component
    - More complex PCB, more wiring, more potential for noise/errors
    - No built-in protections — all in firmware
    - Better learning experience
- [ ] **LOCKED DECISION:** Sensing approach = Discrete sensing 

### MCU Selection
- [ ] Decide primary MCU:
  - [ ] **STM32F446RE (Nucleo board)**
    - Sufficient I/O, ADC channels, I2C/SPI buses
    - CAN peripheral built in
    - Good for professional embedded development
    - STM32CubeIDE / PlatformIO
  - [ ] **ESP32 (DevKit)**
    - Built-in WiFi + Bluetooth (easy wireless telemetry for demo)
    - Sufficient ADC and I/O
    - No native CAN (needs external SPI-CAN module like MCP2515)
    - Arduino IDE / PlatformIO
  - [ ] **STM32 + ESP32 combo**
    - STM32 handles real-time switching and sensing
    - ESP32 handles telemetry/display via WiFi
    - More complex but best of both
- [ ] **LOCKED DECISION:** MCU = STM32F446RE

---

## PRIORITY 1 — Switching Hardware (Depends on P0: Cell Choice)

These depend on your cell's voltage and current ratings decided above.

### MOSFET Selection
### High-Side Switching
- [ ] MOSFET: IRF4905 (P-channel, 55V, 74A, 20mΩ, TO-220)
- [ ] Quantity: 3 (String A switch + String B switch + precharge path)
- [ ] Level shifter per MOSFET: 2N2222 NPN + 1kΩ base resistor + 10kΩ pull-up
- [ ] No gate driver IC needed
- [ ] **LOCKED DECISION:** IRF4905 + NPN level shifter

### Rover Platform
- [ ] 4WD chassis with DC gear motors (not brushless, not thrusters)
- [ ] Motor driver: BTS7960 (handles up to 43A, overkill but robust)
- [ ] Expected current: 3-5A cruise, 8-10A peak
- [ ] RC transmitter + receiver for remote control

###Precharge Circuit (Priority 1 — Switching section):

1× precharge MOSFET (small signal, doesn't carry full load — logic-level N-channel)
1× precharge resistor (10Ω, 1W or 2W rated — handles 0.12A briefly)
This sits between the two strings, used before every parallel reconnection

Bulk Capacitor (Priority 1 — Switching section):

1× electrolytic capacitor (4700µF, 25V rated minimum) on the output bus
Holds bus voltage during 1–2ms string switchover in alternation mode

### Gate Drivers (Skip if using logic-level MOSFETs driven directly from MCU)
### High-Side Switching Circuit
- [ ] Select P-channel MOSFET:
  - [ ] IRF4905 (55V, 74A, 20mΩ — common, cheap, TO-220)
  - [ ] IRF9540N (100V, 23A, 117mΩ — higher voltage rating but higher resistance)
  - [ ] AO4407A (30V, 12A, 28mΩ — SMD, compact)
- [ ] Verify Vgs threshold is achievable (gate pulled to 0V, source at 11.1V = 11.1V Vgs)
- [ ] Verify RDS(on) at your expected current (calculate I²R loss)
- [ ] **LOCKED DECISION:** P-MOSFET model = _______________
- [ ] Quantity needed: 1 per string + 1 for precharge path = 3 minimum

### Level Shifting Components (per P-MOSFET)
- [ ] NPN transistor: 2N2222 or BC547 (any small signal NPN works)
- [ ] Base resistor: 1kΩ (limits MCU current to ~3mA)
- [ ] Gate pull-up resistor: 10kΩ (pulls gate to battery voltage when off)
- [ ] Quantity: 3 sets (one per P-MOSFET)
- [ ] Total cost per switch: ~₹33

### GPIO Planning
- [ ] Total GPIOs needed:
  - [ ] 2-3 for string switching P-MOSFETs
  - [ ] 1 for precharge P-MOSFET
  - [ ] 2 for I2C (SDA + SCL for INA226s)
  - [ ] 6 for ADC (per-cell voltage dividers)
  - [ ] 6 for ADC (NTC thermistors) — can share with voltage using analog mux
  - [ ] 1-2 for UART/CAN
  - [ ] Total: ~18-20 pins
- [ ] STM32F446 has 50+ GPIOs — no expansion needed
- [ ] ESP32 has 30+ GPIOs — no expansion needed
- [ ] **LOCKED DECISION:** No GPIO expansion required

---

## PRIORITY 2 — Sensing Hardware (Depends on P0: Sensing Architecture)

### If Option A — Battery Monitor IC (BQ76952)
- [ ] Procure BQ76952 evaluation module OR bare IC
- [ ] Plan I2C connection to MCU
- [ ] Plan thermistor connections to BQ76952's built-in temp inputs
- [ ] Review BQ76952 datasheet for register configuration
- [ ] Plan how reconfiguration logic coexists with BQ76952's built-in protections
  - [ ] BQ76952 handles: voltage/current/temp monitoring + passive balancing
  - [ ] MCU handles: reconfiguration decisions using data from BQ76952

### If Option B — Discrete Sensing
- [ ] **Current Sensing:**
  - [ ] Select current sensor: INA226 (I2C, ±20mA to 20A range with proper shunt)
  - [ ] Decide number of current sensors: 1 per parallel string (minimum 2 for 2-string config, 3 for flexibility)
  - [ ] Select shunt resistors (value depends on max current — typically 1-10mΩ)
  - [ ] Order INA226 breakout boards or bare ICs
- [ ] **Voltage Sensing:**
  - [ ] Design voltage divider for each cell tap (to bring cell voltage into MCU ADC range 0-3.3V)
  - [ ] Select precision resistors for dividers (1% tolerance minimum)
  - [ ] Plan ADC channel assignment on MCU (need 6 channels for 6 cells)
  - [ ] Consider adding op-amp buffers for cleaner ADC readings
- [ ] **Temperature Sensing:**
  - [ ] Procure 6× NTC 10K thermistors
  - [ ] Select pullup resistor value for NTC voltage divider (typically 10K)
  - [ ] Plan ADC channels or analog mux (CD4051) if ADC channels are limited
  - [ ] Create NTC resistance-to-temperature lookup table in firmware

---

## PRIORITY 3 — Power Supply and Protection (Depends on P0 + P1)

### BMS Board Power Supply
- [ ] Select buck converter to step pack voltage (7.4–12.6V) down to:
  - [ ] 5V rail (for gate drivers, relays, CAN transceiver)
  - [ ] 3.3V rail (for MCU, sensors)
- [ ] **Candidate modules:**
  - [ ] LM2596 module (cheap, available, 3A output)
  - [ ] MP1584 module (smaller, efficient, 3A)
  - [ ] AMS1117-3.3 LDO for 3.3V from 5V (simpler but less efficient)
- [ ] Ensure BMS board can power itself from the battery pack it's managing
- [ ] Add reverse polarity protection (P-MOSFET or diode)

### Safety Hardware
- [ ] Main pack fuse or polyfuse
  - [ ] Rating: slightly above max expected total current
  - [ ] Type: resettable polyfuse preferred for testing phase
- [ ] Physical kill switch / emergency disconnect
  - [ ] Large toggle switch or XT60 connector as manual disconnect point
- [ ] Precharge resistor circuit (optional but recommended)
  - [ ] Limits inrush current when connecting load
  - [ ] Bypassed by main MOSFET after precharge period

###Kill Switch / E-Stop (Priority 3 — Safety):

Physical toggle switch or XT60 loop key on main pack output
Already mentioned but worth promoting — essential for testing phase


---

## PRIORITY 4 — Communication and Interface (Depends on P0: MCU)

### Vehicle Communication
- [ ] If using CAN bus:
  - [ ] CAN transceiver module (MCP2551 or SN65HVD230)
  - [ ] 120Ω termination resistors
  - [ ] Decide CAN message format (what data gets sent to rover controller)
- [ ] If using UART:
  - [ ] USB-UART adapter (CP2102 or FTDI) for laptop logging
  - [ ] Define serial protocol / data packet format

### Telemetry and Display
- [ ] Choose one or more:
  - [ ] USB-UART to laptop (mandatory — needed for data logging and demo)
  - [ ] OLED display on BMS board (SSD1306 128x64, I2C — nice for standalone status)
  - [ ] WiFi dashboard via ESP32 (impressive demo, shows live topology + cell stats on phone/laptop browser)
  - [ ] SD card logging module (for long-duration test data capture)

---

## PRIORITY 5 — PCB Design (Depends on ALL above decisions being locked)

### PCB Specifications
- [ ] Schematic capture in KiCad (all components placed and connected)
- [ ] PCB layout decisions:
  - [ ] Board size (estimate based on component count)
  - [ ] Layer count: 2-layer (minimum) or 4-layer (better for thermals and routing)
  - [ ] Copper weight: 2oz for power traces (1oz is insufficient for >3A)
  - [ ] Power trace width calculation (use online PCB trace width calculator for your max current)
- [ ] Thermal management on PCB:
  - [ ] Thermal vias under MOSFET pads
  - [ ] Large copper pour on bottom layer as heatsink
  - [ ] Keep MOSFETs spaced for airflow
- [ ] Connector placement:
  - [ ] Battery cell input connectors (XT30 or screw terminals)
  - [ ] Load output connector (XT60)
  - [ ] Programming header (SWD for STM32 or USB for ESP32)
  - [ ] Sensor connectors (JST-XH for thermistors)
- [ ] Design review checklist before ordering:
  - [ ] No short circuits between power planes
  - [ ] All MOSFET gate-source resistors present (pull-down to prevent floating gates)
  - [ ] Decoupling capacitors on every IC
  - [ ] Silkscreen labels for debugging
- [ ] Order PCB (JLCPCB or PCBWay — typically 5-day turnaround)

---

## PRIORITY 6 — Rover Platform (Independent of BMS, can be done in parallel)

### Chassis and Drive
- [ ] Procure 4WD or 6WD rover chassis kit
- [ ] Verify motor voltage rating matches your pack's nominal output (~11.1V)
- [ ] Motor driver module:
  - [ ] BTS7960 (up to 43A — overkill but robust) OR
  - [ ] L298N (up to 2A per channel — fine for small motors)
- [ ] Remote control:
  - [ ] RC transmitter + receiver (simplest — FlySky FS-i6 or similar)
  - [ ] OR ESP32/Arduino on rover + Bluetooth/WiFi joystick app
- [ ] Mount points for BMS PCB and battery pack on chassis
- [ ] Ramp / incline for demo (piece of plywood at 15–30° angle)

---

## PRIORITY 7 — Test Equipment and Consumables

### Test Equipment (borrow from lab if possible)
- [ ] Multimeter (voltage and current verification)
- [ ] Oscilloscope (switching transient verification — critical for Milestone 1)
- [ ] Bench power supply (for charging individual cells to specific SOC levels)
- [ ] Electronic load or power resistors (for bench testing BMS before rover integration)
- [ ] Heat gun or soldering iron (for simulating thermal fault in demo)
- [ ] Spot welder (if making custom nickel strip cell connections) OR cell holders

### Consumables
- [ ] Silicone wire: 12 AWG (power), 22 AWG (signal) — 2-3 meters each
- [ ] Solder, flux, solder wick
- [ ] Heat shrink tubing (various sizes)
- [ ] Kapton tape (electrical insulation on battery connections)
- [ ] Zip ties, standoffs, screws for mounting
- [ ] Breadboard (for prototyping gate driver circuit before PCB)

---

## Decision Lock Summary

| Decision | Options | Locked Choice |
|---|---|---|
| Cell model | 21700 vs 18650, specific model | |
| Sensing approach | BQ76952 vs discrete (INA226 + dividers) | |
| MCU | STM32 vs ESP32 vs both | |
| MOSFET | Logic-level vs standard, specific model | |
| Gate driver | Direct MCU drive vs IC driver | |
| GPIO expansion | Direct pins vs shift register vs I2C expander | |
| Communication | CAN vs UART vs WiFi | |
| PCB layers | 2-layer vs 4-layer | |

**Lock all Priority 0 decisions before spending any money.**
