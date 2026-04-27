#include "aes.h"
#include <string.h>

/* ---- AES S-box (FIPS 197 Figure 7) ---- */

static const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* ---- AES inverse S-box ---- */

static const uint8_t RSBOX[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

/* Round constant table: RCON[i] = 2^(i-1) in GF(2^8) for i = 1..10 */
static const uint8_t RCON[11] = {
    0x8d,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

/* ---- GF(2^8) arithmetic ---- */

/**
 * @brief Multiplies two bytes in GF(2^8) with the AES irreducible polynomial.
 *
 * Uses shift-and-add without lookup tables.  Polynomial: x^8+x^4+x^3+x+1
 * (0x11b).  Handles all 256×256 combinations correctly.
 *
 * @why Required for MixColumns and InvMixColumns column mixing.
 *
 * @param a First operand.
 * @param b Second operand.
 * @return  Product a × b in GF(2^8).
 */
static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    uint8_t result = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (b & 1U) result ^= a;
        // Multiply a by x (left-shift), then reduce if degree exceeds 7
        if (a & 0x80U) {
            a = (uint8_t)((a << 1) ^ 0x1bU);
        } else {
            a = (uint8_t)(a << 1);
        }
        b >>= 1;
    }
    return result;
}

/* ---- AES-128 key expansion ---- */

/**
 * @brief Expands a 128-bit key into 11 round keys (176 bytes).
 *
 * Implements the AES-128 key schedule (FIPS 197 §5.2): iterates 40 words
 * beyond the original key, applying RotWord, SubWord, and Rcon XOR at
 * every 4th word.
 *
 * @why Precomputes all round keys so block encrypt/decrypt only need
 *      table lookups, not live key derivation per block.
 *
 * @param key       16-byte AES-128 key (input).
 * @param round_key 176-byte output buffer for the expanded round key schedule.
 */
static void KeyExpansion(const uint8_t *key, uint8_t *round_key)
{
    uint32_t i;
    uint8_t  tmp[4];

    // First round key is the key itself
    memcpy(round_key, key, AES_KEY_SIZE);

    for (i = 4U; i < 44U; i++) {
        memcpy(tmp, round_key + (i - 1U) * 4U, 4U);

        if ((i & 3U) == 0U) {
            // RotWord: left-rotate word by one byte
            uint8_t t = tmp[0];
            tmp[0] = tmp[1]; tmp[1] = tmp[2]; tmp[2] = tmp[3]; tmp[3] = t;
            // SubWord: substitute each byte through the S-box
            tmp[0] = SBOX[tmp[0]]; tmp[1] = SBOX[tmp[1]];
            tmp[2] = SBOX[tmp[2]]; tmp[3] = SBOX[tmp[3]];
            // XOR first byte with round constant
            tmp[0] ^= RCON[i >> 2];
        }

        for (uint32_t j = 0U; j < 4U; j++)
            round_key[i * 4U + j] = round_key[(i - 4U) * 4U + j] ^ tmp[j];
    }
}

/* ---- AES state operations ---- */

/*
 * State layout convention: s[row][col], row and col in 0..3.
 * Input bytes are loaded column-by-column: s[r][c] = in[r + 4*c].
 * Round key bytes follow the same mapping: rk[r + 4*c] XORs s[r][c].
 */

/**
 * @brief XORs each state byte with the corresponding byte of the round key.
 *
 * @why Mixes round key material into the cipher state on every round.
 *
 * @param s  4×4 AES state matrix (modified in place).
 * @param rk Pointer to the 16-byte round key for this round.
 */
static void AddRoundKey(uint8_t s[4][4], const uint8_t *rk)
{
    int r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            s[r][c] ^= rk[r + 4 * c];
}

/**
 * @brief Applies the AES S-box substitution to every byte of the state.
 *
 * @why Provides the non-linear component of the AES round function.
 *
 * @param s 4×4 AES state matrix (modified in place).
 */
static void SubBytes(uint8_t s[4][4])
{
    int r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            s[r][c] = SBOX[s[r][c]];
}

