# STM32F446RE GPIO Pin Map — Reconfigurable BMS

> **Board:** NUCLEO-F446RE (Nucleo-64)
> **Status:** LOCKED — verify in CubeMX before wiring
> **Last updated:** M1 Step 1

---

## Conflicts Resolved

| Issue | Original | Fix |
|---|---|---|
| PA2/PA3 USART2 conflict | PA2 → Q3 GPIO, PA3 → ADC tap 1 | Moved Q1–Q3 GPIO to PB4/PB5/PB6. PA3 freed (but still reserved for USART2) |
| PA8/PA9 not ADC pins | PA8 → "ADC2_IN?", PA9 → "ADC2_IN?" | PA8 → I2C3_SCL (INA260 B). NTCs moved to PC2/PC3/PC5/PA6 |
| Q4 load disconnect missing | No pin assigned | PB10 assigned |
| INA260 address conflict | 2× INA260 on one I2C bus, no A0/A1 pads | Split to I2C1 (String A) + I2C3 (String B) |

---

## GPIO Outputs — Switch Control

| Pin | Function | Connection | Connector | Pull-down |
|---|---|---|---|---|
| PB4 | GPIO_Output | Q1 NPN base (String A main) | Arduino D5 | 100kΩ to GND |
| PB5 | GPIO_Output | Q2 NPN base (String B main) | Arduino D4 | 100kΩ to GND |
| PB6 | GPIO_Output | Q3 NPN base (Precharge) | Arduino D10 | 100kΩ to GND |
| PB10 | GPIO_Output | Q4 NPN base (Load disconnect) | Arduino D6 | 100kΩ to GND |

> **Safety:** All four have 100kΩ base pull-downs. Default state at power-up/reset = all MOSFETs OFF.
> **Drive circuit per switch:** STM32 pin → 1kΩ → 2N2222A base. 100kΩ pull-down on base. Collector → 10kΩ → IRF4905 gate (tied to source = battery+). Emitter → GND.

---

## ADC Inputs — Voltage Taps (6 channels)

All on **ADC1**, scanned via DMA.

| Pin | ADC Channel | Signal | Divider | Max Vin | Max Vadc | Connector |
|---|---|---|---|---|---|---|
| PA0 | ADC1_IN0 | String A Tap 1 (cell A1+) | 33k/10k | ~4.2V | ~0.98V | Arduino A0 |
| PA1 | ADC1_IN1 | String A Tap 2 (cell A1+ A2+) | 33k/10k | ~8.4V | ~1.95V | Arduino A1 |
| PA4 | ADC1_IN4 | String A Tap 3 (top of string) | 33k/10k | ~12.6V | ~2.93V | Arduino A2 |
| PB0 | ADC1_IN8 | String B Tap 1 (cell B1+) | 33k/10k | ~4.2V | ~0.98V | Arduino A3 |
| PB1 | ADC1_IN9 | String B Tap 2 (cell B1+ B2+) | 33k/10k | ~8.4V | ~1.95V | Morpho CN10-24 |
| PC4 | ADC1_IN14 | String B Tap 3 (top of string) | 33k/10k | ~12.6V | ~2.93V | Morpho CN10-34 |

> **Divider ratio:** 10k / (33k + 10k) = 0.2326
> **Cell voltage computed in firmware:** V_cell1 = V_tap1, V_cell2 = V_tap2 − V_tap1, V_cell3 = V_tap3 − V_tap2
> **Resolution:** 12-bit ADC, 3.3V ref → 0.8mV/step → ~3.4mV per step at battery side
> **Worst-case subtraction error:** ~5mV (adequate for 50–100mV fault detection)

---

## ADC Inputs — NTC Thermistors (6 channels)

All on **ADC1**, same DMA scan as voltage taps.

| Pin | ADC Channel | Signal | Connector |
|---|---|---|---|
| PC0 | ADC1_IN10 | NTC Cell A1 | Arduino A5 |
| PC1 | ADC1_IN11 | NTC Cell A2 | Arduino A4 |
| PC2 | ADC1_IN12 | NTC Cell A3 | Morpho CN7-35 |
| PC3 | ADC1_IN13 | NTC Cell B1 | Morpho CN7-37 |
| PC5 | ADC1_IN15 | NTC Cell B2 | Morpho CN10-6 |
| PA6 | ADC1_IN6 | NTC Cell B3 | Arduino D12 |

> **Circuit:** 10kΩ NTC + 10kΩ pull-up to 3.3V (divider). At 25°C → ~1.65V. Lookup table in firmware.

---

## I2C — Current Sensors (INA260)

| Pin | Function | Connection | Connector |
|---|---|---|---|
| PB8 | I2C1_SCL | INA260 String A | Arduino D15 |
| PB9 | I2C1_SDA | INA260 String A | Arduino D14 |
| PA8 | I2C3_SCL | INA260 String B | Arduino D7 |
| PC9 | I2C3_SDA | INA260 String B | Morpho CN10-1 |

> **Why 2 buses:** INA260 modules in hand have no A0/A1 address pads. Same default address → can't share bus.
> **INA260 specs:** Integrated 2mΩ shunt, ±15A range, bus voltage 0–36V. No external shunt needed.

---

