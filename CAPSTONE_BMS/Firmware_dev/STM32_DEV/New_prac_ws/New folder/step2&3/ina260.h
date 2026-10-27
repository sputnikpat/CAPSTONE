/*
 * ina260.h — INA260 current/voltage sensor driver (two-sensor capable)
 *
 *  Rewritten for M1 Step 2:
 *    - parameterised by I2C bus (two sensors, two buses)
 *    - status-returning API; value written through a pointer
 *
 *  WHY THE API CHANGED: the old version returned -9999.0f from
 *  ReadCurrent_mA() to mean "I2C failed". But -9999 mA = -10 A, which is
 *  a perfectly valid reverse current in this system (OC trip is 12 A,
 *  INA260 range is +/-15 A). A real reverse fault near that value would
 *  have been silently swallowed as a comms error. There is no float value
 *  that can be reserved, so status and value must be separate.
 *
 *  Register map and LSB sizes:
 *    0x01 Current      signed 16-bit, LSB = 1.25 mA
 *    0x02 Bus voltage  unsigned 16-bit, LSB = 1.25 mV
 *    0xFE Manufacturer ID, expect 0x5449 ("TI")
 *  >> VERIFY all of the above against the INA260 datasheet, Section 7.6
 *     (Register Maps). These match the values already proven working on
 *     the perf board, but confirm before the report.
 */

#ifndef INA260_H
#define INA260_H

#include "main.h"
#include "i2c_bus.h"

/* 7-bit address 0x40, shifted for HAL's 8-bit convention.
   Both modules are at 0x40 (no A0/A1 pads exposed), which is why
   they live on separate buses. */
#define INA260_ADDR          (0x40 << 1)

#define INA260_REG_CURRENT    0x01
#define INA260_REG_BUSVOLT    0x02
#define INA260_REG_MFR_ID     0xFE

#define INA260_MFR_ID_EXPECT  0x5449

/* One per physical sensor. */
typedef struct {
    I2C_Bus  *bus;            /* which bus this sensor lives on   */
    uint8_t   present;        /* 1 if Init() saw the correct ID   */
    uint32_t  fail_count;     /* diagnostics: failed reads        */
    const char *name;         /* "A" / "B", for logging           */
} INA260;

/**
 * @brief  Probe the sensor by reading its manufacturer ID.
 *         Sets dev->present. Call once at startup and CHECK THE RESULT —
 *         a missing sensor must not be allowed to look like 0 A / 0 V.
 * @return 1 if the sensor answered with the expected ID, else 0.
 */
uint8_t INA260_Init(INA260 *dev, I2C_Bus *bus, const char *name);

/**
 * @brief  Read bus voltage.
 * @param  out_V  written only on success
 * @return 1 on success, 0 on I2C failure (out_V untouched)
 */
uint8_t INA260_ReadBusVoltage_V(INA260 *dev, float *out_V);

/**
 * @brief  Read current. Positive = IN+ to IN-. Negative = reverse.
 * @param  out_mA  written only on success
 * @return 1 on success, 0 on I2C failure (out_mA untouched)
 */
uint8_t INA260_ReadCurrent_mA(INA260 *dev, float *out_mA);

#endif /* INA260_H */
