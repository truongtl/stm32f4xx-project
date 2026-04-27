#include "sha256.h"
#include <string.h>

/* ---- SHA-256 constants (FIPS 180-4) ---- */

/*
 * Round constants: first 32 bits of the fractional parts of the cube roots
 * of the first 64 prime numbers.
 */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/*
 * Initial hash values: first 32 bits of the fractional parts of the square
 * roots of the first 8 prime numbers.
 */
static const uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/* ---- Bit manipulation helpers ---- */

#define ROTR32(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))

/* SHA-256 logical functions */
#define Ch(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define Sigma0(x)     (ROTR32(x,  2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define Sigma1(x)     (ROTR32(x,  6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define sigma0(x)     (ROTR32(x,  7) ^ ROTR32(x, 18) ^ ((x) >>  3))
#define sigma1(x)     (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

/* Load a 32-bit big-endian word from unaligned bytes */
#define LOAD_BE32(p) \
    ((uint32_t)((const uint8_t *)(p))[0] << 24 | \
     (uint32_t)((const uint8_t *)(p))[1] << 16 | \
     (uint32_t)((const uint8_t *)(p))[2] <<  8 | \
     (uint32_t)((const uint8_t *)(p))[3])

/* Store a 32-bit big-endian word to a byte array */
#define STORE_BE32(p, v) do {                    \
    ((uint8_t *)(p))[0] = (uint8_t)((v) >> 24); \
    ((uint8_t *)(p))[1] = (uint8_t)((v) >> 16); \
    ((uint8_t *)(p))[2] = (uint8_t)((v) >>  8); \
    ((uint8_t *)(p))[3] = (uint8_t)((v)      ); \
} while (0)

/* ---- Core transform ---- */

/**
 * @brief Processes one 64-byte block through the SHA-256 compression function.
 *
 * Builds a 64-word message schedule from the input block, then runs 64
 * mixing rounds using the six SHA-256 boolean functions and the round
 * constants K[].  The resulting working variables are added back into
 * ctx->state, following FIPS 180-4 section 6.2.2.
 *
 * @why Performs the core cryptographic work that makes SHA-256 a
 *      one-way function resistant to collision and pre-image attacks.
 *
 * @param ctx   SHA-256 context whose state is updated in-place.
 * @param block Pointer to the 64-byte input block (big-endian byte order).
 */
static void sha256_transform(SHA256_Ctx *ctx, const uint8_t block[64])
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h, T1, T2;
    int i;

    // Load first 16 words of message schedule from block (big-endian)
    for (i = 0; i < 16; i++)
        W[i] = LOAD_BE32(block + i * 4);

    // Extend message schedule to 64 words
    for (i = 16; i < 64; i++)
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        T1 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i];
        T2 = Sigma0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

/* ---- Public API ---- */

/**
 * @brief Initialises a SHA-256 context for a new hash computation.
 *
 * @why Must be called before sha256_update() to set up the initial hash
 *      state and reset counters.
 *
 * @param ctx SHA-256 context to initialise.
 */
void sha256_init(SHA256_Ctx *ctx)
{
    memcpy(ctx->state, H0, sizeof(H0));
    ctx->count  = 0;
    ctx->buflen = 0;
}

/**
 * @brief Feeds a chunk of data into an in-progress SHA-256 computation.
 *
 * Buffers input until a full 64-byte block is available, then calls
 * sha256_transform().  May be called repeatedly with arbitrary-sized
 * chunks; the final hash is obtained via sha256_final().
 *
 * @why Supports streaming hashing over large firmware images that cannot
 *      be held in RAM all at once.
 *
 * @param ctx  SHA-256 context.
 * @param data Pointer to input bytes.
 * @param len  Number of bytes to process.
 */
void sha256_update(SHA256_Ctx *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t have = ctx->buflen;

    ctx->count += len;

    // Fill partial block buffer first
    if (have > 0) {
        uint32_t need = 64 - have;
        if (len < need) {
            memcpy(ctx->buf + have, data, len);
            ctx->buflen = have + len;
            return;
        }
        memcpy(ctx->buf + have, data, need);
        sha256_transform(ctx, ctx->buf);
        data += need;
        len  -= need;
        ctx->buflen = 0;
    }

    // Process full 64-byte blocks directly from input
    while (len >= 64) {
        sha256_transform(ctx, data);
        data += 64;
        len  -= 64;
    }

    // Buffer remaining bytes for next call
    if (len > 0) {
        memcpy(ctx->buf, data, len);
        ctx->buflen = len;
    }
}

