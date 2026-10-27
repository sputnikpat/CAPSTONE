# M1 Step 5 — Safety layer: integration, edge cases, bench tests, FMEA

Labels used throughout: `[DATASHEET]` (a file in this project, page given),
`[CALCULATED]` (math shown), `[SIMULATED]` (host build with stub HAL),
`[ASSUMPTION]` (needs bench data), `[VERIFY]` (stated from memory, not yet
checked against a source). Nothing here is `[BENCH-MEASURED]` yet. Tests
S5-T1 to S5-T12 produce those numbers.

---

## 1. Files

| File | Status | Purpose |
|---|---|---|
| `bms_safety_cfg.h` | NEW | Every threshold, each with its source |
| `bms_safety.h` / `.c` | NEW | OV / UV / OC-discharge / OC-charge / sensor / stuck, debounce, latch, enforcement |
| `bench_debug.h` / `.c` | REPLACES | Gated by the safety block mask; adds `f a` / `f b` fault injection |
| `main.c` | REPLACES | Safety slot, IWDG, reset-cause print, motor stop on trip |
| `motor.h` | REPLACES | **IN3/IN4 corrected to match the PCB netlist** (section 6.3) |
| everything else | UNCHANGED | from Step 4 |

`motor.c` needs no code change (it uses the `motor.h` defines). Its header
comment still lists the old IN3/IN4 mapping; fix the comment when convenient.

**IP separation.** `bms_safety.*` contains protection only: thresholds,
debounce, latch, force-off. It has no precharge, no ΔV gate, no cooldown and no
topology choice. Step 6 (`reconfig.c`) will read `Safety_BlockMask()` and must
never be included by the safety module. The safety module can be shown in full
in the report without exposing the reconfiguration mechanism.

---

## 2. Integration steps

1. **CubeMX**
   - SYS → Debug → **Serial Wire**. This frees PB4 (String A) **and** PB3
     (motor IN3). "Trace Asynchronous Sw" would keep PB3 as SWO.
   - **Do not enable IWDG in CubeMX.** `main.c` starts it by direct register
     writes just before the main loop. A CubeMX `MX_IWDG_Init()` would start it
     during init; the 1.5 s boot screen would then outlast the ~0.34–0.94 s
     timeout and the board would reset forever.
   - I2C3 on PA8/PC9 stays as in Step 4.
2. Copy the files into `Core/Inc` and `Core/Src`. If you regenerate with
   CubeMX, merge the USER CODE blocks of the new `main.c` instead of
   overwriting.
3. Build. Possible compile errors and their meaning:
   - `RCC_FLAG_xxxRST` or `__HAL_RCC_CLEAR_RESET_FLAGS` undefined → the HAL
     name differs from what I used. Look it up in `stm32f4xx_hal_rcc.h`.
   - `DBGMCU_APB1_FZ_DBG_IWDG_STOP` or `IWDG_SR_PVU` undefined → check
     `stm32f446xx.h`. These are CMSIS names, written from memory `[VERIFY]`.
   A wrong name fails the build; it cannot silently misbehave.
4. Terminal: 115200 8N1. The first lines after reset are now:
   `# RESET CAUSE: ...`, sensor probe, then `# SAFETY READY: switching allowed`
   about 60 ms into the loop.

---

## 3. How it works

### 3.1 Loop order (every 20 ms)

```
 SENSE A, B ──► SAFETY_STEP ──► bench cmd (gated) ──► snapshot ──► CSV/OLED ──► motors ──► LED ──► KICK IWDG
                   │
                   ├─ new fault this cycle? → motor_running = 0, Motor_Stop(), "# TRIP: ..." on UART
                   └─ enforces switch state itself, EVERY cycle
```

The watchdog is kicked last. A loop that hangs anywhere, including inside
sensing or safety, never kicks it.

### 3.2 Per-string fault logic

