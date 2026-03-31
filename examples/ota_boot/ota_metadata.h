typedef struct {
    uint32_t magic;                   // 0xDEADBEEF

    // App A (Factory) info
    uint32_t app_a_size;
    uint32_t app_a_crc32;
    uint32_t app_a_version;

    // App B (OTA) info
    uint32_t app_b_size;
    uint32_t app_b_crc32;
    uint32_t app_b_version;
    uint8_t  app_b_status;            // VALID/INVALID/PENDING
    uint8_t  app_b_first_boot;        // Flag boot lần đầu
    uint8_t  app_b_boot_count;        // Số lần thử boot

    // Boot control
    uint8_t  force_factory_boot;      // Cờ buộc boot factory
    uint8_t  ota_in_progress;         // Đang OTA

    // Rollback protection
    uint32_t last_successful_boot;    // APP_FACTORY hoặc APP_B_OTA

    uint32_t metadata_crc32;          // CRC của metadata
} OTA_Metadata_t;

// App status
#define APP_STATUS_INVALID      0x00
#define APP_STATUS_PENDING      0x01  // Đã download, chưa verify
#define APP_STATUS_VALID        0x02  // Đã verify và boot thành công