/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
#include "ili9341.h"
#include "dashboard.h"
#include "dma_fault_test.h"
#include "pixel_dma_test.h"
#include "dashboard_perf.h"
#include "touch_calibration.h"
#include "touch_input.h"
#include "touch_raw.h"
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "touch_perf.h"
#include "router_data_provider.h"
#include "router_transport_uart.h"
#include "touch_cal_store.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ENABLE_DMA_FAULT_TEST 0U   /* Set to 1 for one controlled boot test. */
#define ENABLE_PIXEL_DMA_TEST 0U   /* Set to 1 for visible color/clipping test. */
#define ENABLE_TOUCH_RAW_TEST 1U    /* PC6..PC10 touch wiring verified on board. */
#define ENABLE_TOUCH_CALIBRATION 1U /* Set to 1 for five-point boot calibration. */

#if ENABLE_TOUCH_CALIBRATION && !ENABLE_TOUCH_RAW_TEST
#error "Touch calibration requires ENABLE_TOUCH_RAW_TEST=1"
#endif
#define ENABLE_CAL_STORE 1U /* Keep calibration in reserved Flash sector 11. */
#define ENABLE_ROUTER_UART 0U /* Set to 1 after wiring 3.3V TTL TX to PA3. */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi2_tx;

/* USER CODE BEGIN PV */
/* Read these in CubeIDE Expressions. */
volatile uint32_t dma_safety_ms = 0U;
volatile uint32_t dma_safety_fail = 0U;
volatile uint32_t dma_fault_test_result = 0xFFFFFFFFU; /* Not run */
volatile uint32_t pixel_dma_test_result = 0xFFFFFFFFU; /* Not run */
/* B1 demo diagnostics: values visible in CubeIDE Expressions. */
volatile uint32_t touch_last_target = 0U;
volatile uint32_t touch_last_source = 0U;
volatile uint32_t touch_last_x = 0U, touch_last_y = 0U;
/* Owned by Data Provider; never retain pointers to stack telemetry samples. */
static DashboardStats router_stats;
volatile uint32_t router_uart_init_failed = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */
  if (ILI9341_Init() != ILI9341_OK)
    Error_Handler();
  /* Repo's working landscape orientation is 3, not 1. */
  if (ILI9341_SetRotation(3) != ILI9341_OK)
    Error_Handler();
  if (ILI9341_Width() != ILI9341_NATIVE_HEIGHT ||
      ILI9341_Height() != ILI9341_NATIVE_WIDTH)
    Error_Handler();

  /* Safety regression: running DMA must reject blocking drawing. */
  uint32_t safety_start = HAL_GetTick();
  if (ILI9341_FillScreenDMA(ILI9341_BLUE) != ILI9341_OK)
  {
    dma_safety_fail = 1U;
    Error_Handler();
  }
  if (ILI9341_FillRect(0U, 0U, 10U, 10U, ILI9341_WHITE) != ILI9341_BUSY)
  {
    dma_safety_fail = 2U;
    Error_Handler();
  }
  if (ILI9341_DMA_Wait(1500U) != ILI9341_OK)
  {
    dma_safety_fail = 3U;
    Error_Handler();
  }
  dma_safety_ms = HAL_GetTick() - safety_start;

#if ENABLE_DMA_FAULT_TEST
  /* Run before Dashboard_Init(): tests deliberately interrupt pixel DMA. */
  dma_fault_test_result = (uint32_t)DmaFaultTest_Run(&hspi2);
  if (dma_fault_test_result != (uint32_t)DMA_TEST_PASS)
  {
    Error_Handler(); /* Inspect dma_fault_test_result in Debugger. */
  }
#endif

#if ENABLE_PIXEL_DMA_TEST
  /* Visible multi-color test before Dashboard_Init() restores the screen. */
  pixel_dma_test_result = (uint32_t)PixelDmaTest_Run();
  if (pixel_dma_test_result != (uint32_t)PIXEL_DMA_TEST_PASS)
  {
    Error_Handler();
  }
#endif

  /* Optional five-target calibration runs before Dashboard uses the LCD. */
#if ENABLE_TOUCH_CALIBRATION && ENABLE_TOUCH_RAW_TEST
  TouchRaw_Init();
