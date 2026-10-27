/*
 * bench_debug.c — UART bench commands
 *
 *  M1 Step 4, updated in Step 5 and Step 6. See bench_debug.h.
 */

#include "bench_debug.h"
#include "bms_switch.h"
#include "bms_safety.h"
#include "bms_safety_cfg.h"
#include "telemetry.h"

#define BENCH_INJECT_ARM_MS 3000u

static uint8_t  inject_armed = 0u;
static uint32_t inject_arm_ms = 0u;

#if BENCH_MANUAL_SWITCHING
/* Step 5 manual selector, bring-up only. See Step 5 for the reasoning. */
#define BENCH_BBM_MS          20u
#define BENCH_COOLDOWN_MS    500u
static uint32_t last_cmd_ms = 0u;
static uint8_t  first_cmd   = 1u;

static void select_only(SwitchId on_id)
{
    Switch_Set(SW_STRING_A, SW_OFF);
    Switch_Set(SW_STRING_B, SW_OFF);
    HAL_Delay(BENCH_BBM_MS);
    Switch_Set(on_id, SW_ON);
}
#endif

/* Read one received byte without blocking.
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

void BenchDebug_Poll(UART_HandleTypeDef *huart, uint8_t block_mask)
{
    uint32_t now = HAL_GetTick();
    (void)block_mask;

    if (inject_armed && (now - inject_arm_ms) > BENCH_INJECT_ARM_MS)
    {
        inject_armed = 0u;
        Telemetry_Print("# INJECT disarmed (timeout)");
    }

    int c = bench_getc(huart);
    if (c < 0) return;
    if (c == '\r' || c == '\n' || c == ' ') return;

    /* '0': emergency stop, always accepted, never rate-limited. */
    if (c == '0')
    {
        Switch_AllOff();
        inject_armed = 0u;
        Telemetry_Print("# CMD 0 -> ALL OFF (reconfig will LOCK OUT; RESET to restart)");
        return;
    }

#if BMS_FAULT_INJECTION
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

#if BENCH_MANUAL_SWITCHING
    if (!first_cmd && (now - last_cmd_ms) < BENCH_COOLDOWN_MS)
    {
        Telemetry_Print("# REJECTED: cooldown 500 ms");
        return;
    }
    switch (c)
    {
        case 'a': case 'A':
            if (block_mask & SBLK_STR_A) { Telemetry_Print("# REJECTED: A blocked"); return; }
            select_only(SW_STRING_A); Telemetry_Print("# CMD a -> STRING A ONLY"); break;
        case 'b': case 'B':
            if (block_mask & SBLK_STR_B) { Telemetry_Print("# REJECTED: B blocked"); return; }
            select_only(SW_STRING_B); Telemetry_Print("# CMD b -> STRING B ONLY"); break;
        case 'l': case 'L':
            if (Switch_Get(SW_LOAD) == SW_ON) { Switch_Set(SW_LOAD, SW_OFF); Telemetry_Print("# CMD l -> LOAD OFF"); }
            else if (block_mask & SBLK_LOAD)  { Telemetry_Print("# REJECTED: load blocked"); return; }
            else { Switch_Set(SW_LOAD, SW_ON); Telemetry_Print("# CMD l -> LOAD ON"); }
            break;
        default:
            Telemetry_Print("# ? commands: 0 a b l  fa fb");
            return;
    }
    last_cmd_ms = now;
    first_cmd   = 0u;
#else
#if BMS_FAULT_INJECTION
    Telemetry_Print("# ? commands: 0 (emergency stop)  f a / f b (inject OC)");
#else
    Telemetry_Print("# ? commands: 0 (emergency stop)");
#endif
#endif
}