```
                 sample valid?
                   │ no ──────────────────────────────► latch SENSOR ──► ALL OFF (global)
                   │ yes
                   ▼
   ┌──────── evaluate (every sample) ─────────────────────────────────┐
   │ OV   : V > 12.60 V   } only if V is this string's own voltage    │
   │ UV   : V <  7.50 V   } (see 5.3)                                 │
   │ OCD  : I_dsg  > 12 A                                             │
   │ OCC  : I_chg  >  4 A                                             │
   │ STUCK: commanded OFF ≥100 ms AND |I_real| > 200 mA               │
   └──────────────────────────────────────────────────────────────────┘
                   │ bad?
          no ◄─────┼─────► yes: counter++
   counter = 0     │            counter ≥ 3 ?
                   │                 │ yes
                   ▼                 ▼
                 (ok)          LATCH bit (never clears in software)
                                     │
                       STUCK? ─yes─► ALL OFF incl. load switch (global)
                                     │ no
                                     ▼
                         open THIS string only; other string unaffected
```

### 3.3 System states

```
 power-on / RESET button / watchdog
           │
           ▼
   ┌───────────────┐  every sensor gives 3 clean   ┌──────────────┐
   │  BOOT_HOLD    │ ─────── samples (60 ms) ─────►│   READY      │
   │ all switches  │                               │ bench cmds   │
   │ blocked       │                               │ allowed      │
   └───────────────┘                               └──────┬───────┘
           │ sensor missing at boot                       │ string fault
           ▼                                              ▼
   ┌───────────────┐                               ┌──────────────┐
   │ GLOBAL_FAULT  │◄──── sensor lost / STUCK ─────│ STRING_FAULT │
   │ all off,      │                               │ that string  │
   │ all blocked   │◄──── second string faults ────│ blocked      │
   └───────────────┘   (= both down, all blocked)  └──────────────┘
           │                                              │
           └──────────── only exit: MCU RESET ────────────┘
```

---

## 4. Thresholds and math

| Parameter | Value | Source / math |
|---|---|---|
| OV | 12.60 V | 3 × 4.20 V. Charge voltage 4.20 ± 0.05 V `[DATASHEET 30Q spec §3.3, p.2]` |
| UV | 7.50 V | 3 × 2.50 V. Discharge cut-off 2.5 V `[DATASHEET §3.8, p.2]` |
| OC discharge | 12 A | `[DESIGN]`, below 15 A max continuous discharge `[DATASHEET §3.7]` |
| OC charge | 4 A | Rated (max) charge current 4 A `[DATASHEET §3.4]` |
| Debounce | 3 samples | Trip latency 3 × 20 = 60 ms, up to 80 ms with phase `[CALCULATED]` |
| Ready hold-off | 3 samples | 60 ms after the loop starts `[DESIGN]` |
| Stuck threshold | 200 mA | `[ASSUMPTION]`, see 6.2: **must be checked against the Nucleo's own draw** |
| Stuck settle | 100 ms | Gate turn-off ≈ 0.17 ms `[CALCULATED, bench_debug.c]`; 100 ms is margin |
| V settle after ON | 100 ms | `[DESIGN]`, only used when VBUS is bus-side |
| Injected current | 13 A | OC_DSG + 1 A, so it is unambiguously over the limit |
| IWDG timeout | 0.34–0.94 s | (499+1) × 32 / f_LSI with f_LSI = 47 / 17 kHz `[CALCULATED]`; the LSI range is `[VERIFY: DS10693]` |

**Why string current = cell current.** Each string is 3S1P, so the same
current flows through all three cells (series path). The 4 A and 15 A cell
limits therefore apply directly to the string current.

**IWDG margin.** Worst legitimate loop `[CALCULATED]`: both sensors dead
2 × ~80 ms + OLED timeout 25 ms + CSV 9.5 ms + bench dead time 20 ms + UART
prints ≈ 230 ms. The shortest timeout is 340 ms. That leaves ~110 ms of margin.
S5-T8 measures the real worst case.

