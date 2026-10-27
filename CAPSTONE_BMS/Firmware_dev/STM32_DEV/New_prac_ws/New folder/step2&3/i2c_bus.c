/*
 * i2c_bus.c — shared I2C bus recovery
 *
 *  Created for: Reconfigurable BMS, M1 Step 2
 *
 *  Replaces the hardcoded OLED_I2C_Recover() with a per-bus version so
 *  I2C1 and I2C3 can both be recovered independently.
 */

#include "i2c_bus.h"

#define I2C_MAX_RETRIES   2
#define I2C_TIMEOUT_MS    20    /* never HAL_MAX_DELAY in a control loop */

void I2C_Bus_Recover(I2C_Bus *bus)
{
    if (bus == NULL || bus->hi2c == NULL) return;

    HAL_I2C_DeInit(bus->hi2c);

    /* Drive SCL manually as open-drain so we don't fight a slave
       that is holding SDA low. 9 pulses = one full byte + ACK slot,
       which is the worst case for a slave stuck mid-transfer. */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin   = bus->scl_pin;
    HAL_GPIO_Init(bus->scl_port, &gpio);

    for (int i = 0; i < 9; i++)
    {
        HAL_GPIO_WritePin(bus->scl_port, bus->scl_pin, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(bus->scl_port, bus->scl_pin, GPIO_PIN_SET);
        HAL_Delay(1);
    }

    /* Restore the peripheral (also restores SCL/SDA to AF mode) */
    if (bus->reinit != NULL) bus->reinit();

    bus->recover_count++;
}

uint8_t I2C_Bus_ReadReg(I2C_Bus *bus, uint8_t addr8, uint8_t reg,
                        uint8_t *data, uint16_t len)
{
    if (bus == NULL || bus->hi2c == NULL || data == NULL) return 0;

    for (uint8_t attempt = 0; attempt < I2C_MAX_RETRIES; attempt++)
    {
        if (HAL_I2C_Master_Transmit(bus->hi2c, addr8, &reg, 1,
                                    I2C_TIMEOUT_MS) == HAL_OK &&
            HAL_I2C_Master_Receive (bus->hi2c, addr8, data, len,
                                    I2C_TIMEOUT_MS) == HAL_OK)
        {
            return 1;
        }

        I2C_Bus_Recover(bus);
        HAL_Delay(2);
    }

    return 0;   /* caller MUST treat this as a fault, not a zero reading */
}
