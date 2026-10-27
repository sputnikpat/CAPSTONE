/* HOST-SIM stand-in for bms_switch.h: same API, no HAL. Not firmware. */
#ifndef BMS_SWITCH_H
#define BMS_SWITCH_H
#include <stdint.h>

typedef enum { SW_STRING_A = 0, SW_STRING_B, SW_LOAD, SW_COUNT } SwitchId;
typedef enum { SW_OFF = 0, SW_ON = 1 } SwitchState;

void        Switch_Init(void);
uint8_t     Switch_Set(SwitchId id, SwitchState state);
SwitchState Switch_Get(SwitchId id);
void        Switch_AllOff(void);
const char *Switch_Name(SwitchId id);

/* sim only */
extern uint8_t sim_odr[SW_COUNT];
extern void  (*sim_on_hook)(SwitchId id);   /* called BEFORE a switch turns on */
extern void  (*sim_change_hook)(void);      /* called after any ODR change     */
#endif
