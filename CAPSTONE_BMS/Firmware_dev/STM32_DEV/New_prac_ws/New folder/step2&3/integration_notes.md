# M1 Steps 2 & 3 — integration notes

## Files

| File | Purpose |
|---|---|
| `i2c_bus.h` / `i2c_bus.c` | NEW. Per-bus I2C recovery + retry wrapper |
| `ina260.h` / `ina260.c` | REPLACES existing. Two-sensor, status-returning API |
| `bms_switch.h` / `bms_switch.c` | NEW. MOSFET switch control |

## What to paste into main.c

### Includes

```c
/* USER CODE BEGIN Includes */
#include "i2c_bus.h"
#include "ina260.h"
#include "bms_switch.h"
#include "oled.h"
/* USER CODE END Includes */
```

### Bus and sensor instances

```c
/* USER CODE BEGIN PV */
I2C_Bus bus_i2c1 = { &hi2c1, GPIOB, GPIO_PIN_8, MX_I2C1_Init, 0 };
I2C_Bus bus_i2c3 = { &hi2c3, GPIOA, GPIO_PIN_8, MX_I2C3_Init, 0 };

INA260 ina_a;   /* String A, on I2C1 alongside the OLED */
INA260 ina_b;   /* String B, on I2C3 */
/* USER CODE END PV */
```

`MX_I2C1_Init` and `MX_I2C3_Init` must be non-static for this to link.
CubeMX makes them `static` by default — remove the keyword in both the
definition and the prototype, or wrap them.

### Startup order

```c
/* USER CODE BEGIN 2 */
Switch_Init();          /* FIRST. Everything off before the bus can live. */

OLED_Init();
HAL_Delay(200);
OLED_BootScreen();

uint8_t ok_a = INA260_Init(&ina_a, &bus_i2c1, "A");
uint8_t ok_b = INA260_Init(&ina_b, &bus_i2c3, "B");

if (!ok_a || !ok_b)
{
    /* A missing sensor means that string is unmonitored. Do not run. */
    Switch_AllOff();
    /* show which one failed on the OLED, then halt */
}
/* USER CODE END 2 */
```

### Reading in the loop

```c
float v_a, i_a, v_b, i_b;

uint8_t fresh_a = INA260_ReadBusVoltage_V(&ina_a, &v_a)
               && INA260_ReadCurrent_mA (&ina_a, &i_a);

uint8_t fresh_b = INA260_ReadBusVoltage_V(&ina_b, &v_b)
               && INA260_ReadCurrent_mA (&ina_b, &i_b);

if (!fresh_a || !fresh_b)
{
    Switch_AllOff();        /* unsensed pack is an unsafe pack */
    /* latch a fault — do not simply continue with stale values */
}
```

## Error_Handler

Replace the generated body. The current one disables interrupts and spins,
leaving the MOSFETs wherever they happened to be.

```c
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Safe state FIRST, before interrupts go away and before anything
     that depends on HAL — HAL may be the thing that failed.
     Direct BSRR write: upper 16 bits = reset. */
  GPIOB->BSRR = (uint32_t)(GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_10) << 16u;

  __disable_irq();
  while (1) { }
  /* USER CODE END Error_Handler_Debug */
}
```

Written as a literal rather than calling `Switch_AllOff()` so it has no
dependency on another translation unit having linked or initialised.

## Changes needed in oled.c

`OLED_I2C_Recover()` is now redundant — `i2c_bus.c` does the same job per
bus. Two edits:

1. Delete `OLED_I2C_Recover()` and its prototype in `oled.h`.
2. In `OLED_WriteCommand()` and `OLED_WriteData()`, replace the
   `OLED_I2C_Recover()` call with `I2C_Bus_Recover(&bus_i2c1);` and add
   `#include "i2c_bus.h"`.

Also remove `extern void MX_I2C1_Init(void);` from the top of `oled.c` —
the bus descriptor carries that pointer now.

## CubeMX settings required

- SYS → Debug → **Serial Wire** (releases PB3, PB4 from JTAG)
- I2C3 enabled on PA8 (SCL) / PC9 (SDA), 100 kHz
- PB4, PB5, PB10 → GPIO_Output, push-pull, no pull, low speed
- Confirm `MX_GPIO_Init` writes those three pins LOW

## FMEA — switch module

| Failure | Detection | Safe response |
|---|---|---|
| GPIO stuck high (driver damage) | Commanded off, current still flows | Open the load switch; if that also fails, no software remedy — manual kill switch |
| NPN fails short | String conducts with GPIO low | String current non-zero when commanded off |
| NPN fails open | String never conducts | String current zero when commanded on |
| MOSFET gate oxide punctured | Erratic or permanently-on behaviour | Current readback contradicts command; all off |
| PB4 left in JTAG mode | String A never switches at all | Check Serial Wire setting before suspecting hardware |
| HAL fault mid-operation | `Error_Handler` entered | BSRR write forces all pairs off before halting |

## FMEA — sensing

| Failure | Detection | Safe response |
|---|---|---|
| Sensor absent at boot | `INA260_Init` ID mismatch | Refuse to start, all off |
| I2C bus hung by stuck slave | Transmit/receive timeout | 9-pulse recovery, retry twice |
| Retries exhausted | `ReadReg` returns 0 | All switches off, latch fault |
| Sensor silently wrong | Not detectable at string level | Accepted limitation — single sensor per string, no redundancy |

## Still outstanding

- Precharge pair is not on the fabricated PCB. Parallel-connect has no
  current-limited path, so dV gating is the only protection.
- `-9999.0f` sentinel removed; the old `OLED_ShowReadings()` checks for it
  and will need rewriting in Step 4.
- `INA260_Init()` return value was previously discarded; the startup code
  above now checks it.
