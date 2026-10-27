/*
 * bms_switch.c — low-level MOSFET switch control
 *
 *  M1 Step 3. See bms_switch.h for scope and hardware notes.
 */

#include "bms_switch.h"

/* Lookup so SwitchId -> pin stays in one place */
static const uint16_t sw_pin[SW_COUNT] = {
    [SW_STRING_A] = SW_STRING_A_PIN,
    [SW_STRING_B] = SW_STRING_B_PIN,
    [SW_LOAD]     = SW_LOAD_PIN,
};

static const char * const sw_name[SW_COUNT] = {
    [SW_STRING_A] = "STR A",
    [SW_STRING_B] = "STR B",
    [SW_LOAD]     = "LOAD",
};

void Switch_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Drive LOW *before* switching the pins to output mode, so there is
       no window where the port drives an undefined level. BSRR upper
       half-word = atomic reset. */
    SW_PORT->BSRR = (uint32_t)SW_ALL_PINS << 16u;

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = SW_ALL_PINS;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;        /* 100k base pull-downs are external */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;/* ms-scale switching; slow edges are
                                        preferable here, less ringing */
    HAL_GPIO_Init(SW_PORT, &gpio);

    /* And again after init, belt and braces. */
    SW_PORT->BSRR = (uint32_t)SW_ALL_PINS << 16u;
}

uint8_t Switch_Set(SwitchId id, SwitchState state)
{
    if (id >= SW_COUNT) return 0;

    if (state == SW_ON)
        SW_PORT->BSRR = (uint32_t)sw_pin[id];          /* set   */
    else
        SW_PORT->BSRR = (uint32_t)sw_pin[id] << 16u;   /* reset */

    return 1;
}

SwitchState Switch_Get(SwitchId id)
{
    if (id >= SW_COUNT) return SW_OFF;

    /* ODR = what we commanded. Deliberately not IDR: in output mode IDR
       reflects the pad, which on a shorted or damaged driver could read
       back the commanded value even when the MOSFET never moved. True
       validation is sensor-based, upstream of this module. */
    return (SW_PORT->ODR & sw_pin[id]) ? SW_ON : SW_OFF;
}

void Switch_AllOff(void)
{
    /* Single atomic write, no HAL, no loop, no function pointers.
       Callable from an ISR, a fault handler, or Error_Handler() after
       HAL has already failed. GPIOB clock is enabled in Switch_Init();
       if that never ran, the pins are still inputs and the external
       100k pull-downs hold the NPNs off anyway. */
    SW_PORT->BSRR = (uint32_t)SW_ALL_PINS << 16u;
}

const char *Switch_Name(SwitchId id)
{
    if (id >= SW_COUNT) return "?";
    return sw_name[id];
}
