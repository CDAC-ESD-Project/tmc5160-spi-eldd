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
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include<stdio.h>
#include<string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
//SPI
#define SPI_CS_ENABLE()		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define SPI_CS_DISABLE()	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)

#define DRV_ENABLE()	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_RESET)
#define DRV_DISABLE()	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET)

//#define DRV_EN()	HAL_GPIO_Write_Pin(GPIOE, GPIO_PIN_4, GPIO_PIN_SET);

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
void tmc5160_write_reg(uint8_t addr, uint32_t data);
uint32_t tmc5160_read_reg(uint8_t addr);
void print_uart(char *str);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void display(uint8_t array[])
{ int i;
	for(i = 0; i < 5; i++)
	            {
	          	  char str[10];
	          	  sprintf(str, "0x%02X ", array[i]);
	          	  HAL_UART_Transmit(&huart2, (uint8_t*)str, strlen(str), HAL_MAX_DELAY);
	            }

	            HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
}

void tmc5160_write_reg(uint8_t addr, uint32_t data)
{
    uint8_t tx[5];
    uint8_t rx[5];

    tx[0] = addr | 0x80;          // write bit = bit7 set
    tx[1] = (data >> 24) & 0xFF;  // MSB first
    tx[2] = (data >> 16) & 0xFF;
    tx[3] = (data >> 8)  & 0xFF;
    tx[4] = data & 0xFF;

    SPI_CS_ENABLE();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 5, HAL_MAX_DELAY);
    SPI_CS_DISABLE();
}


uint32_t tmc5160_read_reg(uint8_t addr)
{
    uint8_t tx[5] = {addr, 0x00, 0x00, 0x00, 0x00};
    uint8_t rx[5];

    // first transaction - throwaway (returns previous request's data)
    SPI_CS_ENABLE();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 5, HAL_MAX_DELAY);
    SPI_CS_DISABLE();

    // second transaction - real data for THIS address
    SPI_CS_ENABLE();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 5, HAL_MAX_DELAY);
    SPI_CS_DISABLE();

    uint32_t val = ((uint32_t)rx[1] << 24) | ((uint32_t)rx[2] << 16) |
                   ((uint32_t)rx[3] << 8)  | rx[4];
    return val;
}

void print_uart(char *str)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)str, strlen(str), HAL_MAX_DELAY);
}

