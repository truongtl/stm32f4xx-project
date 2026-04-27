#pragma once

#include <stdint.h>

#define OTA_PACKET_MAGIC    0xAA55
#define OTA_RESPONSE_MAGIC  0x55AA
#define OTA_TRIGGER_BYTE    0x7F

// START packet payload layout (payload[1024]) — active:
//   [  0..  3] fw_crc32[4]  — CRC32 of the complete firmware binary
//   [  4..1023]             — reserved, must be 0xFF
//
// Future upgrade (software-only; STM32F411 has no hardware SHA/AES peripheral):
//   [  0.. 31] fw_sha256[32] — SHA-256 hash of firmware (define OTA_VERIFY_SHA256)
//   [ 32.. 63] fw_hmac[32]   — HMAC-SHA256(OTA_HMAC_KEY, firmware) (define OTA_VERIFY_HMAC)
//   [ 64.. 79] aes_iv[16]    — per-transfer AES-128-CBC IV (define OTA_DECRYPT_AES;
//                              omit if using static OTA_AES_IV from ota_auth.h)
//   [ 80..1023]              — reserved
//
// DATA packet payload: raw firmware chunk bytes (chunk_size valid bytes).
// END packet payload:  unused.
typedef struct {
    uint16_t magic;           // 0xAA55 - start marker
    uint8_t  packet_type;     // PACKET_TYPE_xxx
    uint8_t  seq_number;      // Sequence number (0-255)
    uint32_t total_chunks;    // Total number of data chunks
    uint32_t chunk_index;     // Index of current chunk
    uint32_t chunk_size;      // Payload size in this packet
    uint32_t total_fw_size;   // Total firmware size in bytes
    uint32_t fw_version;      // Firmware version
    uint8_t  payload[1024];   // Firmware data (or metadata in START/END)
    uint32_t crc32;           // CRC32 of entire packet (excluding this field)
} __attribute__((packed)) OTA_Packet_t;

// Packet types
#define PACKET_TYPE_START       0x01
#define PACKET_TYPE_DATA        0x02
#define PACKET_TYPE_END         0x03
#define PACKET_TYPE_ACK         0x10
#define PACKET_TYPE_NACK        0x11
#define PACKET_TYPE_ABORT       0x12

typedef struct {
    uint16_t magic;           // 0x55AA
    uint8_t  response_type;   // ACK/NACK/ABORT
    uint8_t  seq_number;      // Echo sequence number
    uint32_t chunk_index;     // Echo chunk index
    uint8_t  error_code;      // Error code if NACK
    uint32_t crc32;           // CRC32 of response (excluding this field)
} __attribute__((packed)) OTA_Response_t;

// Error codes
#define ERR_NONE                0x00
#define ERR_CRC_FAIL            0x01
#define ERR_FLASH_WRITE_FAIL    0x02
#define ERR_SEQUENCE_ERROR      0x03
#define ERR_TIMEOUT             0x04
#define ERR_FLASH_FULL          0x05
/* #define ERR_AUTH_FAIL        0x06 */ // HMAC authentication failure (enable with OTA_VERIFY_HMAC)