## USART — Debug & Data Logging

| Pin | Function | Connection | Connector |
|---|---|---|---|
| PA2 | USART2_TX | ST-Link Virtual COM Port | Arduino D1 (DO NOT USE) |
| PA3 | USART2_RX | ST-Link Virtual COM Port | Arduino D0 (DO NOT USE) |

> **Hardwired** on Nucleo via solder bridges SB13/SB14 (default ON). Non-negotiable.
> **Data format:** CSV at 10Hz over USB-UART. 14 channels: 6 voltages, 2 currents, 6 temperatures + timestamp + topology state.

---

## CAN Bus — Optional (stretch goal)

| Pin | Function | Connection | Connector |
|---|---|---|---|
| PA11 | CAN1_RX | SN65HVD230 transceiver | Morpho CN10-14 |
| PA12 | CAN1_TX | SN65HVD230 transceiver | Morpho CN10-12 |

> Not needed for M1–M3. Wire only if ahead of schedule.

---

## Reserved / Special Pins

| Pin | Purpose | Notes |
|---|---|---|
| PA5 | LD2 (Green user LED) | Keep for heartbeat / debug blink. Arduino D13 |
| PA13 | SWDIO | ST-Link debug — do not use as GPIO |
| PA14 | SWCLK | ST-Link debug — do not use as GPIO |
| PC13 | B1 USER button | Available as emergency-stop input if needed |

---

## Spare Pins

| Pin | Has ADC? | Connector | Potential Use |
|---|---|---|---|
| PA7 | Yes (IN7) | Arduino D11 | Spare ADC — bus voltage monitor, extra sensor |
| PA5 | Yes (IN5) | Arduino D13 | Spare ADC if LED not needed |
| PA9 | No | Arduino D8 | Spare GPIO |
| PA10 | No | Arduino D2 | Spare GPIO |
| PB3 | No | Arduino D3 | Spare GPIO (caution: SWO trace pin) |
| PB7 | No | Morpho CN7-21 | Spare GPIO |
| PC7 | No | Arduino D9 | Spare GPIO |
| PC6 | No | Morpho CN10-4 | Spare GPIO |
| PC8 | No | Morpho CN10-2 | Spare GPIO |

---

## ADC1 DMA Scan Order

ADC scans in ascending channel order. DMA buffer layout:

```
Index  Channel  Pin   Signal
─────  ───────  ────  ──────────────
[0]    IN0      PA0   Vtap A1
[1]    IN1      PA1   Vtap A2
[2]    IN4      PA4   Vtap A3
[3]    IN6      PA6   NTC Cell B3    ← NTC interleaved by channel number
[4]    IN8      PB0   Vtap B1
[5]    IN9      PB1   Vtap B2
[6]    IN10     PC0   NTC Cell A1
[7]    IN11     PC1   NTC Cell A2
[8]    IN12     PC2   NTC Cell A3
[9]    IN13     PC3   NTC Cell B1
[10]   IN14     PC4   Vtap B3
[11]   IN15     PC5   NTC Cell B2
```

> Voltage taps and NTCs are interleaved in the raw buffer. Use named constants in firmware, NOT raw indices.

---

## Morpho-Only Pins Summary

5 ADC pins + 1 I2C pin are only on morpho connectors (not Arduino headers):

| Pin | Morpho Location | Signal |
|---|---|---|
| PB1 | CN10 pin 24 | Vtap B2 |
| PC4 | CN10 pin 34 | Vtap B3 |
| PC2 | CN7 pin 35 | NTC Cell A3 |
| PC3 | CN7 pin 37 | NTC Cell B1 |
| PC5 | CN10 pin 6 | NTC Cell B2 |
| PC9 | CN10 pin 1 | I2C3_SDA (INA260 B) |

> Use jumper wires from these morpho pins to your zero PCB.

---

## Pin Count Summary

| Category | Pins Used |
|---|---|
| GPIO outputs (switches) | 4 |
| ADC inputs (voltage taps) | 6 |
| ADC inputs (NTC) | 6 |
| I2C (current sensors) | 4 |
| USART (debug) | 2 |
| CAN (optional) | 2 |
| Reserved (LED, SWD, button) | 4 |
| **Total allocated** | **28** |
| Spare (with ADC) | 2 |
| Spare (GPIO only) | 7+ |

---

## CubeMX Verification Checklist

- [ ] PA2 → USART2_TX, PA3 → USART2_RX (default)
- [ ] PB4, PB5, PB6, PB10 → GPIO_Output (push-pull, no pull-up/down, low speed)
- [ ] All 12 ADC pins → ADC1_INx (verify channel numbers match this document)
- [ ] ADC1 → Scan mode ON, DMA continuous, 12-bit resolution
- [ ] PB8 → I2C1_SCL, PB9 → I2C1_SDA
- [ ] PA8 → I2C3_SCL, PC9 → I2C3_SDA
- [ ] PA5 → GPIO_Output (LD2)
- [ ] Confirm PA8 has NO ADC option (validates our earlier finding)
- [ ] Confirm PA9 has NO ADC option (validates our earlier finding)
- [ ] No yellow conflict triangles in CubeMX pin view
- [ ] Generate code, verify it compiles clean
