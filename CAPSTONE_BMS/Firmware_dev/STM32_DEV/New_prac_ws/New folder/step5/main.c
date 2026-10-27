/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Reconfigurable BMS — M1 Step 5 (sensing + safety layer)
  ******************************************************************************
  *
  *  WHAT THIS FIRMWARE DOES
  *    - Step 4 sensing + display, unchanged.
  *    - Step 5 safety layer (bms_safety.c) runs right after sensing, before
  *      anything else: OV / UV / OC (both directions) per string, sensor
  *      loss, stuck-switch readback. 3-sample debounce, latched faults.
  *    - Faults clear ONLY by MCU reset (Nucleo black RESET button B2).
  *    - Independent watchdog (IWDG): if the loop hangs, the MCU resets and
  *      boots with every switch off.
  *    - Reset cause (button / power-on / watchdog / brown-out) is printed
  *      on UART at boot.
  *    - Any NEW fault stops the motors (motor_running = 0). Press PC13 to
  *      drive again once a healthy string is selected.
  *    - Still NO reconfiguration logic. Switches move only by hand via
  *      bench_debug (one string at a time), and only when safety allows.
  *
  *  LOOP TIMING [CALCULATED — BENCH-MEASURE via work_us_max]
  *    period 20 ms, tick-scheduled. Safety_Step adds integer compares
  *    only (microseconds), so the Step 4 budget (~15.4 ms worst healthy
  *    case) is unchanged.
  *
  *  CUBEMX — REQUIRED
  *    - I2C3 on PA8 (SCL) / PC9 (SDA), 100 kHz        (from Step 4)
  *    - SYS -> Debug -> Serial Wire                     (frees PB4 AND PB3)
  *    - Do NOT enable IWDG in CubeMX. It is started by hand just before
  *      the main loop (see Watchdog_Start). A CubeMX-generated
  *      MX_IWDG_Init() would start it during init, and the 1.5 s boot
  *      screen delay below would then reset the board forever.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "i2c_bus.h"
#include "ina260.h"
#include "bms_switch.h"
#include "bms_status.h"
#include "bms_safety.h"
#include "oled.h"
#include "bms_display.h"
#include "telemetry.h"
#include "bench_debug.h"
#include "motor.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BMS_BENCH_DEBUG     1      /* 0 = compile out manual switch commands */

#define LOOP_PERIOD_MS      20u
#define SLOT_EVERY          10u    /* 10 x 20 ms = 200 ms -> 5 Hz            */
#define SLOT_TELEMETRY      0u     /* loop % 10 == 0 : send CSV line          */
#define SLOT_RENDER         5u     /* loop % 10 == 5 : redraw OLED framebuffer */
#define LED_EVERY           25u    /* 25 x 20 ms = 500 ms toggle -> 1 Hz blink */

/*
 * IWDG sizing [CALCULATED]. Timeout = (RLR + 1) x prescaler / f_LSI.
 *   prescaler 32 (PR = 3), RLR = 499:
 *     f_LSI typ 32 kHz -> 500 x 32 / 32000 = 0.500 s
 *     f_LSI max 47 kHz -> 500 x 32 / 47000 = 0.340 s   <- shortest timeout
 *     f_LSI min 17 kHz -> 500 x 32 / 17000 = 0.941 s   <- slowest detection
 *   >> VERIFY the LSI min/typ/max in the STM32F446 datasheet (DS10693,
 *      "LSI oscillator characteristics"). The 17/32/47 kHz values are from
 *      memory, not from a file in this project.
 *   Worst legitimate loop: both sensors dead (2 x ~80 ms, Step 4 notes)
 *     + OLED page timeout 25 ms + CSV 9.5 ms + bench dead time 20 ms
 *     + a few UART prints (~4 ms each)              ~= 230 ms < 340 ms.
 */
#define IWDG_PR_DIV32       3u
#define IWDG_RELOAD         499u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/*
 * motor_running: PC13 user button toggles it. Safety forces it to 0 on any
 * new fault. Single-byte write from main and the ISR: atomic on Cortex-M4.
 */
