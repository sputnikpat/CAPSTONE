/*
 * bench_debug.c — manual switch commands over UART, FOR BENCH TESTS ONLY
 *
 *  M1 Step 4, updated in Step 5. See bench_debug.h.
 */

#include "bench_debug.h"
#include "bms_switch.h"
#include "bms_safety.h"
#include "bms_safety_cfg.h"
#include "telemetry.h"

/* Dead time between "all strings off" and "selected string on".
 * [CALCULATED, order of magnitude only] Gate turn-off is the 10 kOhm
 * pull-up charging the gate: tau ~ R x C_iss.
 * C_iss = 3400 pF typ at VGS = 0 V [IRF4905 datasheet, Infineon, dynamic
 * characteristics table]. tau = 10k x 3.4 nF = 34 us, 5 tau = 0.17 ms.
 * This ignores Miller-plateau charge, so treat it as a lower bound.
 * 20 ms is ~100x margin — cheap on a bench and not a performance claim. */
#define BENCH_BBM_MS          20u
#define BENCH_COOLDOWN_MS    500u
#define BENCH_INJECT_ARM_MS 3000u

static uint32_t last_cmd_ms = 0u;
static uint8_t  first_cmd   = 1u;
static uint8_t  inject_armed = 0u;
static uint32_t inject_arm_ms = 0u;

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

void BenchDebug_Poll(UART_HandleTypeDef *huart, uint8_t block_mask)
{
    uint32_t now = HAL_GetTick();

    /* An armed injection that nobody completed expires. */
    if (inject_armed && (now - inject_arm_ms) > BENCH_INJECT_ARM_MS)
    {
        inject_armed = 0u;
        Telemetry_Print("# INJECT disarmed (timeout)");
    }

    int c = bench_getc(huart);
    if (c < 0) return;
    if (c == '\r' || c == '\n' || c == ' ') return;

    /* '0' is always accepted, with no cooldown — turning things OFF
       must never be rate-limited. It also cancels an armed injection. */
    if (c == '0')
    {
        Switch_AllOff();
        inject_armed = 0u;
        Telemetry_Print("# CMD 0 -> ALL OFF");
        last_cmd_ms = now;
        first_cmd   = 0u;
        return;
    }

#if BMS_FAULT_INJECTION
    /* Injection is never blocked and never rate-limited: it can only
       make the system MORE conservative (it can only cause a trip). */
    if (c == 'f' || c == 'F')
    {
        inject_armed  = 1u;
        inject_arm_ms = now;
        Telemetry_Print("# INJECT armed: press a or b within 3 s");
        return;
    }
    if (inject_armed)
    {
        inject_armed = 0u;
        if (c == 'a' || c == 'A')
        {
            Safety_InjectOC(BMS_STR_A);
            Telemetry_Print("# INJECT OC -> STRING A (trips after debounce)");
            return;
        }
        if (c == 'b' || c == 'B')
        {
            Safety_InjectOC(BMS_STR_B);
            Telemetry_Print("# INJECT OC -> STRING B (trips after debounce)");
            return;
        }
        Telemetry_Print("# INJECT cancelled");
        return;
    }
#endif

    if (!first_cmd && (now - last_cmd_ms) < BENCH_COOLDOWN_MS)
    {
        Telemetry_Print("# REJECTED: cooldown 500 ms");
        return;
    }

    switch (c)
    {
        case 'a':
        case 'A':
            if (block_mask & SBLK_STR_A)
            {
                Telemetry_Print("# REJECTED: string A blocked by safety (RESET to clear)");
                return;
            }
            select_only(SW_STRING_A);
            Telemetry_Print("# CMD a -> STRING A ONLY");
            break;

        case 'b':
        case 'B':
            if (block_mask & SBLK_STR_B)
            {
                Telemetry_Print("# REJECTED: string B blocked by safety (RESET to clear)");
                return;
            }
            select_only(SW_STRING_B);
            Telemetry_Print("# CMD b -> STRING B ONLY");
            break;

        case 'l':
        case 'L':
            /* Turning the load OFF is always allowed; ON needs safety OK. */
            if (Switch_Get(SW_LOAD) == SW_ON)
            {
                Switch_Set(SW_LOAD, SW_OFF);
                Telemetry_Print("# CMD l -> LOAD OFF");
            }
            else if (block_mask & SBLK_LOAD)
            {
                Telemetry_Print("# REJECTED: load blocked by safety");
                return;
            }
            else
            {
                Switch_Set(SW_LOAD, SW_ON);
                Telemetry_Print("# CMD l -> LOAD ON");
            }
            break;

        default:
#if BMS_FAULT_INJECTION
            Telemetry_Print("# ? commands: 0 a b l  fa fb");
#else
            Telemetry_Print("# ? commands: 0 a b l");
#endif
            return;                         /* unknown: no cooldown */
    }

    last_cmd_ms = now;
    first_cmd   = 0u;
}