---

## 5. Edge cases and how each one is handled

These are the decisions I made on your behalf. Each one is implemented in the
code listed.

### 5.1 Reset is the only way to clear a fault
- Latches live in RAM and **there is no software clear path**. The black
  Nucleo RESET button, a power cycle or a watchdog reset are the only exits.
- **A fault that is still present re-latches after reset.** Nothing can be
  switched on until every sensor has given 3 clean samples (BOOT_HOLD). An OV
  string, a dead sensor or a stuck switch is detected again before any
  connection is possible.
- **Injected faults clear on reset**, which is what you want for repeat demo
  runs.
- **Reset never switches anything on.** After reset all strings are off, and
  you reconnect by hand with `a` / `b`. Step 6 takes this over.

### 5.2 A stuck switch is not hidden by a reset
The STUCK check runs from boot. If a pair has failed short, current flows
through a string that is commanded OFF, and STUCK latches again within
~160 ms of the loop starting (100 ms settle + 3 samples). Limitation: with
**no load connected** there is no current, so a stuck switch is undetectable.
That is physics, not a code gap.

### 5.3 Where the sensor reads voltage (Finding B) and the boot deadlock
If an INA260's VBUS is on the bus side of its switch, an OFF string's sensor
reads the bus. With all switches off at boot, the bus is ~0 V. A naive UV
check would latch UV on both strings at boot, and since only reset clears it,
the system could never start. Handling:
- `SAFETY_VBUS_SEES_OPEN_STRING = 0` (default): OV/UV are judged only while
  that string is ON and has been ON for 100 ms. This is correct for both
  possible wirings.
- Set it to `1` only after T4 (Step 4) proves each sensor sees its own string
  with the switch open. OV/UV are then checked all the time, including before
  connecting.
- Host test confirms that 0 V on OFF strings does not latch anything
  `[SIMULATED]`.

### 5.4 Motor transients
Spikes shorter than 3 samples (≤ 40–60 ms) do not trip. The host test shows
that two 12.5 A samples followed by a normal one do not latch `[SIMULATED]`.
If the real motor inrush is longer, M3 tuning will show it as a false trip in
the CSV.

### 5.5 Current sign not yet confirmed (T6)
Discharge sign is a per-string setting (`SAFETY_DSG_SIGN_A/B`, default +1). If
it is wrong, discharge current is judged against the **4 A charge** limit, so
the error trips early, never late. The bench symptom is an `OCC` trip while
discharging above 4 A. Fix by flipping that string's sign.

### 5.6 Injection never pretends to be real
- It replaces only the current **the safety layer sees**. The OLED and CSV
  still show the real measured current.
- The fault label carries `*` (for example `A:-- B:OCD*`), and bit 7 of that
  string's flag byte is set in the CSV `fault` column.
- STUCK uses the real current, so injection cannot fake a stuck switch.
- Set `BMS_FAULT_INJECTION 0` for any run you report as a real fault.

### 5.7 Something tries to re-close a faulted string
Enforcement runs every cycle, not only when the fault first latches. If any
code (bench, a future bug in Step 6) turns a faulted string back on, it is
forced off within one loop. Host-tested `[SIMULATED]`.

### 5.8 Motors on a fault
- Any **new** fault stops the motors and clears `motor_running`. When power
  returns on the healthy string, the rover does not lurch off by itself.
- While **no** string can run, PC13 is ignored.
- Press PC13 to drive again once a healthy string is selected.

### 5.9 Turning things OFF is never blocked
`0` (all off) and turning the load switch **off** are always accepted, with no
cooldown. Only switching **on** is gated by safety.

### 5.10 Watchdog edge cases
- Started after the 1.5 s boot screen, so boot delays never trip it.
- Frozen while a debugger halts the core, so breakpoints do not reset the board.
- If `Error_Handler()` runs after the watchdog has started, it forces all
  switches off and then starves the watchdog. The board reboots into all-off
  and prints `RESET CAUSE: WATCHDOG`. That reboot is intended.
