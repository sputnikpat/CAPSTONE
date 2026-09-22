/*
 * ina260.c
 *
 *  Created on: Aug 23, 2026
 *      Author: harsh
 *
 *  MODIFIED: Added INA260_ReadCurrent_mA()
 *
 *  Current register (0x01):
 *    - 16-bit signed (two's complement)
 *    - LSB = 1.25 mA
 *    - Positive = current flowing from IN+ to IN-
 *    >> VERIFY: INA260 datasheet, Section 7.6.1.2, Register 0x01
 */

#include "ina260.h"
#include "oled.h"   /* for OLED_I2C_Recover() prototype */

extern I2C_HandleTypeDef hi2c1;

#define INA260_MAX_RETRIES 2

/**
 * @brief  Read a 16-bit register from INA260 with retry + I2C recovery.
 * @param  reg   Register address (1 byte)
 * @param  data  Output buffer, must be at least 2 bytes [MSB, LSB]
 * @return 1 on success, 0 on failure after retries
 */
static uint8_t INA260_ReadReg(uint8_t reg, uint8_t *data)
{
    for (uint8_t attempt = 0; attempt < INA260_MAX_RETRIES; attempt++)
    {
        if (HAL_I2C_Master_Transmit(&hi2c1, INA260_ADDR, &reg, 1, 20) == HAL_OK &&
            HAL_I2C_Master_Receive(&hi2c1, INA260_ADDR, data, 2, 20) == HAL_OK)
        {
            return 1;   /* success */
        }

        OLED_I2C_Recover();   /* bus stuck / NACK -> bit-bang SCL, reinit I2C1 */
        HAL_Delay(2);
    }
    return 0;   /* failed after retries */
}

/**
 * @brief  Check INA260 manufacturer ID (expect 0x5449 = "TI").
 * @return 1 if ID matches, 0 on mismatch or I2C failure
 */
uint8_t INA260_Init(void)
{
    uint8_t data[2] = {0};
    if (!INA260_ReadReg(INA260_REG_MFR_ID, data))
        return 0;

    uint16_t id = (data[0] << 8) | data[1];
    return (id == 0x5449);
}

/**
 * @brief  Read bus voltage from INA260.
 *         Register 0x02, LSB = 1.25 mV, unsigned 16-bit.
 * @return Voltage in volts, or -1.0f on I2C failure
 */
float INA260_ReadBusVoltage_V(void)
{
    uint8_t data[2] = {0};
    if (!INA260_ReadReg(INA260_REG_BUSVOLT, data))
        return -1.0f;   /* sentinel = read failed */

    uint16_t raw = (data[0] << 8) | data[1];
    return raw * 0.00125f;   /* 1.25 mV per LSB */
}

/**
 * @brief  Read current from INA260.
 *         Register 0x01, LSB = 1.25 mA, SIGNED 16-bit (two's complement).
 *         Positive = current flowing IN+ to IN-.
 *
 *         VERIFY: INA260 datasheet Section 7.6.1.2
 *         The raw register is int16_t. Negative values mean reverse current.
 *
 * @return Current in milliamps, or -9999.0f on I2C failure.
 *         Using -9999 (not -1) because negative current is a valid reading.
 */
float INA260_ReadCurrent_mA(void)
{
    uint8_t data[2] = {0};
    if (!INA260_ReadReg(INA260_REG_CURRENT, data))
        return -9999.0f;   /* sentinel = read failed (distinct from negative current) */

    /* Two's complement: cast to signed 16-bit */
    int16_t raw = (int16_t)((data[0] << 8) | data[1]);
    return raw * 1.25f;    /* 1.25 mA per LSB */
}
