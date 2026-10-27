#ifndef FAKE_I2C_BUS_H
#define FAKE_I2C_BUS_H
#include "main.h"
typedef struct { I2C_HandleTypeDef *h; GPIO_TypeDef *p; uint32_t pin; void (*re)(void); uint32_t recover_count; } I2C_Bus;
#endif
