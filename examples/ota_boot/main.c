#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdbool.h>
#include "mem_layout.h"
#include "ota_metadata.h"

/* ---- Types ---- */

typedef struct {
    uint32_t stack_pointer;
    void (*reset_handler)(void);
} app_vector_t;

/* ---- Globals ---- */

UART_HandleTypeDef huart1;

/* ---- Forward declarations ---- */

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void Error_Handler(void);

/* ---- HAL MSP callbacks ---- */

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (huart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
    }
}

/* ---- UART helpers ---- */

/**
 * @brief Transmits a null-terminated string over UART.
 *
 * @why Provides basic text output for bootloader status messages.
 *
 * @param str Null-terminated string to transmit.
 */
static void uart_print(const char *str)
{
    HAL_UART_Transmit(&huart1, (const uint8_t *)str, strlen(str), 100);
}

/**
 * @brief Transmits a 32-bit value in hexadecimal format over UART.
 *
 * @why Enables debugging output of addresses, sizes, and CRC values.
 *
 * @param val Value to transmit in hex format.
 */
static void uart_print_hex(uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[] = "0x00000000";
    for (int i = 7; i >= 0; i--)
        buf[2 + (7 - i)] = hex[(val >> (i * 4)) & 0xF];
    uart_print(buf);
}

/* ---- CRC32 (software, standard Ethernet polynomial) ---- */

/**
 * @brief Calculates CRC32 using the standard Ethernet (IEEE 802.3) polynomial.
 *
 * Processes data byte-by-byte with table-free bit-by-bit computation.
 * The initial value is 0xFFFFFFFF and the result is bitwise-inverted before
 * returning, matching the standard CRC32 convention.
 *
 * @why Required to verify firmware integrity after flash write and to validate
 *      OTA packet checksums before accepting any chunk.
 *
 * @param data Pointer to the byte buffer to checksum.
 * @param len  Number of bytes to process.
 * @return    32-bit CRC value.
 */
static uint32_t CRC32_Calculate(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

/* ---- Metadata operations ---- */

/**
 * @brief Reads OTA metadata from flash into the provided structure.
 *
 * @why Retrieves persistent OTA state information stored in flash sector 2.
 *
 * @param meta Pointer to metadata structure to fill.
 */
static void Metadata_Read(OTA_Metadata_t *meta)
{
    memcpy(meta, (const void *)METADATA_START_ADDR, sizeof(OTA_Metadata_t));
}

/**
 * @brief Validates OTA metadata by checking magic number and CRC.
 *
 * @why Ensures metadata integrity before trusting any stored values.
 *
 * @param meta Pointer to metadata structure to validate.
 * @return    true if metadata is valid, false otherwise.
 */
static bool Metadata_IsValid(const OTA_Metadata_t *meta)
{
    if (meta->magic != METADATA_MAGIC)
        return false;
    uint32_t crc = CRC32_Calculate((const uint8_t *)meta,
                                   sizeof(OTA_Metadata_t) - sizeof(uint32_t));
    return (crc == meta->metadata_crc32);
}

/**
 * @brief Writes OTA metadata to flash after calculating its CRC.
 *
 * Erases sector 2, programs the metadata, and verifies the write.
 *
 * @why Persists OTA state changes to survive power cycles and reboots.
 *
 * @param meta Pointer to metadata structure to write.
 * @return    true if write succeeded, false on error.
 */
static bool Metadata_Write(OTA_Metadata_t *meta)
{
    // Calculate CRC for the entire metadata except the CRC field itself
    meta->metadata_crc32 = CRC32_Calculate((const uint8_t *)meta,
                                           sizeof(OTA_Metadata_t) - sizeof(uint32_t));
    HAL_FLASH_Unlock();

    // Erase the metadata sector (sector 2)
    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .Sector       = FLASH_SECTOR_2,
        .NbSectors    = 1,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3
    };
    uint32_t error;
    if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    // Program the metadata word by word
    uint32_t *src = (uint32_t *)meta;
    uint32_t addr = METADATA_START_ADDR;
    for (uint32_t i = 0; i < sizeof(OTA_Metadata_t) / sizeof(uint32_t); i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        addr += 4;
    }

    HAL_FLASH_Lock();
    return true;
}

/* ---- App verification ---- */

/**
 * @brief Performs basic validation of an application's vector table.
 *
 * Checks that the stack pointer is within SRAM bounds and the reset handler
 * is within flash bounds with the Thumb bit set.
 *
 * @why Prevents jumping to invalid or corrupted application code.
 *
 * @param addr Base address of the application.
 * @return    true if application vectors are valid, false otherwise.
 */