#if ENABLE_CAL_STORE
  TouchCalStore_Init();
  uint8_t force_recal = TouchCalStore_ForceRecalibration();
  if (!force_recal && TouchCalStore_Load(&touch_calibration))
  {
      /* Validated CRC and exact screen geometry on load. */
      touch_cal_debug = (TouchCalDebug){0};
      touch_cal_debug.result = TOUCH_CAL_PASS;
      touch_cal_debug.calibrated = 1U;
      touch_cal_debug.max_error_px = touch_cal_store_stats.loaded_fit_error_px;
  }
  else
  {
      TouchCal_RunBoot();
      if (touch_cal_debug.result == TOUCH_CAL_PASS && touch_calibration.valid)
          (void)TouchCalStore_Save(&touch_calibration, touch_cal_debug.max_error_px);
  }
#else
  TouchCal_RunBoot(); /* M2.2 RAM-only fallback */
#endif
#endif
  /* Resume the original known-working blocking Dashboard path. */
  Dashboard_Init();
  /* B1/PA0 is a safe virtual-touch input source (no TFT SPI sharing). */
  TouchInput_Init(HAL_GetTick());
#if ENABLE_TOUCH_RAW_TEST && !ENABLE_TOUCH_CALIBRATION
  TouchRaw_Init();
#endif
  DashboardPerf_Init(HAL_GetTick());
  TouchPerf_Init(HAL_GetTick());
  RouterDataProvider_Init(HAL_GetTick(), &router_stats);
#if ENABLE_ROUTER_UART
  if (RouterUart_Init() == 0U)
    router_uart_init_failed = 1U; /* Leave DEMO running; inspect clocks. */
#endif
  Dashboard_Update(&router_stats);
  DashboardPerf_OnStatsQueued(HAL_GetTick());
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t service_begin = DashboardPerf_CycleNow();
    Dashboard_Service();
    DashboardPerf_RecordService(service_begin);

    uint32_t now = HAL_GetTick();
    uint32_t touch_input_begin = DashboardPerf_CycleNow();
    TouchInput_Service(now);
#if ENABLE_TOUCH_RAW_TEST
    TouchRaw_Service(now);
#if ENABLE_TOUCH_CALIBRATION
    TouchCal_Service(); /* Coordinate diagnostics only; no UI gesture injection. */
#endif
#endif
    TouchPerf_OnInputService(touch_input_begin, DashboardPerf_CycleNow());
    TouchInputEvent input_event;
    while (TouchInput_Pop(&input_event))
    {
      /* Synthetic B1 taps exercise the same classification entry point
       * a later, calibrated hardware touch driver will use. */
      TouchInputTarget target = TouchInput_Classify(&input_event);
      touch_last_target = (uint32_t)target;
      touch_last_source = (uint32_t)input_event.source;
      touch_last_x = input_event.x;
      touch_last_y = input_event.y;
      if (target != TOUCH_INPUT_TARGET_NONE &&
          RouterDataProvider_OnTouch(target, &router_stats) != 0U)
      {
        Dashboard_Update(&router_stats);
        DashboardPerf_OnStatsQueued(now);
        TouchPerf_OnAction(&input_event, target, now);
      }
    }
    uint8_t dma_fault = ILI9341_DMA_HasError();
    DashboardPerf_Poll(now, Dashboard_IsBusy(), dma_fault);
    TouchPerf_Poll(now, Dashboard_IsBusy(), dma_fault);
    if (dma_fault)
    {
      /* Do not silently clear: preserve evidence for debugger. */
      dma_safety_fail = 4U;
      Error_Handler();
    }

    /* Consume a bounded amount of serial telemetry in foreground only.
     * Never process frames, publish stats or redraw from USART2 IRQ. */
#if ENABLE_ROUTER_UART
    if (router_uart_init_failed == 0U)
    {
      RouterRxSample telemetry;
      if (RouterUart_Poll(now, &telemetry) != 0U)
      {
        DashboardStats received = {
            telemetry.telemetry.rx_kbps, telemetry.telemetry.tx_kbps,
            telemetry.telemetry.cpu_usage, telemetry.telemetry.mem_usage};
        (void)RouterDataProvider_PublishExternal(&received,
                                                 telemetry.received_ms);
      }
    }
#endif
    /* Data acquisition must not wait for the display DMA queue. */
    if (RouterDataProvider_Poll(now, &router_stats) != 0U)
    {
      Dashboard_Update(&router_stats);
      DashboardPerf_OnStatsQueued(now);
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 100;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief SPI2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */
}

/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
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
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PD6 PD7 */
  GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_SPI_TxCpltCallback(
    SPI_HandleTypeDef *hspi)
{
  ILI9341_DMA_TxCompleteCallback(hspi);
}

void HAL_SPI_ErrorCallback(
    SPI_HandleTypeDef *hspi)
{
  ILI9341_DMA_ErrorCallback(hspi);
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
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