- The reset-cause check order matters: the pin-reset flag is checked last
  because an internal reset also pulses NRST, and power-on is checked before
  brown-out `[VERIFY: RM0390, RCC_CSR]`.

### 5.11 Charger at the top of its tolerance
4.25 V/cell × 3 = 12.75 V `[CALCULATED]`, which is above the 12.60 V OV trip.
A charger at the high end of its ±0.05 V tolerance will trip OV at the end of
a charge. That is safe-side behaviour, but it will look like a fault during
the charging demo. Step 6's charge-full stop (12.54 V) must end the charge
before OV. Noted in `bms_safety_cfg.h`.

### 5.12 String-level voltage hides one bad cell
OV/UV are string sums, so they are average-per-cell limits. One cell can be
past 4.20 V or below 2.50 V while the sum is inside the limit. This is already
in your Honest Limitations list. No code change.

---

## 6. PCB netlist findings (Switching_PCB.net)

I cross-checked every pin the firmware uses against the netlist.

**Matches the firmware:**

| Function | Netlist | Firmware |
|---|---|---|
| String A switch | PB4 → D1 → R1 (1k) → Q3 base → Gate_Q1 (Q1/Q2) | `SW_STRING_A_PIN` = PB4 ✓ |
| String B switch | PB5 → D2 → R4 (1k) → Q4 base → Gate_Q5 (Q5/Q6) | PB5 ✓ |
| Load switch | PB10 → D3 → R7 (1k) → Q7 base → Gate_Q8 (Q8/Q9) | PB10 ✓ |
| Base pull-downs | R2, R5, R8 = 100k to GND | ✓ |
| Gate pull-ups | R3, R6, R9 = 10k gate to source | ✓ |
| I2C1 | PB8/PB9 → OLED + U2 (INA A) | ✓ |
| I2C3 | PA8/PC9 → U3 (INA B) | ✓ |
| Precharge (PB6) | not connected | matches "not populated" ✓ |

### 6.1 CRITICAL: the INA260 power pins are not connected on the PCB

U2 and U3 have **IN+, IN− and VBUS unconnected** (nets
`unconnected-(U2-IN+-Pad7)`, `unconnected-(U2-VBUS-Pad6)`, and the same for
U3). The power path on the board is `STR_A → Q2 → Q1 → BUS`, with no sensor in
it. Only the I2C and supply pins of the sensors are routed.

So either (a) the sensors are wired into the power path with flying leads off
the PCB, or (b) they are not in the path at all.

If (b): both INA260s read ~0 A and ~0 V forever. **OC, OCC and STUCK can then
never trip**, and OV/UV see 0 V. The firmware would only catch the UV, and
only once a string is on. Every current-based protection in this step depends
on the sensors being in series.

**Action, before any test with cells:** confirm and document where IN+, IN− and
VBUS are wired (a photo for the report is ideal). S5-T1 below checks it
electrically. This wiring also answers Finding B (T4): VBUS is wherever you
connected it.

### 6.2 String A also powers the Nucleo (VIN), upstream of the switch

Net `STR_A` = J1 pin 1, Q2 drain **and U1 VIN (CN6 pin 8)**. Three
consequences:

1. **VIN over-range at full charge.** VIN is specified 7 V to 12 V
   `[DATASHEET UM1724 §7.5.2, Table 7, p.22]`. A full String A is
   3 × 4.20 = 12.60 V `[CALCULATED]`, 0.6 V above the maximum. At 9–12 V the
   board may draw at most 250 mA from VIN (same table). Running at 12.6 V is
   outside the user manual's range. Whether the regulator survives it is not
   something I can confirm; treat it as a real risk and decide (for example,
   power the Nucleo from USB/E5V during the demo, or add a series diode, whose
   ~0.7 V drop brings 12.6 V to ~11.9 V `[CALCULATED, diode drop ASSUMPTION]`).
