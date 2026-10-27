/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Reconfigurable BMS — M1 Step 4 (sensing + display only)
  ******************************************************************************
  *
  *  WHAT THIS FIRMWARE DOES
  *    - Integrates M1 Steps 2/3 (two INA260s on two buses, switch module,
  *      safe Error_Handler)            -> sub-step 4.0
  *    - Shows both strings live on the OLED and over UART CSV  -> Step 4
  *    - Contains NO control logic. The only automatic action is the
  *      existing integration rule: a sensor that cannot be read means all
  *      switches off, latched until reset. OV/UV/OC arrive in Step 5.
  *    - Switches can be moved only by hand via bench_debug (UART), and
  *      never both strings at once.
  *
  *  LOOP TIMING [CALCULATED — to be BENCH-MEASURED via work_us_max]
  *    period 20 ms, tick-scheduled (no HAL_Delay in the loop)
  *      sensors : 4 register reads x (2-byte write + 2-byte read)
  *                ~ 4 x 0.6 ms                          = ~2.4 ms
  *      then EITHER one OLED page (~12 ms + 3 cmds ~1 ms) = ~13 ms
  *          OR      one CSV line (~110 chars @115200)   = ~9.5 ms
  *      worst case ~2.4 + 13                            = ~15.4 ms < 20 ms
  *    OLED content redraw 5 Hz, CSV 5 Hz, LED 1 Hz.
  *
  *  CUBEMX — REQUIRED BEFORE THIS FILE BUILDS AND RUNS CORRECTLY
  *    - Connectivity -> I2C3: I2C, 100 kHz, pins PA8 (SCL) / PC9 (SDA)
  *      Regenerate so stm32f4xx_hal_msp.c gets the I2C3 pin/clock setup.
  *      Without that, I2C3 has no pins, sensor B fails its probe, and the
  *      firmware refuses to run (safe, but you'll see INA B: MISSING).
  *    - SYS -> Debug -> Serial Wire (frees PB4 from NJTRST)
  *  The MX_I2C3_Init() below is written the way CubeMX generates it, so
  *  regeneration will produce the same function rather than a conflict.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "i2c_bus.h"
#include "ina260.h"
#include "bms_switch.h"
#include "bms_status.h"
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

/* Step 4 fault bits. Step 5 replaces these with its own definitions. */
#define FAULT_SENSOR_A      (1u << 0)
#define FAULT_SENSOR_B      (1u << 1)
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
 * motor_running: PC13 user button toggles it (unchanged from the rover
 * code). NOTE: pin map v3 reserves PC13 as the emergency-stop input —
 * this conflict must be settled before the rover demo (M3).
 */
volatile uint8_t motor_running = 0;

INA260 ina_a;   /* String A, on I2C1 alongside the OLED */
INA260 ina_b;   /* String B, on I2C3                    */

static BmsStatus st;
static uint32_t  fault_latched = 0u;
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
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * Bus descriptors. The reinit pointer goes through a wrapper in THIS file
 * so MX_I2Cx_Init can stay `static` exactly as CubeMX generates it —
 * previously the prototype had to be hand-edited to non-static outside a
 * USER CODE block, which CubeMX silently reverts on regeneration.
 */
static void I2C1_Reinit(void) { MX_I2C1_Init(); }
static void I2C3_Reinit(void) { MX_I2C3_Init(); }

I2C_Bus bus_i2c1 = { &hi2c1, GPIOB, GPIO_PIN_8, I2C1_Reinit, 0 };  /* SCL PB8 */
I2C_Bus bus_i2c3 = { &hi2c3, GPIOA, GPIO_PIN_8, I2C3_Reinit, 0 };  /* SCL PA8 */

/* Float mA from the driver -> integer mA, half away from zero.
   INA260 current is an integer multiple of 1.25 mA, so this only decides
   the .25/.75 cases. No libm. */
static int32_t Round_mA(float mA)
{
    return (mA >= 0.0f) ? (int32_t)(mA + 0.5f) : -(int32_t)(-mA + 0.5f);
}

/*
 * Read both registers of one sensor into its snapshot.
 * valid = 1 only if BOTH reads succeeded THIS call; otherwise the old
 * numbers stay in the struct but are flagged stale (display shows LOST,
 * CSV leaves the fields empty).
 * Worst case when a sensor is dead [CALCULATED]: per register,
 * 2 attempts x (20 ms timeout + >=18 ms recovery + 2 ms) = ~80 ms. The
 * V read failing short-circuits the I read, so ~80 ms per dead sensor per
 * loop. That overruns the 20 ms slot — acceptable because a dead sensor
 * has already forced all switches off.
 */
static void Sense_String(INA260 *dev, BmsStringSnap *s, uint32_t now)
{
    float v_V = 0.0f, i_mA = 0.0f;

    uint8_t ok = INA260_ReadBusVoltage_V(dev, &v_V)
              && INA260_ReadCurrent_mA (dev, &i_mA);

    s->valid = ok;
    if (ok)
    {
        s->v_mV       = (int32_t)(v_V * 1000.0f + 0.5f);  /* bus V >= 0 */
        s->i_mA       = Round_mA(i_mA);
        s->last_ok_ms = now;
    }
    s->fail_count = dev->fail_count;
}

/*
 * Topology label from COMMANDED states only. Deliberately trivial:
 * it is a lookup of what we asked for, not a decision and not a
 * confirmation. topo_confirmed stays 0 ("CMD") until Step 6 supplies a
 * sensor-validated label.
 */
static const char *Topo_FromCommands(uint8_t a, uint8_t b)
{
    if (a && b) return "A+B";
    if (a)      return "A";
    if (b)      return "B";
    return "OFF";
}

/* DWT cycle counter for loop-time measurement (Cortex-M4 core debug
   block, CMSIS core_cm4.h names). 84 MHz SYSCLK -> 1 cycle = 11.9 ns,
   CYCCNT wraps every 2^32 / 84e6 = ~51 s; we only ever take short
   differences, so unsigned subtraction handles the wrap. */
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

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* FIRST user action: drive every switch control pin LOW. This is the
     earliest point user code can run after the clock is up. Before this
     line the pins are in their reset state — see the PB4 note in the
     Step 4 notes (NJTRST has an internal pull-up at reset). */
  Switch_Init();
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  /* USER CODE BEGIN 2 */

  /* Re-assert all-off in case MX_GPIO_Init touched PB4/PB5/PB10. */
  Switch_AllOff();

  Motor_GPIO_Init();                  /* motors stopped */
  Cycle_Counter_Init();
  Telemetry_Init(&huart2);

  uint8_t oled_ok = OLED_Init(&hi2c1);

  /* Probe sensors. Return values are CHECKED — a missing sensor must not
     look like 0 V / 0 A. */
  uint8_t ok_a = INA260_Init(&ina_a, &bus_i2c1, "A");
  uint8_t ok_b = INA260_Init(&ina_b, &bus_i2c3, "B");

  Telemetry_Print(oled_ok ? "# OLED OK" : "# OLED FAIL (isolated, will retry)");
  Telemetry_Print(ok_a ? "# INA A OK" : "# INA A MISSING");
  Telemetry_Print(ok_b ? "# INA B OK" : "# INA B MISSING");

  Display_Boot(ok_a, ok_b);           /* includes the column-offset border */

  if (!ok_a) fault_latched |= FAULT_SENSOR_A;
  if (!ok_b) fault_latched |= FAULT_SENSOR_B;
  if (fault_latched) Switch_AllOff();
  /* No halt: the loop keeps running so the screen and CSV keep reporting.
     With a fault latched nothing can turn a switch on (bench_debug
     refuses, and the loop re-asserts all-off every cycle). */

  HAL_Delay(1500);                    /* leave the boot screen readable */

  st.topo_confirmed = 0u;
  uint32_t next_ms  = HAL_GetTick();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

      /* ---- 0. Wait for this iteration's slot (no HAL_Delay) ---- */
      uint32_t now = HAL_GetTick();
      if ((int32_t)(now - next_ms) < 0) continue;

      next_ms += LOOP_PERIOD_MS;
      if ((int32_t)(now - next_ms) >= 0)
      {
          /* Fell a whole period behind: count it and re-sync instead of
             running a burst of back-to-back catch-up iterations. */
          st.overruns++;
          next_ms = now + LOOP_PERIOD_MS;
      }

      uint32_t t_start = DWT->CYCCNT;
      st.loop_count++;
      st.tick_ms = now;

      /* ---- 1. SENSE — always first ---- */
      Sense_String(&ina_a, &st.str[BMS_STR_A], now);
      Sense_String(&ina_b, &st.str[BMS_STR_B], now);

      /* ---- 2. SAFETY — Step 4 has exactly one rule ----
         Exhausted I2C retries on either sensor -> all off, latched.
         An unsensed pack is an unsafe pack. Step 5 adds OV/UV/OC here,
         still BEFORE anything else in the loop. */
      if (!st.str[BMS_STR_A].valid) fault_latched |= FAULT_SENSOR_A;
      if (!st.str[BMS_STR_B].valid) fault_latched |= FAULT_SENSOR_B;
      if (fault_latched) Switch_AllOff();   /* re-asserted every cycle */

      /* ---- 3. Manual bench commands (no control logic exists yet) ---- */
#if BMS_BENCH_DEBUG
      BenchDebug_Poll(&huart2, (fault_latched != 0u) ? 1u : 0u);
#endif

      /* ---- 4. Fill the rest of the snapshot ---- */
      st.sw_a    = (Switch_Get(SW_STRING_A) == SW_ON) ? 1u : 0u;
      st.sw_b    = (Switch_Get(SW_STRING_B) == SW_ON) ? 1u : 0u;
      st.sw_load = (Switch_Get(SW_LOAD)     == SW_ON) ? 1u : 0u;
      st.topo_label   = Topo_FromCommands(st.sw_a, st.sw_b);
      st.fault_flags  = fault_latched;
      st.fault_label  = ((fault_latched & FAULT_SENSOR_A) && (fault_latched & FAULT_SENSOR_B))
                            ? "SENSOR A+B LOST"
                        : (fault_latched & FAULT_SENSOR_A) ? "SENSOR A LOST"
                        : (fault_latched & FAULT_SENSOR_B) ? "SENSOR B LOST"
                        : "NONE";
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
          if (slot == SLOT_RENDER) Display_Render(&st);   /* RAM only */

          uint32_t t_oled = DWT->CYCCNT;
          OLED_Service();                                 /* <= 1 page  */
          uint32_t oled_us = cycles_to_us(DWT->CYCCNT - t_oled);
          if (oled_us > st.oled_us_max) st.oled_us_max = oled_us;
      }

      /* ---- 6. Motors (unchanged rover behaviour) ---- */
      if (motor_running) Motor_Forward(); else Motor_Stop();

      /* ---- 7. Heartbeat LED ---- */
      if ((st.loop_count % LED_EVERY) == 0u)
          HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);

      uint32_t work_us = cycles_to_us(DWT->CYCCNT - t_start);
      if (work_us > st.work_us_max) st.work_us_max = work_us;
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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

  /** Initializes the CPU, AHB and APB buses clocks
  */
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

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
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
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
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
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
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
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* Switch pins PB4/PB5/PB10 are owned by Switch_Init() (bms_switch.c),
     which already ran before this function. If you also add them in
     CubeMX, set their initial output level to LOW there. */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
 * @brief  PC13 user button toggles motor_running (unchanged rover code).
 *         300 ms software lockout as debounce.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13)
    {
        static uint32_t last_press = 0;
        uint32_t now = HAL_GetTick();

        if ((now - last_press) > 300)
        {
            motor_running = !motor_running;
            last_press = now;
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* SAFE STATE FIRST, before interrupts go away and before anything that
     depends on HAL — HAL may be the thing that failed.
     One direct BSRR write: upper 16 bits = reset. SW_PORT / SW_ALL_PINS
     are compile-time macros from bms_switch.h (no call into another
     module), so this stays in sync with the switch pin list instead of
     duplicating it as a literal. If Error_Handler fires before
     Switch_Init() has enabled the GPIOB clock, this write does nothing,
     but the pins are then still inputs and the external 100k pull-downs
     hold the NPNs off — except PB4, see Step 4 notes. */
  SW_PORT->BSRR = (uint32_t)SW_ALL_PINS << 16u;

  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
