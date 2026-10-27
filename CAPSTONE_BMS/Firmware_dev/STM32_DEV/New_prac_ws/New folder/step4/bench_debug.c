/*
 * bench_debug.c — manual switch commands over UART, FOR BENCH TESTS ONLY
 *
 *  M1 Step 4. See bench_debug.h for the restrictions and why they exist.
 */

#include "bench_debug.h"
#include "bms_switch.h"
#include "telemetry.h"

/* Dead time between "all strings off" and "selected string on".
 * [CALCULATED, order of magnitude only] Gate turn-off is the 10 kOhm
 * pull-up charging the gate: tau ~ R x C_iss.
 * C_iss = 3400 pF typ at VGS = 0 V [IRF4905 datasheet, Infineon, dynamic
 * characteristics table]. tau = 10k x 3.4 nF = 34 us, 5 tau = 0.17 ms.
 * This ignores Miller-plateau charge, so treat it as a lower bound.
 * 20 ms is ~100x margin — cheap on a bench and not a performance claim. */
#define BENCH_BBM_MS        20u
#define BENCH_COOLDOWN_MS   500u

static uint32_t last_cmd_ms = 0u;
static uint8_t  first_cmd   = 1u;

/* Read one received byte without blocking.
 * RXNE set -> a byte is in DR. Reading SR then DR also clears an overrun
 * (ORE) on STM32F4 USART, so an ignored burst of keystrokes cannot wedge
 * the receiver.
 * >> VERIFY the flag/macro names in stm32f4xx_hal_uart.h and the ORE
 *    clearing sequence in RM0390, USART chapter (status register). */
static int bench_getc(UART_HandleTypeDef *huart)
{
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE))
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
    }
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE))
    {
        return (int)(huart->Instance->DR & 0xFFu);
    }
    return -1;
}

/* Only one string may ever be on. Off first, dead time, then on. */
static void select_only(SwitchId on_id)
{
    Switch_Set(SW_STRING_A, SW_OFF);
    Switch_Set(SW_STRING_B, SW_OFF);
    HAL_Delay(BENCH_BBM_MS);
    Switch_Set(on_id, SW_ON);
}

void BenchDebug_Poll(UART_HandleTypeDef *huart, uint8_t fault_latched)
{
    int c = bench_getc(huart);
    if (c < 0) return;

    if (c == '\r' || c == '\n' || c == ' ') return;

    /* '0' is always accepted, with no cooldown — turning things OFF
       must never be rate-limited. */
    if (c == '0')
    {
        Switch_AllOff();
        Telemetry_Print("# CMD 0 -> ALL OFF");
        last_cmd_ms = HAL_GetTick();
        first_cmd   = 0u;
        return;
    }

    if (fault_latched)
    {
        Telemetry_Print("# REJECTED: fault latched (reset to clear)");
        return;
    }

    uint32_t now = HAL_GetTick();
    if (!first_cmd && (now - last_cmd_ms) < BENCH_COOLDOWN_MS)
    {
        Telemetry_Print("# REJECTED: cooldown 500 ms");
        return;
    }

    switch (c)
    {
        case 'a':
        case 'A':
            select_only(SW_STRING_A);
            Telemetry_Print("# CMD a -> STRING A ONLY (verify current!)");
            break;

        case 'b':
        case 'B':
            select_only(SW_STRING_B);
            Telemetry_Print("# CMD b -> STRING B ONLY (verify current!)");
            break;

        case 'l':
        case 'L':
            Switch_Set(SW_LOAD, (Switch_Get(SW_LOAD) == SW_ON) ? SW_OFF : SW_ON);
            Telemetry_Print((Switch_Get(SW_LOAD) == SW_ON) ? "# CMD l -> LOAD ON"
                                                           : "# CMD l -> LOAD OFF");
            break;

        default:
            Telemetry_Print("# ? commands: 0 a b l");
            return;                         /* unknown: no cooldown */
    }

    last_cmd_ms = now;
    first_cmd   = 0u;
}
