#include "stm32f4xx_hal.h"

#define BOOTLOADER_ADDR 0x1FFF0000 // Bootloader start address
#define BOOTLOADER_VECTOR_TABLE	((struct bootloader_vectable__t *)BOOTLOADER_ADDR)

extern uint32_t _estack;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void Error_Handler(void);
static void JumpToBootloader(void);
static void Force_Reset_Handler(void);

struct bootloader_vectable__t {
    uint32_t stack_pointer;
    void (*reset_handler)(void);
};

__attribute__((section(".force_reset_vtable")))
const void* force_reset_vtable[] =
{
    (void*)&_estack,          // MSP
    Force_Reset_Handler       // Reset_Handler
};

/**
  * Initializes the Global MSP.
  */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void JumpToBootloader(void)
{
    // Deinit HAL and Clocks
    HAL_DeInit();
    HAL_RCC_DeInit();

    // Disable all interrupts
    __disable_irq();

    // Disable Systick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    // Disable interrupts and clear pending ones
    for (size_t i = 0; i < sizeof(NVIC->ICER)/sizeof(NVIC->ICER[0]); i++) {
        NVIC->ICER[i]=0xFFFFFFFF;
        NVIC->ICPR[i]=0xFFFFFFFF;
    }

    // Map Bootloader (system flash) memory to 0x00000000. This is STM32 family dependant.
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

    // Set embedded bootloader vector table base offset
    WRITE_REG(SCB->VTOR, SCB_VTOR_TBLOFF_Msk & 0x00000000);

    // Switch to Main Stack Pointer (in case it was using the Process Stack Pointer)
    __set_CONTROL(0);

    // Instruction synchronization barrier
    __ISB();

    // Set Main Stack Pointer to the Bootloader defined value.
    __set_MSP(BOOTLOADER_VECTOR_TABLE->stack_pointer);

    __DSB(); // Data synchronization barrier
    __ISB(); // Instruction synchronization barrier

    // Jump to Bootloader Reset Handler
    BOOTLOADER_VECTOR_TABLE->reset_handler();

    // The next instructions will not be reached
    while (1){}
}

__attribute__((noreturn))
void Force_Reset_Handler(void)
{
    __disable_irq();
    __DSB();
    __ISB();

    NVIC_SystemReset();

    while (1);
}

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Configure the system clock */
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    JumpToBootloader();
    /* Infinite loop */
    while (1)
    {
    }
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
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /*Configure GPIO pin : PC13 */
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
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
}
#endif /* USE_FULL_ASSERT */
