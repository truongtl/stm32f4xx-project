#pragma once

#include <stdint.h>

/**
 * AES-128-CBC software implementation.
 *
 * DISABLED — AES decryption is not active in the OTA pipeline. To enable:
 *   1. Define OTA_DECRYPT_AES in your build.
 *   2. Include "aes.h" and "ota_auth.h" in ota_factory_app/main.c.
 *   3. Uncomment OTA_AES_KEY / OTA_AES_IV in ota_auth.h.
 *   4. Restore the commented AES blocks in OTA_Receive() in main.c.
 *   5. Encrypt firmware chunks in tests/ota_host.py with OTA_AES_KEY / OTA_AES_IV.
 *
 * STM32F411 has no hardware AES peripheral; this is a pure software
 * implementation using S-box tables stored in flash.
 *
 * Usage (device side — decrypt received chunks before writing to flash):
 *   AES_Ctx ctx;
 *   AES_Init(&ctx, OTA_AES_KEY, OTA_AES_IV);
 *   AES_CBC_Decrypt(&ctx, pkt.payload, plaintext_buf, chunk_size);
 *
 * Usage (host side — encrypt firmware before sending DATA packets):
 *   AES_Init(&ctx, key, iv);
 *   AES_CBC_Encrypt(&ctx, plaintext, ciphertext, len);
 *
 * Notes:
 *   - len must be a multiple of AES_BLOCK_SIZE (16).  Pad with PKCS#7 or
 *     zero-pad before encrypting; strip padding after decrypting.
 *   - The IV in ctx is updated after each call for multi-block continuity.
 *   - AES_CBC_Decrypt does NOT support in-place operation (ciphertext == plaintext).
 */

#define AES_BLOCK_SIZE    16U   /**< AES block size in bytes (128 bits). */
#define AES_KEY_SIZE      16U   /**< AES-128 key size in bytes. */
#define AES_KEY_EXP_SIZE 176U   /**< Expanded key: 11 round keys × 16 bytes. */

/**
 * AES context holding the expanded round keys and current CBC IV.
 * Initialise once with AES_Init() before calling encrypt or decrypt.
 */
typedef struct {
    uint8_t round_key[AES_KEY_EXP_SIZE]; /**< Pre-expanded round key schedule. */
    uint8_t iv[AES_BLOCK_SIZE];          /**< Current CBC chaining value (IV). */
} AES_Ctx;

void AES_Init       (AES_Ctx *ctx,
                     const uint8_t  key[AES_KEY_SIZE],
                     const uint8_t  iv[AES_BLOCK_SIZE]);

void AES_CBC_Encrypt(AES_Ctx *ctx,
                     const uint8_t *plaintext,
                     uint8_t       *ciphertext,
                     uint32_t       len);

void AES_CBC_Decrypt(AES_Ctx *ctx,
                     const uint8_t *ciphertext,
                     uint8_t       *plaintext,
                     uint32_t       len);
