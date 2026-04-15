#pragma once

#define BOOT_START_ADDR		0x08000000U  // 32KB, sector [1:0]
#define APP_HEADER_ADDR		0x08008000U  // 16KB, sector [2]
#define APP_START_ADDR		0x0800C000U  // 464KB, sector [7:3]
#define APP_FLASH_SIZE		0x00074000U  // 464KB