/**
 * @brief Applies the inverse AES S-box substitution to every byte of the state.
 *
 * @why Inverse of SubBytes; required in the AES decryption round function.
 *
 * @param s 4×4 AES state matrix (modified in place).
 */
static void InvSubBytes(uint8_t s[4][4])
{
    int r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            s[r][c] = RSBOX[s[r][c]];
}

/**
 * @brief Cyclically shifts each row of the state to the left by its row index.
 *
 * Row 0: no shift. Row 1: shift left 1. Row 2: shift left 2. Row 3: shift left 3.
 *
 * @why Provides inter-column diffusion in the AES encryption round.
 *
 * @param s 4×4 AES state matrix (modified in place).
 */
static void ShiftRows(uint8_t s[4][4])
{
    uint8_t t;
    // Row 1: rotate left by 1
    t=s[1][0]; s[1][0]=s[1][1]; s[1][1]=s[1][2]; s[1][2]=s[1][3]; s[1][3]=t;
    // Row 2: rotate left by 2 (swap pairs)
    t=s[2][0]; s[2][0]=s[2][2]; s[2][2]=t;
    t=s[2][1]; s[2][1]=s[2][3]; s[2][3]=t;
    // Row 3: rotate left by 3 (= rotate right by 1)
    t=s[3][3]; s[3][3]=s[3][2]; s[3][2]=s[3][1]; s[3][1]=s[3][0]; s[3][0]=t;
}

/**
 * @brief Cyclically shifts each row of the state to the right by its row index.
 *
 * Inverse of ShiftRows; row 1 shifts right 1, row 2 right 2, row 3 right 3.
 *
 * @why Required in the AES decryption round function.
 *
 * @param s 4×4 AES state matrix (modified in place).
 */
static void InvShiftRows(uint8_t s[4][4])
{
    uint8_t t;
    // Row 1: rotate right by 1
    t=s[1][3]; s[1][3]=s[1][2]; s[1][2]=s[1][1]; s[1][1]=s[1][0]; s[1][0]=t;
    // Row 2: rotate right by 2 (swap pairs)
    t=s[2][0]; s[2][0]=s[2][2]; s[2][2]=t;
    t=s[2][1]; s[2][1]=s[2][3]; s[2][3]=t;
    // Row 3: rotate right by 3 (= rotate left by 1)
    t=s[3][0]; s[3][0]=s[3][1]; s[3][1]=s[3][2]; s[3][2]=s[3][3]; s[3][3]=t;
}

/**
 * @brief Multiplies each column of the state by the MixColumns matrix in GF(2^8).
 *
 * Applies the fixed MDS matrix [[2,3,1,1],[1,2,3,1],[1,1,2,3],[3,1,1,2]]
 * to each of the four state columns.
 *
 * @why Provides diffusion across rows within each column of the encryption state.
 *
 * @param s 4×4 AES state matrix (modified in place).
 */
static void MixColumns(uint8_t s[4][4])
{
    int c;
    for (c = 0; c < 4; c++) {
        uint8_t a0=s[0][c], a1=s[1][c], a2=s[2][c], a3=s[3][c];
        s[0][c] = gf_mul(0x02,a0) ^ gf_mul(0x03,a1) ^ a2             ^ a3;
        s[1][c] = a0              ^ gf_mul(0x02,a1) ^ gf_mul(0x03,a2) ^ a3;
        s[2][c] = a0              ^ a1              ^ gf_mul(0x02,a2) ^ gf_mul(0x03,a3);
        s[3][c] = gf_mul(0x03,a0) ^ a1              ^ a2              ^ gf_mul(0x02,a3);
    }
}

/**
 * @brief Applies the inverse MixColumns transformation to each state column.
 *
 * Applies the inverse MDS matrix [[0e,0b,0d,09],[09,0e,0b,0d],
 * [0d,09,0e,0b],[0b,0d,09,0e]] to each column in GF(2^8).
 *
 * @why Inverse of MixColumns; required in the AES decryption round function.
 *
 * @param s 4×4 AES state matrix (modified in place).
 */
