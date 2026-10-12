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
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi2_tx;

/* USER CODE BEGIN PV */
volatile uint32_t dma_red_ms = 0;
volatile uint32_t dma_green_ms = 0;
volatile uint32_t dma_blue_ms = 0;

volatile uint32_t dma_loop_counter = 0;
volatile uint8_t dma_test_error = 0;
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
  {
      Error_Handler();
  }

  if (ILI9341_SetRotation(3) != ILI9341_OK)
  {
      Error_Handler();
  }

  if (ILI9341_Width() != ILI9341_NATIVE_HEIGHT ||
      ILI9341_Height() != ILI9341_NATIVE_WIDTH)
  {
      Error_Handler();
  }

//  uint32_t start_tick;
//  uint32_t end_tick;
//
//  volatile uint32_t red_time_ms;
//  volatile uint32_t green_time_ms;
//  volatile uint32_t blue_time_ms;

  /* Red */
//  start_tick = HAL_GetTick();
//  ILI9341_FillScreen(ILI9341_RED);
//  end_tick = HAL_GetTick();
//  red_time_ms = end_tick - start_tick;

  /* Green */
//  start_tick = HAL_GetTick();
//  ILI9341_FillScreen(ILI9341_GREEN);
//  end_tick = HAL_GetTick();
//  green_time_ms = end_tick - start_tick;

  /* Blue */
//  start_tick = HAL_GetTick();
//  ILI9341_FillScreen(ILI9341_BLUE);
//  end_tick = HAL_GetTick();
//  blue_time_ms = end_tick - start_tick;
//  Dashboard_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t start_tick;
  /* ---------- RED ---------- */

  start_tick = HAL_GetTick();

  if (ILI9341_FillScreenDMA(ILI9341_RED) != ILI9341_OK)
  {
      Error_Handler();
  }

  while (ILI9341_DMA_IsBusy())
  {
      ILI9341_DMA_Service();

      /* Simulated CPU work */
      dma_loop_counter++;
  }

  if (ILI9341_DMA_HasError())
  {
      dma_test_error = 1U;
      Error_Handler();
  }

  dma_red_ms = HAL_GetTick() - start_tick;

  HAL_Delay(500);


  /* ---------- GREEN ---------- */

  start_tick = HAL_GetTick();

  if (ILI9341_FillScreenDMA(ILI9341_GREEN) != ILI9341_OK)
  {
      Error_Handler();
  }

  while (ILI9341_DMA_IsBusy())
  {
      ILI9341_DMA_Service();
      dma_loop_counter++;
  }

  if (ILI9341_DMA_HasError())
  {
      dma_test_error = 2U;
      Error_Handler();
  }

  dma_green_ms = HAL_GetTick() - start_tick;

  HAL_Delay(500);


  /* ---------- BLUE ---------- */

  start_tick = HAL_GetTick();

  if (ILI9341_FillScreenDMA(ILI9341_BLUE) != ILI9341_OK)
  {
      Error_Handler();
  }

  while (ILI9341_DMA_IsBusy())
  {
      ILI9341_DMA_Service();
      dma_loop_counter++;
  }

  if (ILI9341_DMA_HasError())
  {
      dma_test_error = 3U;
      Error_Handler();
  }

  dma_blue_ms = HAL_GetTick() - start_tick;

//  DashboardStats stats = {
//        .rx_kbps = 1200,
//        .tx_kbps = 500,
//        .cpu_usage = 20,
//        .mem_usage = 40
//    };
  while (1)
  {
//	Dashboard_Update(&stats);
//
//	/* 模擬網路流量增加 */
//	stats.rx_kbps += 100;
//	stats.tx_kbps += 50;
//
//	if (stats.rx_kbps > 5000)
//		stats.rx_kbps = 1000;
//
//	if (stats.tx_kbps > 2000)
//		stats.tx_kbps = 300;
//
//	stats.cpu_usage =
//		(stats.cpu_usage + 5) % 100;
//
//	stats.mem_usage =
//		(stats.mem_usage + 3) % 100;
//
//	HAL_Delay(1000);
	  HAL_Delay(1000);
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
  RCC_OscInitStruct.PLL.PLLN = 50;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
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
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PD6 PD7 */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
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