/**
 * @brief Finalises the SHA-256 computation and writes the 32-byte digest.
 *
 * Appends the mandatory 0x80 padding byte, zero bytes to fill to 56 bytes
 * (mod 64), and the 64-bit big-endian bit-count, then transforms the last
 * block(s) and serialises the eight 32-bit state words into the digest.
 *
 * @why Produces the final hash value conforming to FIPS 180-4.
 *
 * @param ctx    SHA-256 context (consumed; do not reuse without re-init).
 * @param digest Output buffer for the 32-byte SHA-256 digest.
 */
void sha256_final(SHA256_Ctx *ctx, uint8_t digest[32])
{
    uint8_t  pad[64];
    uint64_t bit_count = ctx->count * 8;  // total input length in bits
    uint32_t rem = ctx->buflen;
    uint32_t pad_len;
    uint8_t  len_bytes[8];
    int i;

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;  // mandatory 1-bit before zero padding

    // Pad to 56 bytes (mod 64) to leave room for the 8-byte length field
    pad_len = (rem < 56) ? (56 - rem) : (120 - rem);
    sha256_update(ctx, pad, pad_len);

    // Append original bit length as 64-bit big-endian integer
    len_bytes[0] = (uint8_t)(bit_count >> 56);
    len_bytes[1] = (uint8_t)(bit_count >> 48);
    len_bytes[2] = (uint8_t)(bit_count >> 40);
    len_bytes[3] = (uint8_t)(bit_count >> 32);
    len_bytes[4] = (uint8_t)(bit_count >> 24);
    len_bytes[5] = (uint8_t)(bit_count >> 16);
    len_bytes[6] = (uint8_t)(bit_count >>  8);
    len_bytes[7] = (uint8_t)(bit_count      );
    sha256_update(ctx, len_bytes, 8);

    // Serialise state words to digest bytes (big-endian)
    for (i = 0; i < 8; i++)
        STORE_BE32(digest + i * 4, ctx->state[i]);
}

/**
 * @brief Computes the SHA-256 hash of a complete buffer in one call.
 *
 * @why Convenience wrapper around init/update/final for buffers that fit
 *      in memory (e.g. packet payloads and flash regions).
 *
 * @param data   Pointer to input data.
 * @param len    Number of bytes to hash.
 * @param digest Output buffer for the 32-byte digest.
 */
void sha256(const uint8_t *data, uint32_t len, uint8_t digest[32])
{
    SHA256_Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/**
 * @brief Computes HMAC-SHA256 over a message with the supplied key.
 *
 * Implements RFC 2104 HMAC using SHA-256 as the underlying hash:
 *   HMAC(K, m) = SHA256((K0 XOR opad) || SHA256((K0 XOR ipad) || m))
 * Keys longer than 64 bytes are pre-hashed; shorter keys are zero-padded.
 *
 * @why Provides message authentication: only a party that knows the shared
 *      secret key can produce a valid HMAC, preventing unauthorised firmware
 *      from being accepted during an OTA transfer.
 *
 * @param key      Shared secret key.
 * @param key_len  Length of key in bytes.
 * @param data     Message to authenticate.
 * @param data_len Length of message in bytes.
 * @param digest   Output buffer for the 32-byte HMAC digest.
 */
void hmac_sha256(const uint8_t *key, uint32_t key_len,
                 const uint8_t *data, uint32_t data_len,
                 uint8_t digest[32])
{
    SHA256_Ctx ctx;
    uint8_t K0[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t inner[32];
    int i;

    // Prepare K0: hash long keys, zero-pad short keys
    memset(K0, 0, sizeof(K0));
    if (key_len > 64) {
        sha256(key, key_len, K0);
    } else {
        memcpy(K0, key, key_len);
    }

    // XOR K0 with HMAC ipad (0x36) and opad (0x5c) constants
    for (i = 0; i < 64; i++) {
        ipad[i] = K0[i] ^ 0x36;
        opad[i] = K0[i] ^ 0x5c;
    }

    // Inner hash: SHA256(ipad || message)
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner);

    // Outer hash: SHA256(opad || inner)
    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, digest);
}
