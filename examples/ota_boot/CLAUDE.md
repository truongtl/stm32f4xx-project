# OTA Bootloader Example

A/B partition bootloader with OTA metadata, CRC32 verification, boot counting, and rollback support.

## What It Does

Reads OTA metadata from flash, decides which application partition to boot (factory App A or OTA-updated App B), verifies application integrity via CRC32 and vector table sanity checks, then jumps to the selected app. Supports rollback to factory app on repeated boot failures.

## Boot Flow

1. `HAL_Init()` → `SystemClock_Config()` → GPIO + USART1 init
2. `Metadata_Read(&meta)` — copy metadata from `0x08008000` into RAM
3. If `Metadata_IsValid(&meta)` fails → boot App A (factory) immediately
4. `Boot_Decide(&meta)` — select App A or App B:
   - `ota_in_progress` set → clear it, mark App B invalid, write metadata → App A
   - `force_factory_boot` set → App A
   - App B `APP_STATUS_VALID` → App B
   - App B `APP_STATUS_PENDING` and boot count ≥ `MAX_BOOT_ATTEMPTS` → mark invalid, set `force_factory_boot`, write metadata → App A
   - App B `APP_STATUS_PENDING` → increment boot count, set `app_b_first_boot`, write metadata → App B
   - Default (`INVALID`) → App A
5. If App B selected: `App_VerifyCRC(addr, size, crc32, max_size)`:
   - Check stack pointer in RAM range (`0x20000000`–`0x20020000`)
   - Check reset handler within flash range (`0x08000000`–`0x08080000`)
   - Verify CRC32 of App B flash content against `meta.app_b_crc32`
   - On failure → mark App B invalid, write metadata, fall back to App A
6. If App A selected:
   - If metadata is valid and `app_a_size > 0`: `App_VerifyCRC(addr, size, app_a_crc32, max_size)`
   - Otherwise (first boot / no metadata): `App_Verify(addr)` — vector table SP + reset handler checks only
   - On failure → `Boot_Error()` (blinks LED rapidly, never returns)
7. `JumpToApplication(app_addr)`

## Memory Layout (`mem_layout.h`)

| Region     | Address      | Size   | Sectors |
|------------|--------------|--------|---------|
| Bootloader | `0x08000000` | 32 KB  | 0–1     |
| Metadata   | `0x08008000` | 16 KB  | 2       |
| App A      | `0x0800C000` | 208 KB | 3–5     |
| App B      | `0x08040000` | 256 KB | 6–7     |

## OTA Metadata (`ota_metadata.h`)

`OTA_Metadata_t` struct stored at `METADATA_START_ADDR` (`0x08008000`):

| Field | Description |
|-------|-------------|
| `magic` | `0xDEADBEEF` — validity marker |
| `app_a_crc32` | CRC32 of App A firmware (used when `app_a_size > 0`) |
| `app_a_size` | App A firmware size in bytes |
| `app_b_status` | `INVALID` (0x00), `PENDING` (0x01), `VALID` (0x02) |
| `app_b_crc32` | Expected CRC32 of App B firmware |
| `app_b_size` | App B firmware size in bytes |
| `app_b_version` | App B version number |
| `app_b_first_boot` | Flag: set on first boot after OTA |
| `app_b_boot_count` | Boot attempt counter for rollback |
| `force_factory_boot` | Flag: force App A on next boot |
| `ota_in_progress` | Crash-safe marker: set before erase, cleared by factory app |
| `last_successful_boot` | `APP_FACTORY` or `APP_OTA` |
| `metadata_crc32` | Self-integrity check of this struct |

## Paired With

**`ota_factory_app`** — the factory application (App A) that receives OTA packets and writes App B.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | Boot decision logic, CRC verification, jump-to-app |
| `mem_layout.h` | Partition addresses |
| `ota_metadata.h` | Metadata struct and status constants |
| `STM32F411CEUX_FLASH.ld` | Bootloader linker script (first 32 KB) |

## Security Model

| Mechanism | Status | Notes |
|-----------|--------|-------|
| CRC32 firmware verify | **Active** | Detects accidental flash corruption |
| SHA-256 firmware hash | Disabled | `sha256.c` present; enable with `OTA_VERIFY_SHA256` |
| HMAC-SHA256 auth | Disabled | `sha256.c` + `ota_auth.h` present; enable with `OTA_VERIFY_HMAC` |
| AES-128-CBC channel encryption | N/A (factory app) | `aes.c` in `ota_factory_app`; bootloader reads plaintext from flash |

STM32F411 has no hardware hash or AES peripheral. SHA-256 and HMAC are pure
software (see `sha256.c`). AES-128-CBC decryption belongs to the factory app
(firmware is decrypted before writing to flash, so the bootloader sees
plaintext). For production, enable at minimum HMAC-SHA256 to prevent
unauthorised firmware. Stronger option: replace HMAC with ECDSA.
