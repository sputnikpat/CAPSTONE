/*
 * ina260.h
 *
 *  Created on: Aug 23, 2026
 *      Author: harsh
 */
#ifndef INA260_H
#define INA260_H
#include "main.h"

#define INA260_ADDR        (0x40 << 1)   // 8-bit HAL address
#define INA260_REG_BUSVOLT  0x02
#define INA260_REG_MFR_ID   0xFE

uint8_t INA260_Init(void);              // checks manufacturer ID
float   INA260_ReadBusVoltage_V(void);  // returns volts

#endif