static void InvMixColumns(uint8_t s[4][4])
{
    int c;
    for (c = 0; c < 4; c++) {
        uint8_t a0=s[0][c], a1=s[1][c], a2=s[2][c], a3=s[3][c];
        s[0][c] = gf_mul(0x0e,a0)^gf_mul(0x0b,a1)^gf_mul(0x0d,a2)^gf_mul(0x09,a3);
        s[1][c] = gf_mul(0x09,a0)^gf_mul(0x0e,a1)^gf_mul(0x0b,a2)^gf_mul(0x0d,a3);
        s[2][c] = gf_mul(0x0d,a0)^gf_mul(0x09,a1)^gf_mul(0x0e,a2)^gf_mul(0x0b,a3);
        s[3][c] = gf_mul(0x0b,a0)^gf_mul(0x0d,a1)^gf_mul(0x09,a2)^gf_mul(0x0e,a3);
    }
}

/* ---- AES-128 block cipher ---- */

/**
 * @brief Encrypts a single 16-byte plaintext block with AES-128 (FIPS 197 §5.1).
 *
 * Applies an initial AddRoundKey, 9 full rounds (SubBytes, ShiftRows,
 * MixColumns, AddRoundKey), then a final round without MixColumns.
 * Input and output may alias the same buffer.
 *
 * @why Core forward-cipher primitive used by AES_CBC_Encrypt.
 *
 * @param rk  176-byte expanded round key schedule from KeyExpansion.
 * @param in  16-byte plaintext input block.
 * @param out 16-byte ciphertext output block.
 */
static void AES_BlockEncrypt(const uint8_t *rk, const uint8_t *in, uint8_t *out)
{
    uint8_t s[4][4];
    int r, c, round;

    // Load state column-by-column as per FIPS 197 §3.4
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            s[r][c] = in[r + 4 * c];

    AddRoundKey(s, rk);

    for (round = 1; round < 10; round++) {
        SubBytes(s);
        ShiftRows(s);
        MixColumns(s);
        AddRoundKey(s, rk + round * 16);
    }

    // Final round: no MixColumns
    SubBytes(s);
    ShiftRows(s);
    AddRoundKey(s, rk + 10 * 16);

    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            out[r + 4 * c] = s[r][c];
}

/**
 * @brief Decrypts a single 16-byte ciphertext block with AES-128 (FIPS 197 §5.3).
 *
 * Applies an initial AddRoundKey with the last round key, 9 inverse rounds
 * (InvShiftRows, InvSubBytes, AddRoundKey, InvMixColumns), then a final
 * inverse round without InvMixColumns.
 * Input and output must NOT alias the same buffer.
 *
 * @why Core inverse-cipher primitive used by AES_CBC_Decrypt.
 *
 * @param rk  176-byte expanded round key schedule from KeyExpansion.
 * @param in  16-byte ciphertext input block.
 * @param out 16-byte plaintext output block (must not alias in).
 */
static void AES_BlockDecrypt(const uint8_t *rk, const uint8_t *in, uint8_t *out)
{
    uint8_t s[4][4];
    int r, c, round;

    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            s[r][c] = in[r + 4 * c];

    // Start from the last round key
    AddRoundKey(s, rk + 10 * 16);

    for (round = 9; round > 0; round--) {
        InvShiftRows(s);
        InvSubBytes(s);
        AddRoundKey(s, rk + round * 16);
        InvMixColumns(s);
    }

    // Final inverse round: no InvMixColumns
    InvShiftRows(s);
    InvSubBytes(s);
    AddRoundKey(s, rk);

    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            out[r + 4 * c] = s[r][c];
}

/* ---- Public API ---- */

/**
 * @brief Initialises an AES context with a 128-bit key and initialisation vector.
 *
 * Runs the AES-128 key schedule to precompute all 11 round keys, then stores
 * the IV for use as the first CBC chaining value.  Must be called before any
 * AES_CBC_Encrypt or AES_CBC_Decrypt call.
 *
 * @why One-time setup that amortises the key expansion cost across multiple blocks.
 *
 * @param ctx AES context to initialise.
 * @param key 16-byte AES-128 key.
 * @param iv  16-byte initialisation vector; copied into the context.
 */
