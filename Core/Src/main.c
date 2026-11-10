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
#include <stdio.h>
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "kernel.h"
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
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}



void print_continuously(){
	while(1){
		printf("Hello, PC!\r\n");
	}

}

/* ==========================================================================
 * Lab 5 test case
 *
 *   thread1 - COOPERATIVE. Prints the shared counter several times, then calls
 *             osYield(). Its timeslice is 0, so the OS never pre-empts it; it
 *             only ever gives up the CPU voluntarily.
 *   thread2 - PRE-EMPTIVE. Never yields. It is removed from the CPU only when
 *             its 200 ms timeslice expires and SysTick pends PendSV.
 *
 * thread2 takes a pointer to a struct holding two values and uses both of them
 * to update a global that thread1 prints. Because thread1 cannot see the
 * counter change while it is running, each of its turns prints the SAME value
 * several times; the value jumps only after thread2 has had a turn. That jump
 * is the proof that both threads are sharing the CPU.
 * ========================================================================== */

/* The struct thread2 receives a pointer to. Two members, both used. */
typedef struct {
	uint32_t increment;    /* added to the counter each iteration */
	uint32_t multiplier;   /* the counter is then multiplied by this */
} work_params;

/* Shared between the two threads. volatile because thread2 changes it behind
 * thread1's back — without this the compiler is free to cache it in a register
 * and thread1 would print a stale value forever. */
volatile uint32_t sharedCounter = 0;

/* COOPERATIVE thread. Its argument is how many lines to print per turn. */
void thread1(void *args)
{
	uint32_t lines_per_turn = *(uint32_t*)args;

	while(1)
	{
		for (uint32_t i = 0; i < lines_per_turn; i++)
		{
			printf("counter = %lu\r\n", (unsigned long)sharedCounter);
		}

		/* Hand the CPU over on purpose. Nothing forces us to do this. */
		osYield();
	}
}

/* PRE-EMPTIVE thread. Note there is no osYield() anywhere in here: the only
 * way this thread ever stops running is the OS taking the CPU away. */
void thread2(void *args)
{
	/* Copy the struct out of wherever the caller put it, exactly as in part 1.
	 * We do not know if that memory stays valid, so we take our own copy. */
	work_params params = *(work_params*)args;

	while(1)
	{
		sharedCounter = (sharedCounter + params.increment) * params.multiplier;
	}
}

void jumpAssembly(void* fcn)
{
__asm("MOV PC, R0");
}

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
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */


  //char m = 'm';
  //HAL_UART_Transmit(&huart2,&m,1,HAL_MAX_DELAY);


  //printf("Hello, world!\r\n");


  //print_continuously();
  //jumpAssembly((void*)print_continuously);

  //print_success();
  //print_fail();

  /* --- Lab 2: manual stack allocation (kept for reference) ----------------
  uint32_t* MSP_INIT_VAL = *(uint32_t**)0x0;
  //printf("MSP Init is: %p\r\n",MSP_INIT_VAL); //note the %p to print a pointer. It will be in hex
  uint32_t PSP_val = (uint32_t)MSP_INIT_VAL - 0x400;
  //__set_PSP(PSP_val);
  //__set_CONTROL(0b10);
  stackptr = (uint32_t*)PSP_val;
  ------------------------------------------------------------------------- */

  /* --- Lab 3 (earlier): allocate + build the stack frame by hand ----------
  kernel_init();
  stackptr = allocate_stack();
  *(--stackptr) = 1<<24; //A magic number, this is xPSR
  *(--stackptr) = (uint32_t)print_continuously; //the function name
  for (int i = 0; i < 14 ; i++) {
	  *(--stackptr) = 0xA; //An arbitrary number
  }
  ------------------------------------------------------------------------- */

  /* The full kernel start-up sequence:
   *   1. initialize the kernel
   *   2. create our threads
   *   3. start the kernel (which runs the threads)
   *
   * The arguments must outlive main's call to osKernelStart, so they are static
   * rather than plain locals. main's locals live on MSP, which the interrupt
   * handlers keep reusing once the kernel switches the threads over to PSP. */
  static uint32_t thread1_lines = 5;                  /* lines printed per turn */
  /* The multiplier MUST be odd. An even multiplier shifts bits left out of the
   * 32-bit word on every iteration, so the counter collapses to a fixed value
   * (0xFFFFFFFA for increment=3, multiplier=2) within about 32 iterations and
   * then never changes again - which would make the printed output useless as
   * proof. An odd multiplier is invertible mod 2^32, so the sequence never
   * settles. */
  static work_params thread2_params = {
      .increment  = 7,
      .multiplier = 3
  };

  osKernelInitialize();

  /* Deadline 0 = purely cooperative. The OS clock skips this thread entirely,
   * so it runs until it calls osYield() itself. */
  osCreateThreadWithDeadline(thread1, &thread1_lines, 0);

  /* Deadline 200 ms. This thread never yields, so this is the only thing that
   * ever takes the CPU away from it. */
  osCreateThreadWithDeadline(thread2, &thread2_params, 200);

  osKernelStart();



  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
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

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

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

#ifdef  USE_FULL_ASSERT
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
