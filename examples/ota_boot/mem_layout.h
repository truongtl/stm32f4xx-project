#pragma once

#define BOOT_START_ADDR		    0x08000000  // 32KB, sector [1:0]
#define METADATA_START_ADDR		0x08008000  // 16KB, sector [2]
#define APP_A_START_ADDR		0x0800C000  // 208KB, sector [5:3]
#define APP_B_START_ADDR		0x08040000  // 256KB, sector [7:6]

#define FLASH_END_ADDR          0x08080000
#define APP_A_MAX_SIZE          (APP_B_START_ADDR - APP_A_START_ADDR)  // 208KB
#define APP_B_MAX_SIZE          (FLASH_END_ADDR - APP_B_START_ADDR)    // 256KB
