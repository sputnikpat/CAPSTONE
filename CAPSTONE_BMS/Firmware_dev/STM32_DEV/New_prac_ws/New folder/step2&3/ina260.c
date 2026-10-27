/*
 * ina260.c — INA260 driver, two-sensor capable
 *
 *  M1 Step 2 rewrite. See ina260.h for why the API returns status
 *  separately from the value.
 */

#include "ina260.h"

/* ---- internal: read one 16-bit register ---- */
static uint8_t INA260_ReadReg16(INA260 *dev, uint8_t reg, uint16_t *out_raw)
{
    uint8_t data[2] = {0};

    if (dev == NULL || dev->bus == NULL) return 0;

    if (!I2C_Bus_ReadReg(dev->bus, INA260_ADDR, reg, data, 2))
    {
        dev->fail_count++;
        return 0;
    }

    *out_raw = (uint16_t)((data[0] << 8) | data[1]);
    return 1;
}

uint8_t INA260_Init(INA260 *dev, I2C_Bus *bus, const char *name)
{
    if (dev == NULL) return 0;

    dev->bus        = bus;
    dev->present    = 0;
    dev->fail_count = 0;
    dev->name       = name;

    uint16_t id = 0;
    if (!INA260_ReadReg16(dev, INA260_REG_MFR_ID, &id)) return 0;

    /* Reading the ID proves the right chip is on the right bus at the
       right address. Without this check a missing sensor reads as
       nothing and the BMS would run blind on that string. */
    dev->present = (id == INA260_MFR_ID_EXPECT) ? 1 : 0;
    return dev->present;
}

uint8_t INA260_ReadBusVoltage_V(INA260 *dev, float *out_V)
{
    uint16_t raw;
    if (out_V == NULL) return 0;
    if (!INA260_ReadReg16(dev, INA260_REG_BUSVOLT, &raw)) return 0;

    /* Unsigned: bus voltage cannot be negative.
       1.25 mV per LSB  ->  raw * 0.00125 V */
    *out_V = (float)raw * 0.00125f;
    return 1;
}

uint8_t INA260_ReadCurrent_mA(INA260 *dev, float *out_mA)
{
    uint16_t raw;
    if (out_mA == NULL) return 0;
    if (!INA260_ReadReg16(dev, INA260_REG_CURRENT, &raw)) return 0;

    /* SIGNED two's complement. The cast to int16_t is what makes
       discharge (negative, depending on IN+/IN- orientation) come back
       as a negative number instead of ~65000. */
    int16_t signed_raw = (int16_t)raw;

    /* 1.25 mA per LSB */
    *out_mA = (float)signed_raw * 1.25f;
    return 1;
}