void AES_Init(AES_Ctx *ctx, const uint8_t key[AES_KEY_SIZE],
              const uint8_t iv[AES_BLOCK_SIZE])
{
    KeyExpansion(key, ctx->round_key);
    memcpy(ctx->iv, iv, AES_BLOCK_SIZE);
}

/**
 * @brief Encrypts len bytes using AES-128-CBC.
 *
 * Processes data in 16-byte (AES_BLOCK_SIZE) blocks.  Any trailing bytes
 * that do not form a complete block are silently ignored — the caller must
 * pad plaintext to a block boundary before calling.  The IV held in ctx is
 * updated after each block so that sequential calls produce a continuous
 * CBC ciphertext stream.
 *
 * @why Encrypts firmware data on the host before transmission over UART so
 *      the device decrypts with AES_CBC_Decrypt before writing to flash.
 *
 * @param ctx        Initialised AES context; IV updated in place.
 * @param plaintext  Input plaintext bytes.
 * @param ciphertext Output ciphertext bytes (may alias plaintext).
 * @param len        Number of bytes to process; must be a multiple of 16.
 */
void AES_CBC_Encrypt(AES_Ctx *ctx, const uint8_t *plaintext,
                     uint8_t *ciphertext, uint32_t len)
{
    uint32_t i;
    uint8_t  block[AES_BLOCK_SIZE];
    int j;

    for (i = 0U; i + AES_BLOCK_SIZE <= len; i += AES_BLOCK_SIZE) {
        // XOR plaintext block with IV (or previous ciphertext block in CBC)
        for (j = 0; j < (int)AES_BLOCK_SIZE; j++)
            block[j] = plaintext[i + j] ^ ctx->iv[j];

        AES_BlockEncrypt(ctx->round_key, block, ciphertext + i);

        // Update IV to the freshly produced ciphertext block
        memcpy(ctx->iv, ciphertext + i, AES_BLOCK_SIZE);
    }
}

/**
 * @brief Decrypts len bytes using AES-128-CBC.
 *
 * Processes data in 16-byte blocks.  Trailing bytes beyond the last
 * complete block are silently ignored.  The IV held in ctx is updated
 * after each call for continuity across sequential calls.
 * Input and output buffers must NOT overlap.
 *
 * @why Decrypts AES-128-CBC-encrypted firmware chunks received over UART
 *      before writing plaintext to flash during OTA receive.
 *
 * @param ctx        Initialised AES context; IV updated in place.
 * @param ciphertext Input ciphertext bytes.
 * @param plaintext  Output plaintext bytes (must not alias ciphertext).
 * @param len        Number of bytes to process; must be a multiple of 16.
 */
void AES_CBC_Decrypt(AES_Ctx *ctx, const uint8_t *ciphertext,
                     uint8_t *plaintext, uint32_t len)
{
    uint32_t i;
    uint8_t  raw[AES_BLOCK_SIZE];
    uint8_t  prev[AES_BLOCK_SIZE];
    int j;

    // Initialise CBC chaining value from the current IV
    memcpy(prev, ctx->iv, AES_BLOCK_SIZE);

    for (i = 0U; i + AES_BLOCK_SIZE <= len; i += AES_BLOCK_SIZE) {
        // Decrypt ciphertext block to raw bytes
        AES_BlockDecrypt(ctx->round_key, ciphertext + i, raw);

        // XOR with previous ciphertext block (or IV for the first block)
        for (j = 0; j < (int)AES_BLOCK_SIZE; j++)
            plaintext[i + j] = raw[j] ^ prev[j];

        // Advance CBC chain: current ciphertext block becomes next IV
        memcpy(prev, ciphertext + i, AES_BLOCK_SIZE);
    }

    // Save last ciphertext block as new IV for potential continuation
    if (i > 0U)
        memcpy(ctx->iv, prev, AES_BLOCK_SIZE);
}
