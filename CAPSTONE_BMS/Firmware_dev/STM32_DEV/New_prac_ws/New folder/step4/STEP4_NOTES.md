# M1 Step 4 — OLED BMS screens: integration notes, bench tests, FMEA

Every number here carries a label: `[CALCULATED]`, `[SIMULATED]` (host model,
ideal bus timing, no HAL overhead), `[DATASHEET]` (from a file in this
project), or `[VERIFY]` (stated from memory, not yet checked against a source).
Nothing in this document is `[BENCH-MEASURED]` yet. That is what tests T1–T9
are for.

---

## 1. Files

| File | Status | Purpose |
|---|---|---|
| `main.c` | REPLACES | Step 2/3 integration (4.0) + 20 ms scheduled loop |
| `oled.h` / `oled.c` | REPLACES | Burst page writes, page slicing, failure isolation, `+` `/` glyphs |
| `bms_status.h` | NEW | Neutral snapshot struct that display and telemetry read |
| `bms_display.h` / `.c` | NEW | Boot screen + main BMS screen, integer formatting |
| `telemetry.h` / `.c` | NEW | 5 Hz CSV over USART2 (ST-Link VCP) |
| `bench_debug.h` / `.c` | NEW, TEMPORARY | Manual one-string-at-a-time switch commands for testing |
| `i2c_bus.*`, `ina260.*`, `bms_switch.*` | UNCHANGED | From Steps 2/3 |

Removed: `OLED_I2C_Recover()`, `OLED_BootScreen()`, `OLED_ShowReadings()`, the
duplicate `#define OLED_ADDRESS` in `main.c`, and the `-9999` sentinel check.

### IP separation (your guardrail)

The display and telemetry include only `bms_status.h`. Topology and fault
reach them as text labels. They never include `reconfig.h` and never see the
reconfiguration state enum or transition rules. `bench_debug.c` is a crippled
manual selector, not the reconfiguration mechanism: one string at a time,
never parallel, no SOC or SOA input. It is removed when Step 6 lands.

---

## 2. Integration steps

1. **CubeMX**
   - Connectivity → I2C3 → I2C mode, 100 kHz. Pins PA8 = I2C3_SCL,
     PC9 = I2C3_SDA. `[VERIFY]` that CubeMX offers exactly these pins for
     I2C3 on F446RE; the pin map v3 says so.
   - SYS → Debug → **Serial Wire**.
   - Regenerate. This writes the I2C3 pin and clock setup into
     `stm32f4xx_hal_msp.c`. If you skip this, I2C3 has no pins, sensor B
     fails its probe, and the firmware refuses to run. That failure is safe
     and visible: the boot screen shows `INA B: MISSING`.
2. Copy the new and replaced files into `Core/Src` and `Core/Inc`.
3. Use the provided `main.c` as a whole, or merge its USER CODE blocks into
   your regenerated file. All hand-written code is inside USER CODE blocks.
   The `INA260_Init();` call that used to sit outside them is gone.
   `MX_I2Cx_Init` stays `static`, as CubeMX generates it; the bus reinit
   goes through wrappers in USER CODE 0.
4. **OLED controller.** `oled.h` defaults to `OLED_CTRL_SH1106` because you
   have a 1.3" module. Run T2 to confirm.
5. Terminal: 115200 8N1 on the ST-Link virtual COM port. Save the log; it
   is report data.

---

## 3. How the loop works

```
every 20 ms (tick-scheduled, no HAL_Delay)
  1. SENSE    INA A, INA B            -> snapshot, valid flags
  2. SAFETY   sensor lost? -> all off, latch   (Step 5 adds OV/UV/OC here)
  3. BENCH    one UART command (optional, compiled out with BMS_BENCH_DEBUG 0)
  4. FILL     switch states, labels, counters
  5. OUTPUT   loop%10==0 : CSV line
              otherwise  : (loop%10==5: redraw framebuffer) + ONE OLED page
  6. motors, LED
```

