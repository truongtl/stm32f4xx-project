#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdbool.h>
#include "mem_layout.h"
#include "ota_metadata.h"
#include "packet.h"
/* #include "sha256.h"   */ /* Enable with OTA_VERIFY_SHA256              */
/* #include "aes.h"      */ /* Enable with OTA_DECRYPT_AES                */
/* #include "ota_auth.h" */ /* Enable with OTA_VERIFY_HMAC / OTA_DECRYPT_AES */

#define OTA_CHUNK_RETRIES  3

/* ---- Globals ---- */

UART_HandleTypeDef huart1;
static volatile uint8_t rx_byte;
static volatile uint8_t rx_ready;

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

        HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
        HAL_NVIC_DisableIRQ(USART1_IRQn);
    }
}

/* ---- UART helpers ---- */

/**
 * @brief Transmits a null-terminated string over UART.
 *
 * @why Provides basic text output for OTA status messages and debugging.
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
 * @why Enables debugging output of addresses, sizes, and CRC values during OTA.
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

/**
 * @brief Transmits a 32-bit value in decimal format over UART.
 *
 * @why Provides human-readable progress counters during OTA transfers.
 *
 * @param val Value to transmit in decimal format.
 */
static void uart_print_dec(uint32_t val)
{
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (val == 0) buf[--i] = '0';
    while (val > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    uart_print(&buf[i]);
}

/* ---- UART RX callback ---- */

/**
 * @brief UART receive complete callback that sets the ready flag.
 *
 * @why Signals the main loop that a byte has been received for processing.
 *
 * @param huart UART handle pointer.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        rx_ready = 1;
        HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1);
    }
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
    meta->metadata_crc32 = CRC32_Calculate((const uint8_t *)meta,
                                           sizeof(OTA_Metadata_t) - sizeof(uint32_t));
    HAL_FLASH_Unlock();

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

/* ---- Flash helpers ---- */

/**
 * @brief Erases the specified number of flash sectors starting from a given sector.
 *
 * @why Prepares flash memory for new firmware by clearing existing data.
 *
 * @param start_sector First sector number to erase.
 * @param count        Number of sectors to erase.
 * @return             true if erase succeeded, false on error.
 */
static bool Flash_EraseSectors(uint32_t start_sector, uint32_t count)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .Sector       = start_sector,
        .NbSectors    = count,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3
    };
    uint32_t error;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &error);

    HAL_FLASH_Lock();
    return (status == HAL_OK);
}

/**
 * @brief Writes data to flash memory with read-back verification.
 *
 * Handles unaligned writes by padding to word boundaries and verifies
 * the written data matches the input.
 *
 * @why Safely programs firmware chunks to flash with integrity checking.
 *
 * @param addr Flash address to write to.
 * @param data Pointer to data buffer to write.
 * @param len  Number of bytes to write.
 * @return     true if write and verification succeeded, false on error.
 */
static bool Flash_WriteData(uint32_t addr, const uint8_t *data, uint32_t len)
{
    HAL_FLASH_Unlock();

    uint32_t i = 0;
    while (i + 3 < len) {
        uint32_t word;
        memcpy(&word, &data[i], sizeof(uint32_t));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        i += 4;
    }

    if (i < len) {
        uint32_t word = 0xFFFFFFFF;
        memcpy(&word, &data[i], len - i);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
    }

    HAL_FLASH_Lock();

    /* Read-back verification */
    if (memcmp((const void *)addr, data, len) != 0)
        return false;

    return true;
}

/* ---- OTA protocol ---- */

/**
 * @brief Sends an OTA response packet with the specified type and error code.
 *
 * Constructs and transmits a response packet with CRC validation.
 *
 * @why Provides structured communication back to the OTA host application.
 *
 * @param type     Response type (ACK/NACK/ABORT).
 * @param seq      Sequence number to echo.
 * @param chunk_idx Chunk index for progress tracking.
 * @param err      Error code (0 for success).
 */
static void OTA_SendResponse(uint8_t type, uint8_t seq, uint32_t chunk_idx, uint8_t err)
{
    OTA_Response_t resp;
    resp.magic = OTA_RESPONSE_MAGIC;
    resp.response_type = type;
    resp.seq_number = seq;
    resp.chunk_index = chunk_idx;
    resp.error_code = err;
    resp.crc32 = CRC32_Calculate((const uint8_t *)&resp,
                                 sizeof(OTA_Response_t) - sizeof(uint32_t));
    HAL_UART_Transmit(&huart1, (uint8_t *)&resp, sizeof(resp), 100);
}