volatile uint8_t motor_running = 0;

INA260 ina_a;   /* String A, on I2C1 alongside the OLED */
INA260 ina_b;   /* String B, on I2C3                    */

static BmsStatus st;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C3_Init(void);
/* USER CODE BEGIN PFP */
static void   I2C1_Reinit(void);
static void   I2C3_Reinit(void);
static void   Sense_String(INA260 *dev, BmsStringSnap *s, uint32_t now);
static int32_t Round_mA(float mA);
static const char *Topo_FromCommands(uint8_t a, uint8_t b);
static void   Cycle_Counter_Init(void);
static const char *Reset_Cause_ReadAndClear(void);
static void   Watchdog_Start(void);
static inline void Watchdog_Kick(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void I2C1_Reinit(void) { MX_I2C1_Init(); }
static void I2C3_Reinit(void) { MX_I2C3_Init(); }

I2C_Bus bus_i2c1 = { &hi2c1, GPIOB, GPIO_PIN_8, I2C1_Reinit, 0 };  /* SCL PB8 */
I2C_Bus bus_i2c3 = { &hi2c3, GPIOA, GPIO_PIN_8, I2C3_Reinit, 0 };  /* SCL PA8 */

static int32_t Round_mA(float mA)
{
    return (mA >= 0.0f) ? (int32_t)(mA + 0.5f) : -(int32_t)(-mA + 0.5f);
}

/* Unchanged from Step 4. valid = 1 only if BOTH reads succeeded this call. */
static void Sense_String(INA260 *dev, BmsStringSnap *s, uint32_t now)
{
    float v_V = 0.0f, i_mA = 0.0f;

    uint8_t ok = INA260_ReadBusVoltage_V(dev, &v_V)
              && INA260_ReadCurrent_mA (dev, &i_mA);

    s->valid = ok;
    if (ok)
    {
        s->v_mV       = (int32_t)(v_V * 1000.0f + 0.5f);
        s->i_mA       = Round_mA(i_mA);
        s->last_ok_ms = now;
    }
    s->fail_count = dev->fail_count;
}

static const char *Topo_FromCommands(uint8_t a, uint8_t b)
{
    if (a && b) return "A+B";
    if (a)      return "A";
    if (b)      return "B";
    return "OFF";
}

static void Cycle_Counter_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t cycles_to_us(uint32_t cyc)
{
    return cyc / (SystemCoreClock / 1000000u);
}

/*
 * Why did we just reset? Read RCC->CSR reset flags, then clear them
 * (they are sticky across resets until cleared by RMVF).
 *
 * ORDER MATTERS:
 *   - PINRST is checked LAST: on STM32F4 the internal reset pulse is also
 *     driven onto the NRST pin, so the pin flag accompanies other resets.
 *   - PORRST before BORRST: a power-on also sets the BOR flag.
 * >> VERIFY both statements and the macro names (__HAL_RCC_GET_FLAG,
 *    RCC_FLAG_xxxRST, __HAL_RCC_CLEAR_RESET_FLAGS) in RM0390 (RCC_CSR
 *    register description) and stm32f4xx_hal_rcc.h. If a name differs,
 *    the build fails loudly — it cannot silently misbehave.
 */
static const char *Reset_Cause_ReadAndClear(void)
{
    const char *c;

    if      (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) c = "WATCHDOG (loop hung!)";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) c = "WINDOW WATCHDOG";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) c = "LOW-POWER";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))  c = "SOFTWARE";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST))  c = "POWER-ON";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST))  c = "BROWN-OUT";
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))  c = "RESET BUTTON / NRST";
    else                                           c = "UNKNOWN";

    __HAL_RCC_CLEAR_RESET_FLAGS();
    return c;
}

