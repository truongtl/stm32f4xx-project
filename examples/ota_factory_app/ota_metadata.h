#pragma once

#include <stdint.h>

#define METADATA_MAGIC          0xDEADBEEF
#define MAX_BOOT_ATTEMPTS       3

#define APP_STATUS_INVALID      0x00
#define APP_STATUS_PENDING      0x01
#define APP_STATUS_VALID        0x02

typedef enum {
    APP_FACTORY = 0,
    APP_B_OTA   = 1
} App_Type_t;

typedef struct {
    uint32_t magic;

    // App A (Factory) info
    uint32_t app_a_size;
    uint32_t app_a_crc32;
    uint32_t app_a_version;

    // App B (OTA) info
    uint32_t app_b_size;
    uint32_t app_b_crc32;
    uint32_t app_b_version;
    uint8_t  app_b_status;
    uint8_t  app_b_first_boot;
    uint8_t  app_b_boot_count;

    // Boot control
    uint8_t  force_factory_boot;
    uint8_t  ota_in_progress;
    uint8_t  _reserved[3];

    // Rollback protection
    uint32_t last_successful_boot;

    uint32_t metadata_crc32;
} __attribute__((aligned(4))) OTA_Metadata_t;
