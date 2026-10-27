#ifndef FAKE_OLED_H
#define FAKE_OLED_H
#include "main.h"
uint8_t OLED_Init(I2C_HandleTypeDef*); void OLED_Service(void); uint32_t OLED_ErrorCount(void);
#endif