2. **False STUCK on String A is possible.** If INA A sits between the String A
   cells and J1 (string side), the Nucleo's own supply current flows through
   it even with String A switched **off**. The STUCK threshold is 200 mA. The
   Nucleo + OLED + two INA260 boards draw an unknown amount, possibly over
   100 mA `[ASSUMPTION]`. S5-T1 measures this. If the off-state reading is
   above ~100 mA, raise `SAFETY_STUCK_MA` (keeping it well under the load
   current) or move the VIN tap. If INA A is on the bus side of the switch, the
   Nucleo draw does not pass through it and this point does not apply.
3. **The MCU keeps draining String A after String A trips UV.** At 7.50 V the
   MCU still runs (VIN ≥ 7 V). Below ~7 V it browns out and every pin floats.
   PB5 and PB10 are then held off by their 100k pull-downs; PB4 has the
   Finding A question (6.4). Do not leave the board powered from a UV-tripped
   String A unattended.

### 6.3 Motor IN3/IN4 were swapped in `motor.h` (fixed)

Netlist: PA9 → IN1, PA10 → IN2, **PB3 → IN3, PC7 → IN4** (U4 pins 7–10). This
matches your screenshot. The old `motor.h` had IN3 = PC7 and IN4 = PB3, so
`Motor_Forward()` drove Motor B **backwards**. `Motor_Stop()` sets all four LOW
and was never affected. The new `motor.h` matches the board.

### 6.4 Base-drive diodes do not remove Finding A

Each GPIO drives the NPN base through a 1N5400 (D1/D2/D3) and then 1k. The
diode drops some voltage, but at the tiny current of PB4's internal pull-up
the drop is small. Re-doing the Step 4 calculation with an assumed ~0.45 V
diode drop at ~40 µA `[ASSUMPTION — 1N5400 is not characterised at µA]`:

```
I_B = (3.3 − 0.45 − 0.65) / (50k + 1k) − 0.65 / 100k
    = 43.1 µA − 6.5 µA = 36.6 µA
β·I_B = 50 × 36.6 µA = 1.83 mA  >  1.09 mA needed    [hFE(min) from P2N2222A datasheet]
```

The NPN can still saturate, so String A can still turn on during reset. **T1
(Step 4) is still required**, and now matters more because the RESET button
is your fault-clear method. The 2.2k pull-down fix from Step 4 still works:
the diode only adds drop, which helps.

### 6.5 Cosmetic mismatches (report consistency only)

- U1 symbol is **NUCLEO-F401RE**. The Nucleo-64 pinout is shared, so the PCB
  is fine for the F446RE, but the report should say F446RE.
- Q3/Q4/Q7 symbols are **2N2219** (TO-92 footprint). Your locked BOM says
  2N2222A. Make sure the schematic image in the report matches the part you
  fitted.

### 6.6 I2C pull-ups go to 5 V

The INA260 boards and the OLED are powered from `U1_5V`. If their on-board
pull-ups go to their VCC, SDA/SCL idle at 5 V into PB8, PB9, PA8 and PC9.
Those pins must be 5 V-tolerant (FT). I believe most of them are, but I am not
certain for every one. **Check the "I/O structure" column of the pin table in
the STM32F446 datasheet (DS10693) for PB8, PB9, PA8 and PC9** before the next
long run.

---

## 7. Bench tests

Run in order. Always behind the fuse and kill switch. Record the results as
`[BENCH-MEASURED]`.

