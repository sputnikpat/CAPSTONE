/*
 * ina260.h
 *
 *  Created on: Aug 23, 2026
 *      Author: harsh
 *
 *  MODIFIED: Added current measurement support (register 0x01)
 *
 *  INA260 register 0x01 = Current register, LSB = 1.25 mA
 *  >> VERIFY this against your INA260 datasheet (Table 2, Register Map)
 */

#ifndef INA260_H
#define INA260_H

#include "main.h"

#define INA260_ADDR         (0x40 << 1)   /* 8-bit HAL address (7-bit 0x40) */

/* ---- Register addresses ---- */
#define INA260_REG_CURRENT   0x01         /* Current result, LSB = 1.25 mA  */
#define INA260_REG_BUSVOLT   0x02         /* Bus voltage,    LSB = 1.25 mV  */
#define INA260_REG_MFR_ID    0xFE         /* Manufacturer ID (expect 0x5449)*/

/* ---- Functions ---- */
uint8_t INA260_Init(void);               /* Checks manufacturer ID         */
float   INA260_ReadBusVoltage_V(void);   /* Returns volts, -1.0 on error   */
float   INA260_ReadCurrent_mA(void);     /* Returns milliamps, -1.0 on err */

#endif /* INA260_H */
