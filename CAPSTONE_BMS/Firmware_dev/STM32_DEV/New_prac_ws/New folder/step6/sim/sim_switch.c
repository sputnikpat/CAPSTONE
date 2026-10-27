/* HOST-SIM stand-in for bms_switch.c. Emulates the GPIO output register. */
#include "bms_switch.h"
#include <stddef.h>

uint8_t sim_odr[SW_COUNT];
void  (*sim_on_hook)(SwitchId id) = NULL;
void  (*sim_change_hook)(void) = NULL;

void Switch_Init(void) { for (int k = 0; k < SW_COUNT; k++) sim_odr[k] = 0; }

uint8_t Switch_Set(SwitchId id, SwitchState st)
{
    if (id >= SW_COUNT) return 0;
    uint8_t v = (st == SW_ON) ? 1u : 0u;
    if (v && !sim_odr[id] && sim_on_hook) sim_on_hook(id);
    if (sim_odr[id] != v) { sim_odr[id] = v; if (sim_change_hook) sim_change_hook(); }
    return 1;
}

SwitchState Switch_Get(SwitchId id) { return (id < SW_COUNT && sim_odr[id]) ? SW_ON : SW_OFF; }

void Switch_AllOff(void)
{
    uint8_t ch = 0;
    for (int k = 0; k < SW_COUNT; k++) { if (sim_odr[k]) ch = 1; sim_odr[k] = 0; }
    if (ch && sim_change_hook) sim_change_hook();
}

const char *Switch_Name(SwitchId id) { static const char *n[] = {"STR A","STR B","LOAD"}; return id < SW_COUNT ? n[id] : "?"; }