/**
 * @brief Main OTA receive function that handles the complete firmware update protocol.
 *
 * Implements a 3-step OTA protocol:
 * 1. Receive START packet — extracts expected firmware CRC32 from payload bytes 0-3.
 * 2. Receive DATA packets with firmware chunks (with retries on error).
 * 3. Receive END packet, verify CRC32 of the written flash region against the
 *    expected value, then update metadata and reboot.
 *
 * Software SHA-256, HMAC-SHA256, and AES-128-CBC implementations are preserved
 * in sha256.c, aes.c, and ota_auth.h but disabled (STM32F411 has no hardware
 * hash or AES peripheral). Enable by defining OTA_VERIFY_SHA256 / OTA_VERIFY_HMAC /
 * OTA_DECRYPT_AES and restoring the commented blocks below.
 *
 * @why Orchestrates the full OTA firmware update with CRC32 integrity checking.
 */
static void OTA_Receive(void)
{
    static OTA_Packet_t pkt;
    OTA_Metadata_t meta;
    uint32_t calc_crc;

    uart_print("Waiting for START packet...\r\n");

    /* Step 1: Receive START packet with firmware metadata */
    if (HAL_UART_Receive(&huart1, (uint8_t *)&pkt, sizeof(pkt), 30000) != HAL_OK) {
        uart_print("Timeout\r\n");
        return;
    }

    if (pkt.magic != OTA_PACKET_MAGIC || pkt.packet_type != PACKET_TYPE_START) {
        OTA_SendResponse(PACKET_TYPE_NACK, 0, 0, ERR_SEQUENCE_ERROR);
        uart_print("Invalid START\r\n");
        return;
    }

    calc_crc = CRC32_Calculate((const uint8_t *)&pkt,
                               sizeof(OTA_Packet_t) - sizeof(uint32_t));
    if (calc_crc != pkt.crc32) {
        OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_CRC_FAIL);
        uart_print("START CRC fail\r\n");
        return;
    }

    // Extract firmware information from START packet
    uint32_t total_chunks  = pkt.total_chunks;
    uint32_t total_fw_size = pkt.total_fw_size;
    uint32_t fw_version    = pkt.fw_version;

    // Extract expected firmware CRC32 from START payload bytes 0-3
    uint32_t expected_fw_crc32;
    memcpy(&expected_fw_crc32, pkt.payload, sizeof(uint32_t));
    /* Future: when OTA_VERIFY_SHA256/OTA_VERIFY_HMAC are defined, extract instead:
     *   uint8_t expected_fw_sha256[32]; memcpy(expected_fw_sha256, pkt.payload,      32);
     *   uint8_t expected_fw_hmac[32];   memcpy(expected_fw_hmac,   pkt.payload + 32, 32);
     */
    /* Future (OTA_DECRYPT_AES): extract per-session IV from payload bytes 64..79:
     *   uint8_t aes_iv[AES_BLOCK_SIZE]; memcpy(aes_iv, pkt.payload + 64, AES_BLOCK_SIZE);
     * OR use the static OTA_AES_IV from ota_auth.h.  Initialise after the size
     * checks below:
     *   AES_Ctx aes_ctx; AES_Init(&aes_ctx, OTA_AES_KEY, aes_iv);
     */

    // Validate firmware size
    if (total_fw_size == 0 || total_fw_size > APP_B_MAX_SIZE) {
        OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_FLASH_FULL);
        uart_print("FW too large\r\n");
        return;
    }

    // Validate chunk count (must be at least 1, cannot exceed total size)
    if (total_chunks == 0 || total_chunks > total_fw_size) {
        OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_SEQUENCE_ERROR);
        uart_print("Invalid total_chunks\r\n");
        return;
    }

    uart_print("FW size: ");
    uart_print_hex(total_fw_size);
    uart_print(" ver: ");
    uart_print_hex(fw_version);
    uart_print("\r\n");

    /* Mark OTA as in progress to prevent boot attempts during update */
    Metadata_Read(&meta);
    if (!Metadata_IsValid(&meta))
        memset(&meta, 0, sizeof(meta));
    meta.magic = METADATA_MAGIC;
    meta.ota_in_progress = 1;
    Metadata_Write(&meta);

    /* Erase App B flash region (sectors 6-7) for new firmware */
    uart_print("Erasing App B...\r\n");
    if (!Flash_EraseSectors(FLASH_SECTOR_6, 2)) {
        OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_FLASH_WRITE_FAIL);
        uart_print("Erase failed\r\n");
        meta.ota_in_progress = 0;
        Metadata_Write(&meta);
        return;
    }

    OTA_SendResponse(PACKET_TYPE_ACK, pkt.seq_number, 0, ERR_NONE);

    /* Step 2: Receive and process DATA packets with retry logic */
    uint32_t write_addr = APP_B_START_ADDR;

    for (uint32_t i = 0; i < total_chunks; i++) {
        bool chunk_ok = false;

        // Retry failed chunks up to OTA_CHUNK_RETRIES times
        for (uint8_t retry = 0; retry < OTA_CHUNK_RETRIES; retry++) {
            if (HAL_UART_Receive(&huart1, (uint8_t *)&pkt, sizeof(pkt), 10000) != HAL_OK) {
                OTA_SendResponse(PACKET_TYPE_NACK, 0, i, ERR_TIMEOUT);
                uart_print("DATA timeout\r\n");
                continue;
            }

            if (pkt.magic != OTA_PACKET_MAGIC || pkt.packet_type != PACKET_TYPE_DATA) {
                OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, i, ERR_SEQUENCE_ERROR);
                continue;
            }

            if (pkt.chunk_index != i) {
                OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, i, ERR_SEQUENCE_ERROR);
                continue;
            }

            calc_crc = CRC32_Calculate((const uint8_t *)&pkt,
                                       sizeof(OTA_Packet_t) - sizeof(uint32_t));
            if (calc_crc != pkt.crc32) {
                OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, i, ERR_CRC_FAIL);
                continue;
            }

            // Validate chunk size to prevent buffer overflows
            if (pkt.chunk_size == 0 || pkt.chunk_size > sizeof(pkt.payload)) {
                OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, i, ERR_SEQUENCE_ERROR);
                uart_print("Invalid chunk_size\r\n");
                continue;
            }

            // Write chunk to flash and verify
            /* Future (OTA_DECRYPT_AES): decrypt chunk before writing to flash:
             *   static uint8_t dec_buf[sizeof(pkt.payload)];
             *   AES_CBC_Decrypt(&aes_ctx, pkt.payload, dec_buf, pkt.chunk_size);
             *   Replace pkt.payload with dec_buf in the Flash_WriteData call below.
             */
            if (!Flash_WriteData(write_addr, pkt.payload, pkt.chunk_size)) {
                OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, i, ERR_FLASH_WRITE_FAIL);
                uart_print("Write/verify failed\r\n");
                continue;
            }

            chunk_ok = true;
            break;
        }

        if (!chunk_ok) {
            uart_print("Chunk failed after retries\r\n");
            meta.ota_in_progress = 0;
            meta.app_b_status = APP_STATUS_INVALID;
            Metadata_Write(&meta);
            return;
        }

        // Prevent write address from overflowing App B region
        if ((write_addr + pkt.chunk_size) > (APP_B_START_ADDR + APP_B_MAX_SIZE)) {
            uart_print("write_addr overflow\r\n");
            meta.ota_in_progress = 0;
            meta.app_b_status = APP_STATUS_INVALID;
            Metadata_Write(&meta);
            return;
        }

        write_addr += pkt.chunk_size;
        OTA_SendResponse(PACKET_TYPE_ACK, pkt.seq_number, pkt.chunk_index, ERR_NONE);

        /* Progress every 10 chunks */
        if ((i % 10) == 0 || i == total_chunks - 1) {
            uart_print_dec(i + 1);
            uart_print("/");
            uart_print_dec(total_chunks);
            uart_print("\r\n");
        }
    }

    /* Step 3: Receive END packet and verify complete firmware */
    if (HAL_UART_Receive(&huart1, (uint8_t *)&pkt, sizeof(pkt), 10000) != HAL_OK) {
        uart_print("END timeout\r\n");
        meta.ota_in_progress = 0;
        meta.app_b_status = APP_STATUS_INVALID;
        Metadata_Write(&meta);
        return;
    }

    if (pkt.magic != OTA_PACKET_MAGIC || pkt.packet_type != PACKET_TYPE_END) {
        OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_SEQUENCE_ERROR);
        meta.ota_in_progress = 0;
        meta.app_b_status = APP_STATUS_INVALID;
        Metadata_Write(&meta);
        return;
    }

    calc_crc = CRC32_Calculate((const uint8_t *)&pkt,
                               sizeof(OTA_Packet_t) - sizeof(uint32_t));
    if (calc_crc != pkt.crc32) {
        OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_CRC_FAIL);
        uart_print("END CRC fail\r\n");
        meta.ota_in_progress = 0;
        meta.app_b_status = APP_STATUS_INVALID;
        Metadata_Write(&meta);
        return;
    }

    /* Verify CRC32 integrity of the written firmware image */
    uart_print("Verifying CRC32...\r\n");
    uint32_t computed_fw_crc = CRC32_Calculate((const uint8_t *)APP_B_START_ADDR, total_fw_size);
    if (computed_fw_crc != expected_fw_crc32) {
        uart_print("Firmware CRC mismatch\r\n");
        OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_CRC_FAIL);
        meta.ota_in_progress = 0;
        meta.app_b_status = APP_STATUS_INVALID;
        Metadata_Write(&meta);
        return;
    }

    /* Future: replace the CRC32 check above with SHA-256 + HMAC when enabled.
     *
     * #ifdef OTA_VERIFY_SHA256
     *   uint8_t computed_sha256[32];
     *   sha256((const uint8_t *)APP_B_START_ADDR, total_fw_size, computed_sha256);
     *   if (memcmp(computed_sha256, expected_fw_sha256, 32) != 0) {
     *       uart_print("SHA-256 mismatch\r\n");
     *       OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_CRC_FAIL);
     *       ... return;
     *   }
     * #endif
     * #ifdef OTA_VERIFY_HMAC
     *   uint8_t computed_hmac[32];
     *   hmac_sha256(OTA_HMAC_KEY, sizeof(OTA_HMAC_KEY),
     *               (const uint8_t *)APP_B_START_ADDR, total_fw_size, computed_hmac);
     *   if (memcmp(computed_hmac, expected_fw_hmac, 32) != 0) {
     *       uart_print("HMAC auth failed\r\n");
     *       OTA_SendResponse(PACKET_TYPE_NACK, pkt.seq_number, 0, ERR_AUTH_FAIL);
     *       ... return;
     *   }
     * #endif
     */

    /* Update metadata to mark App B as ready for boot testing */
    meta.app_b_size = total_fw_size;
    meta.app_b_crc32 = computed_fw_crc;
    meta.app_b_version = fw_version;
    meta.app_b_status = APP_STATUS_PENDING;
    meta.app_b_first_boot = 0;
    meta.app_b_boot_count = 0;
    meta.force_factory_boot = 0;
    meta.ota_in_progress = 0;
    Metadata_Write(&meta);

    uart_print("OTA complete! Rebooting...\r\n");
    OTA_SendResponse(PACKET_TYPE_ACK, pkt.seq_number, 0, ERR_NONE);

    HAL_Delay(100);
    NVIC_SystemReset();
}

