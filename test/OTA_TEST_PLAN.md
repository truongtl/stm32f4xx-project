# OTA Boot & Factory App — Test Plan

Covers `examples/ota_boot` (bootloader at `0x08000000`) and
`examples/ota_factory_app` (App A at `0x0800C000`).

---

## Hardware & Software Requirements

| Item | Purpose |
|------|---------|
| STM32F411CEU6 board (WeAct BlackPill) | Target MCU |
| USB-to-UART adapter (CP2102 / CH340) | UART access on PA9/PA10 at 115200 8N1 |
| ST-Link v2 / v3 | Flash programming and hardware reset |
| `arm-none-eabi-gcc` toolchain | Building firmware images |
| Python ≥ 3.10 + `pyserial` | OTA host tool and test scripts |
| `stlink-tools` (`st-flash`) | Direct flash writes and board reset in probe tests |

Install Python dependencies:
```bash
pip install pyserial
```

---

## Memory Map Reference

| Region     | Address      | Size   | Sectors |
|------------|--------------|--------|---------|
| Bootloader | `0x08000000` | 32 KB  | 0–1     |
| Metadata   | `0x08008000` | 16 KB  | 2       |
| App A      | `0x0800C000` | 208 KB | 3–5     |
| App B      | `0x08040000` | 256 KB | 6–7     |

---

## Initial Flash Setup

Perform once before running any tests:

```bash
# 1. Build both examples
make ota_boot
make ota_factory_app

# 2. Flash the bootloader
st-flash write build/ota_boot/out/ota_boot.bin 0x08000000

# 3. Flash the factory app
st-flash write build/ota_factory_app/out/ota_factory_app.bin 0x0800C000
```

After flashing, connect a terminal (115200 8N1) to USART1 (PA9/PA10) and confirm:
```
--- OTA Bootloader v1.0 ---
Verifying App A...
-> Boot App A (Factory)
--- OTA Factory App v1.0 ---
```

---

## Test Files

| File | What it tests |
|------|---------------|
| `tests/ota_host.py` | Core OTA packet library (also a CLI tool) |
| `tests/test_bootloader.py` | Boot decision logic, metadata validation, rollback |
| `tests/test_ota_protocol.py` | OTA packet protocol compliance over UART |
| `tests/test_recovery.py` | Interrupted OTA, CRC failures, retry behaviour |

### Running a test suite

```bash
# Run all tests on /dev/ttyUSB0
python tests/test_ota_protocol.py --port /dev/ttyUSB0

# Run with st-flash (probe) tests enabled
python tests/test_bootloader.py  --port /dev/ttyUSB0
python tests/test_recovery.py    --port /dev/ttyUSB0

# Send a real firmware update manually
python tests/ota_host.py \
    --port /dev/ttyUSB0 \
    --firmware build/ota_factory_app/out/ota_factory_app.bin \
    --version 2
```

---

## Phase 1 — Bootloader Boot Decision Logic

Tests in: `test_bootloader.py`

### TC-BOOT-01: Banner on every boot
- **Objective**: Bootloader must print version banner on every power-on.
- **Method**: Serial only; read UART for 5 s after reset.
- **Pass**: `"--- OTA Bootloader v1.0 ---"` present in output.

### TC-BOOT-02: First boot with no metadata → factory
- **Objective**: Flash with no valid metadata must select App A.
- **Method**: Serial only (fresh board or erased metadata sector).
- **Pass**: `"Boot App A"` and `"--- OTA Factory App v1.0 ---"` present.

### TC-BOOT-03: Invalid/corrupted metadata → factory  *(requires st-flash)*
- **Objective**: Corrupted metadata sector (all `0xFF`) must fall back to App A.
- **Method**: Write 16 KB of `0xFF` to `0x08008000`, reset, read UART.
- **Pass**: `"Metadata invalid"` + `"Boot App A"` in output.

### TC-BOOT-04: `force_factory_boot` flag set → factory  *(requires st-flash)*
- **Objective**: When `force_factory_boot=1` in metadata, must ignore App B status.
- **Method**: Write metadata with valid CRC + `force_factory_boot=1`, `app_b_status=VALID`.
- **Pass**: `"Force factory boot"` + `"Boot App A"` in output.

### TC-BOOT-05: `ota_in_progress` flag set → clear + factory  *(requires st-flash)*
- **Objective**: Power loss during OTA leaves `ota_in_progress=1`; bootloader must recover.
- **Method**: Write metadata with `ota_in_progress=1`, reset, read UART.
- **Pass**: `"OTA interrupted, clearing"` + `"Boot App A"` in output.