The CSV line and an OLED page never share an iteration, so their bus and
UART times never add up.

### Timing

| Item | `[CALCULATED]` | `[SIMULATED]` |
|---|---|---|
| Old driver, full frame, OLED healthy | ≥ 283 ms (1048 tx × 27 clocks @100 kHz) | — |
| Old driver, full frame, OLED unplugged | ≥ 18.9 s (1048 × ≥18 ms recovery) | — |
| New: one OLED page | 11.7 ms data + ~0.8 ms commands | 12.6 ms |
| New: worst loop work, all healthy | ~15.4 ms | 14.6 ms |
| New: worst loop work, OLED unplugged | ~2.5 ms + one NACK every 2 s | 8.6 ms, 0 overruns in 10 s |
| CSV line (~110 chars) | 9.5 ms | included above |
| Screen content refresh | 5 Hz | 5 Hz |
| Step 5 debounce, 3 samples | 3 × 20 ms = 60–80 ms (old loop: ~1.5 s) | — |
| Loop with one sensor dead | ~80 ms with 20 ms timeouts | 53.8 ms with fast NACK; loop overruns |

The simulation uses ideal bus timing. Real HAL software overhead makes all of
these larger. **T7 measures the real values** through the `work_us_max` and
`oled_us_max` CSV columns.

A dead sensor stretches the loop well past 20 ms. That is accepted: the
same event has already forced all switches off and latched the fault.

---

## 4. Two hardware findings — please read before bench testing

### Finding A — PB4 may turn String A ON while the MCU is in reset

PB4 is a JTAG pin (NJTRST). `[VERIFY: RM0390 GPIO chapter, "I/O pin
configuration after reset"]` After reset, the debug pins are in alternate
function with internal pulls, and NJTRST (PB4) has a pull-up. Selecting
"Serial Wire" in CubeMX changes nothing until firmware runs. During reset,
and until `Switch_Init()` executes, PB4 can be pulled high by the MCU itself.

`[CALCULATED]`, using R_PU = 30–50 kΩ `[VERIFY: STM32F446 datasheet DS10693,
I/O static characteristics]`, your 1 kΩ base resistor and 100 kΩ base
pull-down:

```
Weakest case (R_PU = 50k):
  I_B = (3.3 - 0.65) / (50k + 1k)  -  0.65 / 100k
      = 52.0 uA - 6.5 uA = 45.5 uA

Collector current needed to pull the gate down (10k gate pull-up, ~11.1 V pack):
  I_C(sat) = (11.1 - 0.2) / 10k = 1.09 mA

hFE(min) = 50 at I_C = 1 mA                          [DATASHEET: P2N2222A]
  beta x I_B = 50 x 45.5 uA = 2.27 mA  >  1.09 mA   -> NPN saturates
  -> gate pulled to ~0.2 V -> VGS ~ -10.9 V -> String A pair fully ON
```

Even with the weakest pull-up, the base drive is about twice what saturation
needs. The window is reset-to-`Switch_Init()` (expected to be milliseconds,
`[VERIFY]` in T1), **plus the whole time NRST is held low**: holding the
reset button, ST-Link "connect under reset", and possibly brown-out reset.

The existing M1 test "power-up default with GPIO disconnected" cannot catch
this, because the pull-up comes from the MCU pin.

**Bench test T1 decides whether this is real.** If it is, the options are
(your call, nothing is changed in code):

1. Strengthen only the PB4 channel's base pull-down so the divider keeps the
   base below about 0.3 V:
   `R_PD ≤ 0.3 × (30k + 1k) / (3.3 − 0.3) = 3.1 kΩ`, so use 2.2 kΩ.
   Check that it still turns on when driven:
   `I_B = (3.3 − 0.65)/1k − 0.65/2.2k = 2.65 − 0.30 = 2.35 mA` (deep
   saturation). This is a one-resistor swap. It departs from the locked
   "100 kΩ pull-downs" wording but serves the same intent.