| # | Test | Method | Pass |
|---|---|---|---|
| **S5-T1** | Sensors really in the power path (6.1, 6.2) | All switches off. Read `ia`/`ib` in the CSV. Then `a` with a resistive load: compare `ia` with a DMM in series. Same for `b`. | Off-state current < 100 mA on both. On-state current matches the DMM. **If `ia`/`ib` stay ~0 with load current flowing, stop: OC and STUCK cannot work.** |
| **S5-T2** | Reset state of PB4 and PB3 | Hold RESET with a load on the String A path and the motors connected. | String A current ≈ 0 and neither motor moves. If String A conducts: apply the Step 4 Finding A fix first. |
| **S5-T3** | Boot hold-off | Reset, then immediately send `a` repeatedly. | First `a` within ~60 ms of loop start is `REJECTED`; `# SAFETY READY` appears; then `a` works. |
| **S5-T4** | Injected OC, demo path | `b`, load on, PC13 to drive. Type `f` then `b`. | Within ~60–80 ms: B opens, `# TRIP: A:-- B:OCD*`, motors stop, OLED row 5 inverted. `b` is rejected. `a` is accepted; PC13 drives again on A. |
| **S5-T5** | Trip latency | Toggle a spare GPIO (PC8) at the trip, scope it against the injection keystroke's UART line. | Record the value. Expected 60–80 ms `[CALCULATED]`. |
| **S5-T6** | Latch survives the condition going away | After S5-T4, wait 1 min. | Fault stays latched. Only RESET clears it. |
| **S5-T7** | Real UV | Bench PSU instead of cells on String A (current-limited). `a`, then lower the PSU from 9 V through 7.5 V. | Trips at 7.50 V ± INA260 accuracy. Record the trip voltage. |
| **S5-T8** | Watchdog | Temporarily add `while(1){}` after a `#define HANG_TEST 1` in the loop (or halt via debugger with the freeze bit removed). | Board resets within 0.34–0.94 s; UART shows `RESET CAUSE: WATCHDOG`; all switches off. Remove the test code after. |
| **S5-T9** | Sensor lost mid-run | `a` with load. Short String B's SDA to GND for ~1 s. | All off within one loop, `SENSOR B LOST`, everything blocked until RESET. |
| **S5-T10** | Stuck-switch detection | `a` with load, then `0`, then bridge String A's pair drain-to-drain with a fused jumper (simulated short). | After ~160 ms: `STUCK A`, load switch opens. **Only do this with the fuse in place.** |
| **S5-T11** | Motor transients | Rover on the bench, String A, PC13 forward/stop repeatedly for 2 min. | Zero trips. Note the CSV `ia` peak; it sets the M3 demo threshold. |
| **S5-T12** | Loop timing | Run 5 min. | `overruns` = 0 and `work_us_max` < 20000. |

---

## 8. FMEA — Step 5 code

| Failure | Detection | Safe response |
|---|---|---|
| String over-voltage | V > 12.60 V for 3 samples while trusted | Open that string, latch, stop motors |
| String under-voltage | V < 7.50 V for 3 samples while trusted | Open that string, latch, stop motors |
| Discharge over-current | I_dsg > 12 A for 3 samples | Open that string, latch, stop motors |
| Charge over-current | I_chg > 4 A for 3 samples | Open that string, latch |
| Hard short (100s of A) | Firmware is too slow: 60–80 ms | **Fuse per string.** Not a firmware function. The netlist shows no fuse on the PCB: it must be in the cell wiring. |
| Sensor unreadable | I2C retries exhausted (`valid = 0`) | All off incl. load, latch, block everything |
| Sensor silently wrong / frozen | Not detectable with one sensor per string | Accepted limitation (unchanged from Step 3) |
| Sensor not in power path (6.1) | S5-T1 only; firmware sees plausible 0 A | **None in firmware.** Hardware must be verified. |
| Switch fails short | Current > 200 mA in a string commanded OFF for ≥100 ms, 3 samples | All off incl. load switch, latch. If the load switch is also shorted: manual kill switch |
| Switch fails open | String never conducts | Not detected in Step 5 (no harm). Step 6 ON-readback |
| Faulted string re-closed by other code | Enforcement every cycle | Forced off within one loop |
| Main loop hangs | IWDG not kicked | MCU reset in 0.34–0.94 s, boot into all off, reset cause printed |
| HAL error after watchdog start | `Error_Handler` | Switches off by BSRR, then watchdog reset |
| PB4 pulled high during reset (Finding A) | S5-T2 | Hardware fix (2.2k pull-down). Firmware cannot act while in reset. |
| MCU brown-out from low String A (6.2) | None (MCU is off) | PB5/PB10 held off by pull-downs; PB4 per Finding A |
| Injected fault mistaken for real | `*` in label, bit 7 in flags | Compile out with `BMS_FAULT_INJECTION 0` |
| Wrong current sign | Early trip as `OCC` during discharge | Safe-side. Fix sign after T6 |
| Boot with a still-present fault | Hold-off: 3 clean samples before any switch | Fault re-latches before anything connects |
| Boot deadlock from 0 V bus | — | Prevented: OV/UV only judged when VBUS is that string's (5.3) |
| Reset button hides a stuck switch | STUCK check runs from boot | Re-latches within ~160 ms if load is connected |

