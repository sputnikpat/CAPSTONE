/*
 * i2c_bus.h — shared I2C bus recovery
 *
 * Both I2C1 (OLED + INA260 String A) and I2C3 (INA260 String B)
 * need the same stuck-bus recovery. The original OLED_I2C_Recover()
 * was hardcoded to I2C1 and PB8; this generalises it.
 *
 * Why recovery is needed: an I2C slave that was mid-transaction when
 * the master reset will hold SDA low forever, hanging the bus. Pulsing
 * SCL nine times lets it finish its byte and release SDA.
 *
 *  >> VERIFY: pin assignments below against STM32F446RE_Pin_Map_v3.md
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "main.h"

/* ---- Bus descriptors ----
 * SCL pin is needed for bit-banging during recovery.
 * I2C1_SCL = PB8   (OLED + INA260 String A)
 * I2C3_SCL = PA8   (INA260 String B)
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;        /* HAL handle                        */
    GPIO_TypeDef      *scl_port;    /* GPIO port of SCL                  */
    uint16_t           scl_pin;     /* GPIO pin  of SCL                  */
    void             (*reinit)(void); /* MX_I2Cx_Init for this bus       */
    uint32_t           recover_count; /* diagnostics: times recovered    */
} I2C_Bus;

/* Declared in main.c / i2c_bus.c — one per physical bus */
extern I2C_Bus bus_i2c1;
extern I2C_Bus bus_i2c3;

/**
 * @brief  Unstick a hung I2C bus: deinit, bit-bang 9 SCL pulses, reinit.
 *         Safe to call when the bus is healthy (costs ~20 ms).
 */
void I2C_Bus_Recover(I2C_Bus *bus);

/**
 * @brief  Transmit a register address then read n bytes, with retries.
 * @param  bus    bus descriptor
 * @param  addr8  8-bit HAL address (7-bit << 1)
 * @param  reg    register address
 * @param  data   output buffer
 * @param  len    bytes to read
 * @return 1 on success, 0 on failure after all retries
 */
uint8_t I2C_Bus_ReadReg(I2C_Bus *bus, uint8_t addr8, uint8_t reg,
                        uint8_t *data, uint16_t len);

#endif /* I2C_BUS_H */