2. Move String A control to a non-debug pin (PB7 or PC8 are spare). This
   needs PCB rework.

PB5 and PB10 have no debug function and are not affected.

### Finding B — the INA260 may be unable to see a disconnected string's voltage

Pin map v3 places each INA260 "between the string switch and the common bus
node". The INA260 measures voltage at its VBUS pin. If VBUS is on the bus
side of the switch (on breakout modules it is commonly tied to IN−,
`[VERIFY on your module and PCB]`), then **with that string's switch open,
its sensor reads the common bus, not its own string**.

The consequence is for Step 6, not Step 4. The ΔV < 0.1 V gate before
paralleling needs each string's own open-circuit voltage. If both sensors
read the same bus node, ΔV is always about 0 and the gate passes no matter
what the strings actually are. Given that M0 calculated 218.8 A for a cold
connect, that would be the worst possible silent failure.

**Bench test T4 decides this.** With all switches off, if both strings show
about 0 V, or the bus voltage, instead of their DMM values, VBUS is on the
bus side.

---

## 5. Bench tests

Run them in order. Always run behind the fuse and kill switch. Record
results as `[BENCH-MEASURED]`.

| # | Test | Method | Pass |
|---|---|---|---|
| **T1** | PB4 at reset (Finding A) | Pack connected, **resistive load on the String A path, load switch bypassed or closed by hand**, firmware flashed. Hold the Nucleo RESET button (B2). Measure PB4 to GND, String A current (DMM in series) and Q1 gate-source voltage. Repeat during power-up with a scope on the gate if available. | PB4 < 0.5 V and String A current ≈ 0 while reset is held. **If it fails, stop and pick option 1 or 2 above.** |
| **T2** | OLED controller / offset | Power up and look at the boot screen border. | All four edges visible and unbroken. Left edge missing or wrapped → change `OLED_COL_OFFSET`. Garbage → switch to `OLED_CTRL_SSD1306`. |
| **T3** | Sensor probe | Boot with both sensors fitted, then with each one unplugged in turn. | `INA A/B: OK`. With one unplugged: `MISSING`, `FLT SENSOR x LOST` inverted, switches stay off, `a`/`b` commands rejected. |
| **T4** | Voltage visibility (Finding B) | All switches off (`0`). Compare V_A and V_B on screen with a DMM at each string's own terminals. Then `a`: does V_B change to match A? | Each sensor shows its own string's DMM voltage with switches open. If both show ≈0 V or the same value → VBUS is bus-side; flag for Step 6. |
| **T5** | Accuracy | One string on with a steady resistive load. DMM voltage at the sensor's VBUS point; DMM current in series. Three load levels. | Agreement within the INA260 datasheet accuracy plus DMM accuracy. `[VERIFY]` both specs; neither datasheet is in the project files. Record the deviation either way. |
| **T6** | Current sign | Same setup as T5, discharging into the load. | Note the sign for each string. Both strings must show the same sign for discharge. If they differ, one module is wired reversed: fix the wiring or note it for Step 5, because OC must use the absolute value or a per-string sign. |
| **T7** | Timing | Run 5 minutes with motors off, then with motors running. Read `work_us_max`, `oled_us_max` and `overruns` from the CSV. | `overruns` = 0 and `work_us_max` < 20000 in normal running. These are your real `[BENCH-MEASURED]` timing numbers. |
| **T8** | OLED isolation | Unplug the OLED connector while running, wait 10 s, plug it back in. | CSV keeps flowing at 5 Hz, `overruns` stays 0, INA A keeps reading, `oled_err` rises about every 2 s, the screen recovers by itself after replugging. **Check before unplugging which module carries the I2C1 pull-ups**: if only the OLED does, INA A will fail, which is a real hardware finding. |
| **T9** | Bus fault → all off | String A on (`a`) with load. Short String B's SDA to GND with a jumper for about 1 s. | Within one loop: switches all off, `FLT SENSOR B LOST` inverted, B row `LOST`, CSV B fields empty. The fault stays latched after removing the jumper until reset. |