### TC-BOOT-06: App B `VALID` status → boots App B  *(requires st-flash + valid App B binary)*
- **Objective**: Valid App B with matching CRC in metadata must be selected.
- **Method**: Write valid App B binary at `0x08040000`, write matching metadata.
- **Pass**: `"-> Boot App B (OTA)"` in output.

### TC-BOOT-07: App B `PENDING`, first attempt → boots App B, increments counter  *(requires st-flash)*
- **Objective**: First pending boot attempt increments `app_b_boot_count` to 1.
- **Method**: Write metadata with `app_b_status=PENDING`, `app_b_boot_count=0`.
- **Pass**: `"Trying App B (attempt 1/3)"` in output.

### TC-BOOT-08: App B `PENDING` at max attempts → rollback  *(requires st-flash)*
- **Objective**: After `MAX_BOOT_ATTEMPTS` (3) pending attempts, roll back to factory.
- **Method**: Write metadata with `app_b_status=PENDING`, `app_b_boot_count=3`.
- **Pass**: `"exceeded max attempts"` + `"Boot App A"` in output.

### TC-BOOT-09: Post-rollback `force_factory_boot` persists  *(requires st-flash)*
- **Objective**: After rollback writes `force_factory_boot=1`, next cold boot must also use factory.
- **Method**: Run TC-BOOT-08, then reset again without modifying metadata.
- **Pass**: `"Force factory boot"` on second reboot.

---

## Phase 2 — OTA Protocol Compliance

Tests in: `test_ota_protocol.py`

### TC-PROTO-01: OTA trigger byte activates OTA mode
- **Objective**: Sending `0x7F` must cause factory app to enter `OTA_Receive()`.
- **Method**: Send `0x7F` via serial, read response.
- **Pass**: `"OTA mode triggered"` + `"Waiting for START packet..."` in UART output.

### TC-PROTO-02: Valid START packet → ACK
- **Objective**: Well-formed START packet must receive `PACKET_TYPE_ACK`.
- **Method**: Send valid START with correct magic, CRC, non-zero firmware size.
- **Pass**: Response `response_type == 0x10` (ACK), `error_code == 0`.

### TC-PROTO-03: START with wrong magic → NACK
- **Objective**: START packet with magic ≠ `0xAA55` must be rejected.
- **Method**: Build packet, overwrite magic with `0xBBAA`, send.
- **Pass**: Response `response_type == 0x11` (NACK).

### TC-PROTO-04: START with corrupt CRC → NACK (`ERR_CRC_FAIL`)
- **Objective**: Corrupted packet CRC field must be rejected.
- **Method**: Flip one bit in the packet CRC byte.
- **Pass**: NACK with `error_code == 0x01` (`ERR_CRC_FAIL`).

### TC-PROTO-05: START with `total_fw_size` > `APP_B_MAX_SIZE` → NACK (`ERR_FLASH_FULL`)
- **Objective**: Oversized firmware must be rejected before erasing.
- **Method**: Set `total_fw_size = (256 * 1024) + 1` in START packet.
- **Pass**: NACK with `error_code == 0x05` (`ERR_FLASH_FULL`).

### TC-PROTO-06: START with `total_fw_size = 0` → NACK
- **Objective**: Zero-size firmware must be rejected.
- **Pass**: NACK response.

### TC-PROTO-07: DATA chunks ACKed in sequence
- **Objective**: Each well-formed DATA packet must be ACKed with correct `chunk_index`.
- **Method**: 2-chunk transfer; verify ACK for each chunk.
- **Pass**: Both responses ACK with matching `chunk_index`.

### TC-PROTO-08: DATA with wrong `chunk_index` → NACK (`ERR_SEQUENCE_ERROR`)
- **Objective**: Out-of-order chunk must be rejected.
- **Method**: After START ACK, send `chunk_index=1` when firmware expects `0`.
- **Pass**: NACK with `error_code == 0x03`.

### TC-PROTO-09: DATA with corrupt CRC → NACK (`ERR_CRC_FAIL`)
- **Objective**: DATA with flipped CRC must be rejected.
- **Pass**: NACK with `error_code == 0x01`.

### TC-PROTO-10: Wrong packet type instead of DATA → NACK
- **Objective**: Sending `PACKET_TYPE_END` (0x03) when DATA is expected must fail.
- **Pass**: NACK response.

### TC-PROTO-11: Full transfer (START + DATA + END) → ACK + reboot
- **Objective**: A complete valid transfer must ACK the END packet and trigger reboot.
- **Method**: Send valid 1-chunk firmware image through all three stages.
- **Pass**: `PACKET_TYPE_ACK` on END; board reboots (UART disconnects briefly).