void tmc5160_poll_status(void)
{
    char buf[150];
    uint32_t drvstatus = tmc5160_read_reg(0x6F);
    uint32_t gstat     = tmc5160_read_reg(0x01);
    int32_t  xactual   = (int32_t)tmc5160_read_reg(0x21);
    uint32_t chopconf  = tmc5160_read_reg(0x6C);

    sprintf(buf, "XACTUAL=%ld GSTAT=0x%08lX DRVSTATUS=0x%08lX CHOPCONF=0x%08lX\r\n",
            (long)xactual, gstat, drvstatus, chopconf);
    print_uart(buf);

    // Decode DRV_STATUS fault bits
    if (drvstatus & (1UL << 27)) print_uart("FAULT: OT (overtemp shutdown)\r\n");
    if (drvstatus & (1UL << 26)) print_uart("WARN: OTPW (overtemp prewarn)\r\n");
    if (drvstatus & (1UL << 25)) print_uart("FAULT: S2GB (short to GND phase B)\r\n");
    if (drvstatus & (1UL << 24)) print_uart("FAULT: S2GA (short to GND phase A)\r\n");
    if (drvstatus & (1UL << 29)) print_uart("FAULT: S2VSB (short to supply phase B)\r\n");
    if (drvstatus & (1UL << 28)) print_uart("FAULT: S2VSA (short to supply phase A)\r\n");
    if (drvstatus & (1UL << 23)) print_uart("WARN: OLB (open load phase B)\r\n");
    if (drvstatus & (1UL << 22)) print_uart("WARN: OLA (open load phase A)\r\n");
    if (drvstatus & (1UL << 31)) print_uart("INFO: standstill = 1 (motor NOT moving)\r\n");
    else                          print_uart("INFO: standstill = 0 (motor IS moving)\r\n");

    if (gstat & 0x1) print_uart("GSTAT: reset flag set\r\n");
    if (gstat & 0x2) print_uart("GSTAT: driver error\r\n");
    if (gstat & 0x4) print_uart("GSTAT: charge pump undervoltage\r\n");

    // Decode CHOPCONF TOFF — this is the #1 reason for "no response"
    uint8_t toff = chopconf & 0x0F;
    sprintf(buf, "CHOPCONF TOFF=%d (must be non-zero for driver to be enabled)\r\n", toff);
    print_uart(buf);
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

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  SPI_CS_DISABLE();
  DRV_ENABLE();
  HAL_Delay(1000);

  //SPI Transmit Array
  uint8_t tx_data[] = {0x01,0x00,0x00,0x00,0x00};

  //SPI Receive Array
  uint8_t rx_spi_data[5] = {0x00};

  //SPI Message Transmit
  SPI_CS_ENABLE();
  HAL_SPI_TransmitReceive(&hspi1, tx_data, rx_spi_data, 5, HAL_MAX_DELAY);
  SPI_CS_DISABLE();
  display(tx_data);

    //SPI Message Receive
  display(rx_spi_data);


  //SPI Message Transmit
  SPI_CS_ENABLE();
  HAL_SPI_TransmitReceive(&hspi1, tx_data, rx_spi_data, 5, HAL_MAX_DELAY);
  SPI_CS_DISABLE();
  display(tx_data);
  //SPI Message Receive
  display(rx_spi_data);

      uint8_t tx_drvstatus[5] = {0x6F, 0x00, 0x00, 0x00, 0x00};
      SPI_CS_ENABLE();
      HAL_SPI_TransmitReceive(&hspi1, tx_drvstatus, rx_spi_data, 5, HAL_MAX_DELAY);
      SPI_CS_DISABLE();
      display(tx_drvstatus);

            HAL_Delay(1000);
      SPI_CS_ENABLE();
      HAL_SPI_TransmitReceive(&hspi1, tx_drvstatus, rx_spi_data, 5, HAL_MAX_DELAY); // 2nd call = real data
      SPI_CS_DISABLE();
      display(rx_spi_data);


  uint8_t tx_clear_gstat[5] = {0x81, 0x00, 0x00, 0x00, 0x07}; // 0x81 = write bit + addr 0x01, data=0x07 clears bits 0-2
  SPI_CS_ENABLE();
  HAL_SPI_TransmitReceive(&hspi1, tx_clear_gstat, rx_spi_data, 5, HAL_MAX_DELAY);
  SPI_CS_DISABLE();

  SPI_CS_ENABLE();
  HAL_SPI_TransmitReceive(&hspi1, tx_clear_gstat, rx_spi_data, 5, HAL_MAX_DELAY);
  SPI_CS_DISABLE();
  display(rx_spi_data);

  SPI_CS_ENABLE();
    HAL_SPI_TransmitReceive(&hspi1, tx_clear_gstat, rx_spi_data, 5, HAL_MAX_DELAY);
    SPI_CS_DISABLE();
    display(rx_spi_data);


    tmc5160_write_reg(0x00, 0x00000000);
    // TOFF=3, HSTRT=4, HEND=1, TBL=2, MRES=0 (256 microsteps)
    tmc5160_write_reg(0x0B, 0x000000C8); // GLOBAL_SCALER = 200 (out of 256)
    tmc5160_write_reg(0x6C, 0x000100C3);
    tmc5160_write_reg(0x10, 0x000A1F1A); // IHOLD=16, IRUN=20, IHOLDDELAY=10 (more current)
    tmc5160_write_reg(0x11, 0x0000000A);
    tmc5160_write_reg(0x20, 0x00000000); // 0 = positioning mode (uses ramp registers + XTARGET)
    ///
    tmc5160_write_reg(0x23, 0x00000000); // VSTART = 0
    tmc5160_write_reg(0x24, 0x00000064); // A1   = 100
    tmc5160_write_reg(0x25, 0x00000C80); // V1   = 3200
    tmc5160_write_reg(0x26, 0x00000032); // AMAX = 50
    tmc5160_write_reg(0x27, 0x00001388); // VMAX = 5000   <- much slower
    tmc5160_write_reg(0x28, 0x00000064); // DMAX = 100
    tmc5160_write_reg(0x2A, 0x000000C8); // D1   = 200
    tmc5160_write_reg(0x2B, 0x0000000A); // VSTOP = 10
    //
    tmc5160_write_reg(0x21, 0x00000000);  // XACTUAL = 0 (define current position as zero)
    tmc5160_write_reg(0x2D, 0x0000C800);  // XTARGET = 51200 (200 full steps * 256 microsteps)

    tmc5160_write_reg(0x21, 0x00000000);  // XACTUAL = 0
    HAL_Delay(5);
    int32_t check = (int32_t)tmc5160_read_reg(0x21);
    char buf[60];
    sprintf(buf, "After XACTUAL write, readback = %ld\r\n", (long)check);
    print_uart(buf);

    tmc5160_write_reg(0x2D, 0x0000C800);  // XTARGET = 51200

    for (int i = 0; i < 20; i++) {
        int32_t xa = (int32_t)tmc5160_read_reg(0x21);
        uint32_t rm = tmc5160_read_reg(0x20);
        sprintf(buf, "t=%dms XACTUAL=%ld RAMPMODE=%lu\r\n", i*100, (long)xa, rm);
        print_uart(buf);
        HAL_Delay(100);
    }




  //UART Data Sent
  HAL_UART_Transmit(&huart2, (uint8_t*)"----------------------------\r\n", 30, HAL_MAX_DELAY);

  DRV_DISABLE();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  tmc5160_poll_status();
	  HAL_Delay(200);
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
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

  /*Configure GPIO pin : PE5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : PA4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