Also worth logging during T5–T7: toggle `l` and `a`/`b` while watching
`FA`/`FB`/`RC` on row 6. Any increase during switching means switching
transients are corrupting I2C. That is an M1 DoD item ("switching causes no
I2C faults").

---

## 6. FMEA — Step 4 code

| Failure | Detection | Safe response |
|---|---|---|
| OLED unplugged or dead | HAL NACK or timeout on a page write | Frame aborted, `oled_err++`, 2 s back-off, re-init on retry. No bus recovery from the OLED side. BMS unaffected. |
| OLED holds SDA low (stuck slave) | OLED write times out (25 ms); the next INA A read fails | INA A path performs 9-pulse recovery (single owner). If retries are exhausted → all off, latched. |
| INA sensor read fails after retries | `valid = 0` this cycle | All switches off, fault latched, row inverted `LOST`, CSV fields empty. Never displays stale numbers. |
| Sensor missing at boot | ID probe fails | Fault latched before the loop, switches never enabled, boot screen shows `MISSING`. |
| MCU hangs in the main loop | Heartbeat row 7 and LED stop changing | No automatic remedy in Step 4. **An independent watchdog (IWDG) is recommended for Step 5**: a hang currently leaves the switches in their last state. |
| HAL error / Error_Handler | Entered from any HAL init failure | Single BSRR write forces all switch pins low before halting. |
| Reset / programming window | Hardware (Finding A) | PB4 may be pulled high, see T1. PB5/PB10 are held off by the 100 kΩ pull-downs. |
| Bench command closes a string onto a fault | Operator watches current | No automatic validation (Step 6). Commands are refused while any fault is latched; `0` always works and is never rate-limited. |
| Two strings commanded together | Not possible by construction | `bench_debug` only ever selects one string. Break-before-make 20 ms (≥100× the calculated 0.17 ms gate turn-off). |
| UART TX blocked | 20 ms timeout | Line dropped silently. Telemetry loss never affects the BMS. |
| Topology display wrong | — | Labelled `CMD`: it shows what was commanded, not what was confirmed. Never presented as confirmed until Step 6 readback. |
| Display shows a sign-dropped current | Fixed | Formatter handles sign separately; host-tested at −0.505 A → `-0.51`. |

---

## 7. Host simulation evidence (`[SIMULATED]`)

The unchanged `main.c` loop was compiled on a PC against a stub HAL with a
simulated SH1106 (132-column RAM), two INA260s and USART2. The panel was
read back from the simulated controller RAM, so this checks the
page/column commands, not just the framebuffer.

- Strict compile (`-Wall -Wextra -Wpedantic -Wconversion`): clean.
- Boot screen border and main screen render as laid out at column offset 2.
  Nothing is written to RAM columns 0, 1, 130 or 131.
- Formatters: 11119 mV → `11.12`; −505 mA → `-0.51`; −10000 mA → `-10.00`
  (the old sentinel value, now an ordinary reading); ±15 A fits the row.
- OLED absent for 10 s: 0 overruns, 5 OLED errors (2 s back-off).
- Sequence: `a` → A only; `b` → B only; `a` inside 500 ms → rejected;
  sensor B removed → all off, latched, `SENSOR B LOST`; `a` → rejected.

**Limits of this evidence:** the stub does not model HAL software overhead,
I2C clock stretching, electrical noise, the real SH1106 command set, or the
real INA260. It verifies logic and layout only.

---

## 8. Still outstanding after Step 4

- Finding A (T1) and Finding B (T4) results.
- PC13 is still the motor toggle in code but "emergency stop" in pin map v3.
- Watchdog (IWDG) for hang detection → Step 5.
- Pin map v3 still lists PB6 as precharge (not populated) and PB3 for the
  L298N (reserved for SWO). The BOM still lists INA226, NTC and BTS7960.
- `bench_debug` must be compiled out or removed in Step 6.
