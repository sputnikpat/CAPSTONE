#ifndef FAKE_BMS_SWITCH_H
#define FAKE_BMS_SWITCH_H
#include "main.h"
#define SW_PORT GPIOB
#define SW_STRING_A_PIN GPIO_PIN_4
#define SW_STRING_B_PIN GPIO_PIN_5
#define SW_LOAD_PIN GPIO_PIN_10
#define SW_ALL_PINS (SW_STRING_A_PIN | SW_STRING_B_PIN | SW_LOAD_PIN)
typedef enum { SW_STRING_A = 0, SW_STRING_B, SW_LOAD, SW_COUNT } SwitchId;
typedef enum { SW_OFF = 0, SW_ON = 1 } SwitchState;
void Switch_Init(void); uint8_t Switch_Set(SwitchId, SwitchState); SwitchState Switch_Get(SwitchId); void Switch_AllOff(void);
#endif