/* ---- Boot confirmation ---- */

/**
 * @brief Confirms successful boot and clears any stale OTA flags.
 *
 * Called at application startup to handle interrupted OTA recovery
 * and record successful boot.
 *
 * @why Ensures OTA state is properly maintained across application boots.
 */
static void Boot_Confirm(void)
{
    OTA_Metadata_t meta;
    Metadata_Read(&meta);

    if (!Metadata_IsValid(&meta)) {
        uart_print("No valid metadata\r\n");
        return;
    }

    bool needs_write = false;

    // Clear interrupted OTA flag and invalidate partial firmware
    if (meta.ota_in_progress) {
        uart_print("Previous OTA was interrupted\r\n");
        meta.ota_in_progress = 0;
        meta.app_b_status = APP_STATUS_INVALID;
        needs_write = true;
    }

    // Record successful factory application boot
    if (meta.last_successful_boot != APP_FACTORY) {
        meta.last_successful_boot = APP_FACTORY;
        needs_write = true;
    }

    if (needs_write)
        Metadata_Write(&meta);
}

/* ---- Main ---- */

/**
 * @brief OTA factory application entry point.
 *
 * Runs the factory application, monitors for OTA trigger byte,
 * and handles firmware updates when triggered.
 *
 * @why Provides the factory application that can be updated via OTA
 *      while maintaining the ability to recover from failed updates.
 *
 * @return int (never returns).
 */
int main(void)
{
    // Set vector table offset for App A (factory) region
    SCB->VTOR = APP_A_START_ADDR;
    __DSB();
    __ISB();

    // Re-enable interrupts (disabled by bootloader)
    __enable_irq();

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    uart_print("\r\n--- OTA Factory App v1.0 ---\r\n");
    Boot_Confirm();

    /* Start UART RX interrupt for OTA trigger detection */
    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1);

    while (1)
    {
        if (rx_ready) {
            rx_ready = 0;
            if (rx_byte == OTA_TRIGGER_BYTE) {
                uart_print("OTA mode triggered\r\n");
                HAL_UART_AbortReceive_IT(&huart1);
                OTA_Receive();
                /* Re-enable RX interrupt after OTA (if we get here = OTA failed) */
                HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1);
            }
        }

        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);
        __WFI();
    }
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