static bool App_Verify(uint32_t addr)
{
    // Read and validate stack pointer from vector table
    uint32_t sp = *(volatile uint32_t *)addr;
    if (sp < 0x20000000 || sp > 0x20020000) {
        uart_print("  Invalid SP: ");
        uart_print_hex(sp);
        uart_print("\r\n");
        return false;
    }

    // Read and validate reset handler from vector table
    uint32_t reset = *(volatile uint32_t *)(addr + 4);
    if ((reset & 0x1U) == 0U || reset < 0x08000000 || reset > 0x08080000) {
        uart_print("  Invalid reset handler: ");
        uart_print_hex(reset);
        uart_print("\r\n");
        return false;
    }

    return true;
}

/**
 * @brief Performs comprehensive validation of an application including CRC.
 *
 * First validates the vector table, then checks size bounds, and finally
 * verifies the CRC32 of the entire application image.
 *
 * @why Ensures application integrity before allowing it to run.
 *
 * @param addr          Base address of the application.
 * @param size          Size of the application in bytes.
 * @param expected_crc  Expected CRC32 value.
 * @param max_size      Maximum allowed size for this application slot.
 * @return              true if application is valid, false otherwise.
 */
static bool App_VerifyCRC(uint32_t addr, uint32_t size, uint32_t expected_crc,
                          uint32_t max_size)
{
    // First verify basic vector table integrity
    if (!App_Verify(addr))
        return false;

    // Validate application size
    if (size == 0 || size > max_size) {
        uart_print("  Invalid size: ");
        uart_print_hex(size);
        uart_print("\r\n");
        return false;
    }

    // Check for valid CRC value (not 0 or all 1s)
    if (expected_crc == 0 || expected_crc == 0xFFFFFFFF) {
        uart_print("  No valid CRC stored\r\n");
        return false;
    }

    // Calculate and verify CRC32 of the application image
    uart_print("  Checking CRC32...\r\n");
    uint32_t crc = CRC32_Calculate((const uint8_t *)addr, size);
    if (crc != expected_crc) {
        uart_print("  CRC mismatch: expected ");
        uart_print_hex(expected_crc);
        uart_print(", got ");
        uart_print_hex(crc);
        uart_print("\r\n");
        return false;
    }
    return true;
}

/* ---- Boot decision ---- */

/**
 * @brief Determines which application to boot based on metadata state.
 *
 * Implements the OTA boot logic: handles interrupted OTAs, force factory boot,
 * validates App B status, manages boot attempt counters, and falls back to factory.
 *
 * @why Centralizes the complex boot decision logic for maintainability.
 *
 * @param meta Pointer to current metadata state.
 * @return    APP_FACTORY or APP_B_OTA depending on boot decision.
 */
static App_Type_t Boot_Decide(OTA_Metadata_t *meta)
{
    // Handle interrupted OTA by clearing progress and invalidating App B
    if (meta->ota_in_progress) {
        uart_print("OTA interrupted, clearing\r\n");
        meta->ota_in_progress = 0;
        meta->app_b_status = APP_STATUS_INVALID;
        if (!Metadata_Write(meta)) {
            uart_print("Metadata write failed\r\n");
        }
        return APP_FACTORY;
    }

    // Check for forced factory boot flag
    if (meta->force_factory_boot) {
        uart_print("Force factory boot flag set\r\n");
        return APP_FACTORY;
    }

    // Boot validated App B if available
    if (meta->app_b_status == APP_STATUS_VALID) {
        uart_print("App B validated, booting OTA app\r\n");
        return APP_B_OTA;
    }

    // Handle pending App B with attempt counting
    if (meta->app_b_status == APP_STATUS_PENDING) {
        // Give up after max attempts and force factory boot
        if (meta->app_b_boot_count >= MAX_BOOT_ATTEMPTS) {
            uart_print("App B exceeded max attempts, marking invalid\r\n");
            meta->app_b_status = APP_STATUS_INVALID;
            meta->force_factory_boot = 1;
            if (!Metadata_Write(meta)) {
                uart_print("Metadata write failed\r\n");
            }
            return APP_FACTORY;
        }

        // Increment attempt counter and prepare for App B boot
        uart_print("Trying App B (attempt ");
        char num = '1' + meta->app_b_boot_count;
        HAL_UART_Transmit(&huart1, (uint8_t *)&num, 1, 10);
        uart_print("/");
        char max = '0' + MAX_BOOT_ATTEMPTS;
        HAL_UART_Transmit(&huart1, (uint8_t *)&max, 1, 10);
        uart_print(")\r\n");

        meta->app_b_boot_count++;
        meta->app_b_first_boot = 1;
        if (!Metadata_Write(meta)) {
            uart_print("Metadata write failed, aborting OTA boot\r\n");
            return APP_FACTORY;
        }
        return APP_B_OTA;
    }

    // Default to factory application
    return APP_FACTORY;
}