/*
 * Independent watchdog, started by direct register writes so it needs no
 * HAL IWDG module and no CubeMX setting (see header for why CubeMX must
 * NOT start it).
 *
 * Key register (IWDG_KR) values [VERIFY: RM0390, IWDG chapter]:
 *   0xCCCC  start the watchdog (also forces LSI on). Cannot be stopped
 *           afterwards except by reset — this is intentional.
 *   0x5555  unlock PR and RLR for writing
 *   0xAAAA  reload the counter ("kick")
 * PR/RLR writes are applied in the LSI clock domain; SR.PVU / SR.RVU stay
 * set until they are. We wait (bounded) so the new timeout is really in
 * force before the loop starts.
 */
static void Watchdog_Start(void)
{
    /* Freeze IWDG while the debugger halts the core, otherwise every
       breakpoint resets the board. No effect when no debugger is attached.
       >> VERIFY register/bit name in stm32f446xx.h (DBGMCU_APB1_FZ). */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    IWDG->KR  = 0xCCCCu;            /* start (default timeout ~0.5 s)    */
    IWDG->KR  = 0x5555u;            /* unlock PR/RLR                     */
    IWDG->PR  = IWDG_PR_DIV32;      /* LSI / 32                          */
    IWDG->RLR = IWDG_RELOAD;        /* 12-bit reload                     */

    uint32_t t0 = HAL_GetTick();
    while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0u)
    {
        if ((HAL_GetTick() - t0) > 50u) break;   /* never hang here */
    }
    IWDG->KR = 0xAAAAu;             /* first kick with the new values    */
}

