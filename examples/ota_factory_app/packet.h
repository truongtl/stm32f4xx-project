typedef struct {
    uint16_t magic;           // 0xAA55 - start marker
    uint8_t  packet_type;     // PACKET_TYPE_xxx
    uint8_t  seq_number;      // Sequence number (0-255)
    uint32_t total_chunks;    // Tổng số chunk
    uint32_t chunk_index;     // Index của chunk hiện tại
    uint32_t chunk_size;      // Kích thước payload
    uint32_t total_fw_size;   // Tổng kích thước firmware
    uint32_t fw_version;      // Version firmware
    uint8_t  payload[1024];   // Dữ liệu firmware
    uint32_t crc32;           // CRC32 của toàn bộ packet
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
    uint8_t  error_code;      // Mã lỗi nếu NACK
    uint32_t crc32;           // CRC32 của response
} __attribute__((packed)) OTA_Response_t;

// Error codes
#define ERR_NONE                0x00
#define ERR_CRC_FAIL            0x01
#define ERR_FLASH_WRITE_FAIL    0x02
#define ERR_SEQUENCE_ERROR      0x03
#define ERR_TIMEOUT             0x04
#define ERR_FLASH_FULL          0x05