### TC-PROTO-12: END packet with wrong expected firmware CRC → NACK
- **Objective**: If the expected CRC stored at START does not match actual written bytes, END must be NACKed.
- **Method**: Send START with a deliberately wrong `expected_fw_crc` in payload; send correct DATA; send END.
- **Pass**: NACK with `error_code == 0x01` (`ERR_CRC_FAIL`) on END response.

---

## Phase 3 — Error Recovery

Tests in: `test_recovery.py`

### TC-RECOV-01: Partial OTA (START only, then abandon) → `ota_in_progress` in flash
- **Objective**: After START ACK, dropping the connection must leave `ota_in_progress=1`.
- **Method**: Send START, close serial; reconnect after reset.
- **Pass**: `"OTA interrupted, clearing"` on next boot; `"Boot App A"` follows.

### TC-RECOV-02: Partial OTA (some DATA sent, then abandon) → same recovery
- **Objective**: Mid-transfer abandonment must also be recoverable.
- **Method**: Send START + 1 DATA chunk; close serial; reset board.
- **Pass**: `"OTA interrupted"` + `"Boot App A"`.

### TC-RECOV-03: DATA CRC failure → NACK, then correct resend → ACK
- **Objective**: Firmware retries bad chunks up to `OTA_CHUNK_RETRIES` (3) times.
- **Method**: Send corrupt DATA (flip CRC byte) → verify NACK; resend correct DATA → verify ACK.
- **Pass**: NACK on corrupt packet; ACK on correct resend.

### TC-RECOV-04: First PENDING boot attempt  *(requires st-flash)*
- **Objective**: Metadata with `PENDING` + `boot_count=0` → bootloader attempts App B + writes `boot_count=1`.
- **Pass**: `"attempt 1/3"` in UART output.

### TC-RECOV-05: Second PENDING boot attempt  *(requires st-flash)*
- **Objective**: Metadata with `PENDING` + `boot_count=1` → `"attempt 2/3"`.
- **Pass**: `"attempt 2/3"` in output.

### TC-RECOV-06: Rollback at max attempts  *(requires st-flash)*
- **Objective**: Metadata with `PENDING` + `boot_count=3` triggers rollback.
- **Pass**: `"exceeded max attempts"` + `"Boot App A"`.

### TC-RECOV-07: `force_factory_boot` persists after rollback  *(requires st-flash)*
- **Objective**: After rollback, subsequent boots must see `force_factory_boot=1` and stay on factory.
- **Method**: Run TC-RECOV-06, then reset again.
- **Pass**: `"Force factory boot"` on second boot.

### TC-RECOV-08: App B CRC mismatch in metadata → fallback  *(requires st-flash)*
- **Objective**: When metadata stores a wrong `app_b_crc32`, `App_VerifyCRC()` must fail.
- **Method**: Write metadata with `app_b_status=VALID`, `app_b_size=4096`, `app_b_crc32=0xDEADBEEF`.
- **Pass**: `"CRC mismatch"` + `"fallback to factory"` + `"Boot App A"`.

---

## Phase 4 — Edge Cases

These are manual tests.

### TC-EDGE-01: Maximum firmware size (256 KB)
Send a firmware image exactly at `APP_B_MAX_SIZE` (262144 bytes).
- **Pass**: OTA completes, board boots App B.

### TC-EDGE-02: Single-byte firmware (invalid vector table)
Send a 1-byte firmware via OTA. After reboot, bootloader's `App_Verify()` must fail CRC/SP check.
- **Pass**: `"Invalid SP"` or `"CRC mismatch"` logged; falls back to App A.

### TC-EDGE-03: Repeated OTA cycles
Perform 5 consecutive OTA updates (alternating version numbers). Verify each cycle completes cleanly.
- **Pass**: Each OTA ACKs END, board reboots, boots new version.

### TC-EDGE-04: Serial noise before OTA trigger
Send random bytes on UART before `0x7F`.
- **Pass**: Factory app ignores non-trigger bytes; LED continues blinking; OTA still triggers on `0x7F`.

---

## Pass/Fail Criteria Summary

| Category | # Tests | Probe Required |
|----------|---------|----------------|
| Boot decision | 9 | 7 |
| Protocol compliance | 12 | 0 |
| Error recovery | 8 | 5 |
| Edge cases | 4 | 0 (manual) |
| **Total** | **33** | **12** |

All automated tests must pass. Manual edge-case tests should be run before releases. Probe tests should be run on full CI rigs with an attached ST-Link.
