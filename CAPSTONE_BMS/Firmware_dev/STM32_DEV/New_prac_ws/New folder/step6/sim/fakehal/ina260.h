#ifndef FAKE_INA260_H
#define FAKE_INA260_H
#include "i2c_bus.h"
typedef struct { uint32_t fail_count; } INA260;
uint8_t INA260_Init(INA260*, I2C_Bus*, const char*); uint8_t INA260_ReadBusVoltage_V(INA260*, float*); uint8_t INA260_ReadCurrent_mA(INA260*, float*);
#endif