/* ---- Jump to application ---- */

/**
 * @brief Performs a safe jump to the specified application address.
 *
 * Deinitializes peripherals, disables interrupts, sets up the vector table,
 * configures the stack pointer, and transfers control to the application.
 *
 * @why Enables bootloader-to-application transitions with proper cleanup
 *      and vector table setup.
 *
 * @param addr Base address of the application to jump to.
 */
__attribute__((noreturn))
static void JumpToApplication(uint32_t addr)
{
    const app_vector_t *vectors = (const app_vector_t *)addr;

    uart_print("Jumping to ");
    uart_print_hex(addr);
    uart_print("\r\n");
    HAL_Delay(10);

    // Clean shutdown of peripherals
    HAL_UART_DeInit(&huart1);
    HAL_RCC_DeInit();
    HAL_DeInit();

    // Disable all interrupts and clear pending ones
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    for (uint32_t i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    // Remap vector table to application address
    __HAL_SYSCFG_REMAPMEMORY_FLASH();
    SCB->VTOR = addr;
    __DSB();
    __ISB();

    // Set up application stack and jump to reset handler
    __set_MSP(vectors->stack_pointer);
    __DSB();
    __ISB();

    vectors->reset_handler();

    while (1) {}
}

/* ---- Error state (no valid app) ---- */

/**
 * @brief Error handler for when no valid application is found.
 *
 * Displays error message and blinks LED indefinitely.
 *
 * @why Provides a visible error indication when the bootloader cannot
 *      find a valid application to boot.
 */
__attribute__((noreturn))
static void Boot_Error(void)
{
    uart_print("BOOT FAILED: No valid application!\r\n");
    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(100);
    }
}

/* ---- Main ---- */

/**
 * @brief OTA bootloader entry point.
 *
 * Reads metadata, makes boot decision, verifies selected application,
 * and jumps to it with proper error handling.
 *
 * @why Implements the core OTA bootloader logic that can boot either
 *      factory or OTA applications based on metadata state.
 *
 * @return int (never returns).
 */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    uart_print("\r\n--- OTA Bootloader v1.0 ---\r\n");

    OTA_Metadata_t meta;
    Metadata_Read(&meta);

    App_Type_t app;
    uint32_t app_addr = APP_A_START_ADDR;

    // Determine which application to boot
    if (!Metadata_IsValid(&meta)) {
        uart_print("Metadata invalid, booting factory\r\n");
        app = APP_FACTORY;
    } else {
        app = Boot_Decide(&meta);
    }

    // Verify App B if selected
    if (app == APP_B_OTA) {
        app_addr = APP_B_START_ADDR;
        uart_print("Verifying App B...\r\n");
        if (!App_VerifyCRC(app_addr, meta.app_b_size, meta.app_b_crc32,
                           APP_B_MAX_SIZE)) {
            uart_print("App B failed, fallback to factory\r\n");
            meta.app_b_status = APP_STATUS_INVALID;
            if (!Metadata_Write(&meta)) {
                uart_print("Metadata write failed\r\n");
            }
            app = APP_FACTORY;
        }
    }

    // Verify App A (factory) if selected
    if (app == APP_FACTORY) {
        app_addr = APP_A_START_ADDR;
        uart_print("Verifying App A...\r\n");
        if (Metadata_IsValid(&meta) && meta.app_a_size > 0) {
            // Use stored CRC if available
            if (!App_VerifyCRC(app_addr, meta.app_a_size, meta.app_a_crc32,
                               APP_A_MAX_SIZE))
                Boot_Error();
        } else {
            // Fallback to basic vector table check for first boot
            if (!App_Verify(app_addr))
                Boot_Error();
        }
    }

    uart_print(app == APP_B_OTA ? "-> Boot App B (OTA)\r\n" : "-> Boot App A (Factory)\r\n");
    JumpToApplication(app_addr);
}

/* ---- Peripheral init ---- */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
        Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
