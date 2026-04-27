#pragma once

#include <stdint.h>

/**
 * Shared cryptographic keys for OTA firmware authentication and encryption.
 *
 * ALL mechanisms DISABLED. To enable individual features:
 *
 *   HMAC-SHA256 (OTA_VERIFY_HMAC):
 *     1. Define OTA_VERIFY_HMAC in your build.
 *     2. Include "sha256.h" and "ota_auth.h" in ota_factory_app/main.c.
 *     3. Uncomment OTA_HMAC_KEY and the HMAC block in OTA_Receive().
 *     4. Use the same key in tests/ota_host.py (OTA_HMAC_KEY).
 *
 *   AES-128-CBC (OTA_DECRYPT_AES):
 *     1. Define OTA_DECRYPT_AES in your build.
 *     2. Include "aes.h" and "ota_auth.h" in ota_factory_app/main.c.
 *     3. Uncomment OTA_AES_KEY / OTA_AES_IV and the AES block in OTA_Receive().
 *     4. Use the same key and IV in tests/ota_host.py (OTA_AES_KEY, OTA_AES_IV).
 *
 * STM32F411 has no hardware hash or AES peripheral; both mechanisms use
 * pure software implementations (sha256.c, aes.c).
 *
 * Security notes when enabled:
 *  - Change all keys and the IV before deploying to production hardware.
 *  - Protect bootloader flash with STM32 RDP Level 1 (JTAG lock).
 *  - For stronger authentication, replace HMAC with ECDSA (e.g. micro-ecc).
 *  - For AES, use a random per-session IV carried in the START packet
 *    (payload bytes 64..79) instead of this static IV to prevent IV reuse.
 */

/*
 * HMAC-SHA256 authentication key — enable with OTA_VERIFY_HMAC.
 * Must match OTA_HMAC_KEY in tests/ota_host.py when active.
 */
/*
static const uint8_t OTA_HMAC_KEY[32] = {
    0x5A, 0x3F, 0x9C, 0x12, 0xE7, 0x4B, 0xD8, 0x01,
    0xAB, 0xC6, 0x27, 0x84, 0x3E, 0xF5, 0x9D, 0x70,
    0x1C, 0x68, 0xA2, 0xBB, 0x4F, 0x93, 0xD6, 0x0E,
    0x72, 0x51, 0xC4, 0x87, 0x3A, 0x9E, 0xF2, 0x16,
};
*/

/*
 * AES-128-CBC channel encryption key — enable with OTA_DECRYPT_AES.
 * Must match OTA_AES_KEY in tests/ota_host.py when active.
 * Default value is the FIPS 197 Appendix B test key — change before production.
 */
/*
static const uint8_t OTA_AES_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C,
};
*/

/*
 * AES-128-CBC initialisation vector — enable with OTA_DECRYPT_AES.
 * Must match OTA_AES_IV in tests/ota_host.py when active.
 * For production: extract a random per-session IV from START payload
 * bytes 64..79 instead of using this static value.
 */
/*
static const uint8_t OTA_AES_IV[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};
*/
