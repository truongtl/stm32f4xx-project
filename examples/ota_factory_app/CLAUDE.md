# OTA Factory App Example

Factory application (App A) for the OTA system. Runs from `0x0800C000`, blinks LED, and implements the full OTA firmware update protocol over UART.

## What It Does

Sets `SCB->VTOR` to `APP_A_START_ADDR`, calls `Boot_Confirm()` to finalise any prior OTA state, then enters the main loop. The main loop blinks the LED every 500 ms and listens for the OTA trigger byte (`0x7F`). When triggered, `OTA_Receive()` erases App B, downloads firmware in chunks over UART, verifies CRC, updates metadata to `APP_STATUS_PENDING`, and reboots. On the next boot the OTA bootloader boots App B.

## Boot Flow

1. `HAL_Init()`
2. `SCB->VTOR = APP_A_START_ADDR` — vector table at `0x0800C000`
3. `__enable_irq()` — re-enable interrupts
4. `SystemClock_Config()` → GPIO + USART1 init
5. Print "--- OTA Factory App v1.0 ---"
6. `Boot_Confirm()` — reads metadata; clears `ota_in_progress` if set, records `last_successful_boot = APP_FACTORY`
7. `HAL_UART_Receive_IT()` — start interrupt-driven RX
8. Main loop: toggle PC13 LED every 500 ms; on `OTA_TRIGGER_BYTE` (`0x7F`) → call `OTA_Receive()`

## OTA Receive Flow (`OTA_Receive()`)

1. Wait for `START` packet (30 s timeout) — validates magic + CRC; reads `total_chunks`, `total_fw_size`, `fw_version`, expected firmware CRC32 from payload
2. Set `ota_in_progress = 1` in metadata and write to flash (crash-safe marker)
3. Erase App B flash region (sectors 6–7)
4. ACK the `START` packet
5. For each chunk: receive `DATA` packet → validate magic/sequence/CRC → write `chunk_size` bytes to App B flash with read-back verification → ACK; retry up to `OTA_CHUNK_RETRIES` (3) on error
6. Receive `END` packet; verify complete firmware CRC32 against expected value from `START`
7. Update metadata: `app_b_size`, `app_b_crc32`, `app_b_version`, `app_b_status = APP_STATUS_PENDING`, `ota_in_progress = 0`
8. ACK `END`, delay 100 ms, `NVIC_SystemReset()`

## Packet Protocol (`packet.h`)

**Host → Device (`OTA_Packet_t`)**:

| Field | Description |
|-------|-------------|
| `magic` | `0xAA55` |
| `packet_type` | `START` (0x01), `DATA` (0x02), `END` (0x03) |
| `seq_number` | Packet sequence number |
| `total_chunks` | Total DATA chunks in this transfer |
| `chunk_index` | Index of this chunk |
| `chunk_size` | Payload bytes used in this packet |
| `total_fw_size` | Total firmware binary size |
| `fw_version` | Firmware version number |
| `payload[1024]` | Firmware data |
| `crc32` | Packet integrity checksum |

**Device → Host (`OTA_Response_t`)**:

| Field | Description |
|-------|-------------|
| `magic` | `0x55AA` |
| `response_type` | `ACK` (0x10), `NACK` (0x11), `ABORT` (0x12) |
| `seq_number` | Echoed sequence number |
| `chunk_index` | Echoed chunk index |
| `error_code` | `NONE`, `CRC_FAIL`, `FLASH_WRITE_FAIL`, `SEQUENCE_ERROR`, `TIMEOUT`, `FLASH_FULL` |
| `crc32` | Response integrity checksum |

## Memory Layout (`mem_layout.h`)

Same as `ota_boot`:

| Region     | Address      | Size   | Sectors |
|------------|--------------|--------|---------|
| Bootloader | `0x08000000` | 32 KB  | 0–1     |
| Metadata   | `0x08008000` | 16 KB  | 2       |
| App A      | `0x0800C000` | 208 KB | 3–5     |
| App B      | `0x08040000` | 256 KB | 6–7     |

## Paired With

**`ota_boot`** — the bootloader that decides whether to run this factory app or the OTA-updated App B.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | App entry with VTOR relocation, OTA receive logic, `Boot_Confirm()` |
| `mem_layout.h` | Partition addresses (shared with ota_boot) |
| `ota_metadata.h` | Metadata struct and status constants (shared with ota_boot) |
| `packet.h` | OTA packet and response structures |
| `STM32F411CEUX_FLASH.ld` | Linker script placing app at `0x0800C000` |