static inline void Watchdog_Kick(void)
{
    IWDG->KR = 0xAAAAu;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* FIRST user action: every switch control pin LOW. */
  Switch_Init();
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  /* USER CODE BEGIN 2 */

  Switch_AllOff();                    /* re-assert after MX_GPIO_Init */

  Motor_GPIO_Init();                  /* motors stopped */
  Cycle_Counter_Init();
  Telemetry_Init(&huart2);

  {
      char line[64];
      snprintf(line, sizeof(line), "# RESET CAUSE: %s", Reset_Cause_ReadAndClear());
      Telemetry_Print(line);
  }

  uint8_t oled_ok = OLED_Init(&hi2c1);

  /* Safety starts BEFORE the probe so a missing sensor latches into it. */
  Safety_Init(HAL_GetTick());

  uint8_t ok_a = INA260_Init(&ina_a, &bus_i2c1, "A");
  uint8_t ok_b = INA260_Init(&ina_b, &bus_i2c3, "B");

  Telemetry_Print(oled_ok ? "# OLED OK" : "# OLED FAIL (isolated, will retry)");
  Telemetry_Print(ok_a ? "# INA A OK" : "# INA A MISSING");
  Telemetry_Print(ok_b ? "# INA B OK" : "# INA B MISSING");

  Display_Boot(ok_a, ok_b);

  if (!ok_a) Safety_LatchSensorAtBoot(BMS_STR_A);
  if (!ok_b) Safety_LatchSensorAtBoot(BMS_STR_B);
  /* No halt: the loop keeps running so the screen and CSV keep reporting.
     With a sensor fault latched, Safety_Step forces all-off every cycle and
     Safety_BlockMask() blocks every switch command. */

  HAL_Delay(1500);                    /* boot screen; IWDG NOT running yet */

  st.topo_confirmed = 0u;
  uint8_t ready_reported = 0u;

  Watchdog_Start();                   /* from here on the loop MUST kick */
  uint32_t next_ms = HAL_GetTick();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

      /* ---- 0. Wait for this iteration's slot (no HAL_Delay) ----
         The wait itself kicks nothing: if the tick stops, the loop body
         never runs and the watchdog resets us. */
      uint32_t now = HAL_GetTick();
      if ((int32_t)(now - next_ms) < 0) continue;

      next_ms += LOOP_PERIOD_MS;
      if ((int32_t)(now - next_ms) >= 0)
      {
          st.overruns++;
          next_ms = now + LOOP_PERIOD_MS;
      }

      uint32_t t_start = DWT->CYCCNT;
      st.loop_count++;
      st.tick_ms = now;

      /* ---- 1. SENSE — always first ---- */
      Sense_String(&ina_a, &st.str[BMS_STR_A], now);
      Sense_String(&ina_b, &st.str[BMS_STR_B], now);

      /* ---- 2. SAFETY — before anything that could move a switch ----
         Safety_Step opens faulted strings itself, every cycle. */
      if (Safety_Step(st.str, now))
      {
          /* New fault this cycle: stop the motors so that when power
             returns (healthy string selected) the rover does not restart
             on its own into a stalled-motor inrush. */
          motor_running = 0u;
          Motor_Stop();

          char line[64];
          snprintf(line, sizeof(line), "# TRIP: %s  flags=0x%04lX",
                   Safety_Label(), (unsigned long)Safety_Flags());
          Telemetry_Print(line);
      }
      if (Safety_AllStringsDown()) motor_running = 0u;  /* ignore PC13 */

      if (!ready_reported && Safety_Ready())
      {
          Telemetry_Print("# SAFETY READY: switching allowed");
          ready_reported = 1u;
      }

      /* ---- 3. Manual bench commands, gated by safety ---- */
#if BMS_BENCH_DEBUG
      BenchDebug_Poll(&huart2, Safety_BlockMask());
#endif

      /* ---- 4. Fill the rest of the snapshot ---- */
      st.sw_a    = (Switch_Get(SW_STRING_A) == SW_ON) ? 1u : 0u;
      st.sw_b    = (Switch_Get(SW_STRING_B) == SW_ON) ? 1u : 0u;
      st.sw_load = (Switch_Get(SW_LOAD)     == SW_ON) ? 1u : 0u;
      st.topo_label   = Topo_FromCommands(st.sw_a, st.sw_b);
      st.fault_flags  = Safety_Flags();
      st.fault_label  = Safety_Label();
      st.motor_on     = motor_running;
      st.i2c1_recover = bus_i2c1.recover_count;
      st.i2c3_recover = bus_i2c3.recover_count;
      st.oled_err     = OLED_ErrorCount();

      /* ---- 5. Outputs: CSV OR one OLED page, never both in one slot ---- */
      uint32_t slot = st.loop_count % SLOT_EVERY;
      if (slot == SLOT_TELEMETRY)
      {
          Telemetry_Send(&st);
      }
      else
      {
          if (slot == SLOT_RENDER) Display_Render(&st);

          uint32_t t_oled = DWT->CYCCNT;
          OLED_Service();
          uint32_t oled_us = cycles_to_us(DWT->CYCCNT - t_oled);
          if (oled_us > st.oled_us_max) st.oled_us_max = oled_us;
      }

      /* ---- 6. Motors ---- */
      if (motor_running) Motor_Forward(); else Motor_Stop();

      /* ---- 7. Heartbeat LED ---- */
      if ((st.loop_count % LED_EVERY) == 0u)
          HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);

      uint32_t work_us = cycles_to_us(DWT->CYCCNT - t_start);
      if (work_us > st.work_us_max) st.work_us_max = work_us;

      /* ---- 8. Kick the watchdog LAST: only a loop that got all the way
         through sensing AND safety earns another 340+ ms of life. ---- */
      Watchdog_Kick();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_I2C3_Init(void)
{
  hi2c3.Instance = I2C3;
  hi2c3.Init.ClockSpeed = 100000;
  hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* Switch pins PB4/PB5/PB10 are owned by Switch_Init() (bms_switch.c). */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
 * @brief  PC13 user button toggles motor_running. 300 ms lockout debounce.
 *         Ignored while no string can run (safety would only stop it again).
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13)
    {
        static uint32_t last_press = 0;
        uint32_t now = HAL_GetTick();

        if ((now - last_press) > 300)
        {
            if (!Safety_AllStringsDown())
                motor_running = !motor_running;
            last_press = now;
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* SAFE STATE FIRST — one direct BSRR write, no HAL. */
  SW_PORT->BSRR = (uint32_t)SW_ALL_PINS << 16u;

  /* If the watchdog is already running, this loop starves it and the MCU
     resets ~0.34-0.94 s later, booting into all-off again. That is the
     intended recovery: safe, and the next boot prints "RESET CAUSE:
     WATCHDOG". Before Watchdog_Start() it simply halts here, switches off. */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
