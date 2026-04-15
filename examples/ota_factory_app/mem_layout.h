#pragma once

#define BOOT_START_ADDR         0x08000000  // 32KB, sectors 0-1
#define METADATA_START_ADDR     0x08008000  // 16KB, sector 2
#define APP_A_START_ADDR        0x0800C000  // App A: 208KB, sectors 3-5
#define APP_B_START_ADDR        0x08040000  // App B: 256KB, sectors 6-7

#define FLASH_END_ADDR          0x08080000
#define APP_A_MAX_SIZE          (APP_B_START_ADDR - APP_A_START_ADDR)  // 208KB
#define APP_B_MAX_SIZE          (FLASH_END_ADDR - APP_B_START_ADDR)    // 256KB
