#pragma once

#include <stdint.h>

/**
 * SHA-256 hash context.  Holds the running hash state, total byte count,
 * and a partial-block accumulator buffer.
 */
typedef struct {
    uint32_t state[8];   /**< Current hash state (256 bits across 8 words). */
    uint64_t count;      /**< Total bytes hashed so far (for length padding). */
    uint8_t  buf[64];    /**< Partial 64-byte block accumulator. */
    uint32_t buflen;     /**< Number of valid bytes currently in buf. */
} SHA256_Ctx;

void sha256_init  (SHA256_Ctx *ctx);
void sha256_update(SHA256_Ctx *ctx, const uint8_t *data, uint32_t len);
void sha256_final (SHA256_Ctx *ctx, uint8_t digest[32]);

void sha256      (const uint8_t *data, uint32_t len, uint8_t digest[32]);
void hmac_sha256 (const uint8_t *key,  uint32_t key_len,
                  const uint8_t *data, uint32_t data_len,
                  uint8_t digest[32]);