---

## 9. Host simulation evidence `[SIMULATED]`

`bms_safety.c` was compiled on a PC (`gcc -std=c99 -Wall -Wextra -Wpedantic
-Wconversion`, **zero warnings**) against stub switch and snapshot code, and
driven through 22 scenarios. All passed:

- Everything blocked until 3 clean samples; ready on the 3rd.
- Injected OC on B with A running: trips on the 3rd sample (60 ms of loop
  time), A and load keep running, label `A:-- B:OCD*`, only B blocked.
- Re-closing B after the trip: forced off on the next step.
- Two 12.5 A samples, then a normal one, then two more: no trip. Third
  consecutive: OCD, string opened.
- UV on a freshly connected string: latched 7 samples (140 ms) after the low
  reading appeared, because of the 100 ms V-settle window + 3-sample debounce.
- 0 V on all-OFF strings for 20 samples: no fault (no boot deadlock).
- 600 mA through an OFF string: STUCK, all off including load.
- Current during the 100 ms after an OFF command: not STUCK.
- Sensor B invalid: all off in the same step, everything blocked.
- −4.5 A: OCC. 12.65 V: OV. Missing sensor at boot: all blocked.

**Limits:** no HAL, no real INA260, no I2C timing, no electrical noise. This
verifies the decision logic only. `main.c` and `bench_debug.c` were not host
compiled because they depend on the STM32 HAL; the first CubeIDE build is
their compile check.

---

## 10. Demo in Step 5 (and what waits for Step 6)

`bench_debug` still never puts both strings on together. That is deliberate:
paralleling needs the ΔV gate, which is Step 6. So in Step 5 the demo is:

```
RESET → (# SAFETY READY) → b → l → PC13 (rover drives on B)
      → f b  → ~60-80 ms → B opens, motors stop, OLED "FLT A:-- B:OCD*"
      → a    → PC13      → rover drives on A
      → RESET to clear and repeat
```

The final demo (both strings paralleled, B fails, rover keeps driving on A
without stopping) needs Step 6. The safety behaviour is identical; only the
"both on" starting state is new.

---

## 11. Outstanding after Step 5

- **6.1 sensor wiring: verify before any cell test.**
- 6.2 VIN at 12.6 V: decide how the Nucleo is powered for the demo.
- 6.6 FT check on the four I2C pins.
- Step 4 T1 (PB4 at reset), T4 (VBUS location), T6 (current sign).
- `SAFETY_STUCK_MA` against the measured off-state current (S5-T1).
- Pin map v3 still lists PB6 precharge and BTS7960 motor pins; update it to
  the netlist.
- `motor.c` header comment (old IN3/IN4 mapping).
- `bench_debug` must be compiled out once Step 6 owns the switches.
