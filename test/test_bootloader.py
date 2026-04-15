#!/usr/bin/env python3
"""
Automated test suite for OTA bootloader boot decision logic.
Covers TC-BOOT-01 through TC-BOOT-09 from OTA_TEST_PLAN.md.

Tests observe UART output on USART1 (115200 8N1, PA9/PA10).
Tests marked REQUIRES_PROBE write metadata directly to flash sector 2
(0x08008000) via st-flash and require an ST-Link connected to the board.

Run (serial-only tests):
    python tests/test_bootloader.py --port /dev/ttyUSB0

Run (including probe tests):
    python tests/test_bootloader.py --port /dev/ttyUSB0
    # (st-flash must be on PATH and ST-Link connected)

Dependencies:
    pip install pyserial
    Optional: stlink-tools (st-flash binary)
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

import serial

# Locate ota_host relative to this file so the module resolves regardless
# of the working directory.
sys.path.insert(0, str(Path(__file__).parent))
from ota_host import crc32

# ---------------------------------------------------------------------------
# Memory map constants — from examples/ota_boot/mem_layout.h
# ---------------------------------------------------------------------------

METADATA_START_ADDR = 0x08008000
METADATA_SECTOR_SIZE = 16 * 1024        # 16 KB, sector 2
APP_A_START_ADDR    = 0x0800C000

# ---------------------------------------------------------------------------
# OTA_Metadata_t constants — from examples/ota_boot/ota_metadata.h
# ---------------------------------------------------------------------------

METADATA_MAGIC      = 0xDEADBEEF
APP_STATUS_INVALID  = 0x00
APP_STATUS_PENDING  = 0x01
APP_STATUS_VALID    = 0x02
MAX_BOOT_ATTEMPTS   = 3
APP_FACTORY         = 0
APP_B_OTA           = 1

# ---------------------------------------------------------------------------
# Global test configuration set via CLI
# ---------------------------------------------------------------------------

TEST_PORT = '/dev/ttyUSB0'
TEST_BAUD = 115200


# ---------------------------------------------------------------------------
# Metadata helpers
# ---------------------------------------------------------------------------

def pack_metadata(
    magic=METADATA_MAGIC,
    app_a_size=0,
    app_a_crc32=0,
    app_a_version=0,
    app_b_size=0,
    app_b_crc32=0,
    app_b_version=0,
    app_b_status=APP_STATUS_INVALID,
    app_b_first_boot=0,
    app_b_boot_count=0,
    force_factory_boot=0,
    ota_in_progress=0,
    last_successful_boot=APP_FACTORY,
) -> bytes:
    """Serialise an OTA_Metadata_t into 44 bytes with a correct trailing CRC32.

    Layout matches the C struct with __attribute__((aligned(4))).  No
    compiler padding is inserted because the 5 uint8 fields + 3 reserved
    bytes are followed by a naturally-aligned uint32_t at offset 36:

        offset  0: magic                     (uint32)
        offset  4: app_a_size/crc32/version  (3 × uint32)
        offset 16: app_b_size/crc32/version  (3 × uint32)
        offset 28: app_b_status / first_boot / boot_count /
                   force_factory_boot / ota_in_progress / _reserved[3]
                                              (5 + 3 = 8 × uint8)
        offset 36: last_successful_boot      (uint32)
        offset 40: metadata_crc32            (uint32)  ← calculated here
    """
    body = struct.pack(
        '<IIIIIII',
        magic,
        app_a_size, app_a_crc32, app_a_version,
        app_b_size, app_b_crc32, app_b_version,
    )
    body += struct.pack(
        'BBBBBBBB',
        app_b_status,
        app_b_first_boot,
        app_b_boot_count,
        force_factory_boot,
        ota_in_progress,
        0, 0, 0,                    # _reserved[3]
    )
    body += struct.pack('<I', last_successful_boot)
    # CRC covers all bytes except the final metadata_crc32 field (40 bytes)
    body += struct.pack('<I', crc32(body))
    return body


def st_flash_available() -> bool:
    """Return True if the st-flash binary is found on PATH."""
    return shutil.which('st-flash') is not None


def write_metadata_via_probe(meta_bytes: bytes) -> None:
    """Write OTA metadata to flash sector 2 (0x08008000) using st-flash.

    Pads the payload to the full 16 KB sector size with 0xFF before
    writing so that any bytes beyond sizeof(OTA_Metadata_t) are in the
    erased state the firmware expects.

    Raises:
        RuntimeError: if st-flash is not on PATH or the write fails.
    """
    if not st_flash_available():
        raise RuntimeError(
            "st-flash not found on PATH. "
            "Install stlink-tools: https://github.com/stlink-org/stlink"
        )

    # Pad to exactly one metadata sector (16 KB)
    data = meta_bytes + b'\xff' * (METADATA_SECTOR_SIZE - len(meta_bytes))

    tmp = tempfile.NamedTemporaryFile(suffix='.bin', delete=False)
    try:
        tmp.write(data)
        tmp.close()
        result = subprocess.run(
            ['st-flash', 'write', tmp.name,
             f'0x{METADATA_START_ADDR:08X}'],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"st-flash write failed (rc={result.returncode}):\n"
                f"{result.stderr}"
            )
    finally:
        os.unlink(tmp.name)


def wait_for_boot_banner(timeout: float = 5.0) -> str:
    """Collect UART output for *timeout* seconds and return it as one string.

    Opens the serial port independently of any OTAHost instance so this
    function can be called right after write_metadata_via_probe() triggers
    a board reset.
    """
    collected = []
    deadline  = time.time() + timeout
    try:
        with serial.Serial(TEST_PORT, TEST_BAUD, timeout=0.3) as ser:
            while time.time() < deadline:
                line = ser.readline()
                if line:
                    collected.append(
                        line.decode('ascii', errors='replace').strip()
                    )
    except serial.SerialException:
        pass
    return '\n'.join(collected)


# ---------------------------------------------------------------------------
# TC-BOOT-01 / TC-BOOT-02: Serial-only tests (no probe required)
# ---------------------------------------------------------------------------

class TestBootBanner(unittest.TestCase):
    """TC-BOOT-01 — Bootloader banner appears on every boot."""

    def test_banner_present(self):
        """The bootloader must print its version banner within 5 s of reset."""
        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn(
            'OTA Bootloader',
            output,
            f"Banner not found in UART output:\n{output}",
        )


class TestInitialFactoryBoot(unittest.TestCase):
    """TC-BOOT-02 — Board with no valid metadata boots App A."""

    def test_boots_factory_app(self):
        """Without valid metadata the bootloader must select App A (factory)."""
        output = wait_for_boot_banner(timeout=6.0)
        self.assertIn(
            'Boot App A',
            output,
            f"Expected 'Boot App A' in output:\n{output}",
        )
        self.assertIn(
            'OTA Factory App',
            output,
            f"Factory app banner missing from output:\n{output}",
        )


# ---------------------------------------------------------------------------
# TC-BOOT-03 through TC-BOOT-09: Probe-required tests
# ---------------------------------------------------------------------------

class _ProbeTestBase(unittest.TestCase):
    """Base class that skips the test when st-flash is unavailable."""

    def setUp(self):
        if not st_flash_available():
            self.skipTest(
                "st-flash not found on PATH — skipping probe test. "
                "Install stlink-tools to enable these tests."
            )


class TestInvalidMetadataFallback(_ProbeTestBase):
    """TC-BOOT-03 — Invalid/erased metadata falls back to factory app."""

    def test_corrupted_metadata_boots_factory(self):
        """All-0xFF metadata sector (no valid magic) must select App A."""
        # Write 44 bytes of 0xFF — magic will not match METADATA_MAGIC
        write_metadata_via_probe(b'\xff' * 44)
        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('Metadata invalid', output,
                      f"Output:\n{output}")
        self.assertIn('Boot App A', output,
                      f"Output:\n{output}")


class TestForceFactoryBootFlag(_ProbeTestBase):
    """TC-BOOT-04 — force_factory_boot=1 overrides App B VALID status."""

    def test_force_factory_boot_respected(self):
        """Bootloader must boot factory regardless of app_b_status when flag is set."""
        meta = pack_metadata(force_factory_boot=1, app_b_status=APP_STATUS_VALID)
        write_metadata_via_probe(meta)
        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('Force factory boot', output,
                      f"Output:\n{output}")
        self.assertIn('Boot App A', output,
                      f"Output:\n{output}")


class TestOTAInProgressRecovery(_ProbeTestBase):
    """TC-BOOT-05 — ota_in_progress=1 is cleared and factory app is booted."""

    def test_interrupted_ota_cleared(self):
        """Power-loss mid-OTA leaves ota_in_progress=1; bootloader must recover."""
        meta = pack_metadata(
            ota_in_progress=1,
            app_b_status=APP_STATUS_PENDING,
        )
        write_metadata_via_probe(meta)
        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('OTA interrupted', output,
                      f"Output:\n{output}")
        self.assertIn('Boot App A', output,
                      f"Output:\n{output}")


class TestPendingFirstAttempt(_ProbeTestBase):
    """TC-BOOT-07 — First PENDING boot attempt increments counter to 1."""

    def test_first_attempt_logged(self):
        """boot_count=0 + PENDING must produce 'attempt 1' in boot output."""
        meta = pack_metadata(
            app_b_status=APP_STATUS_PENDING,
            app_b_boot_count=0,
        )
        write_metadata_via_probe(meta)
        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('Trying App B', output,
                      f"Output:\n{output}")
        self.assertIn('attempt 1/', output,
                      f"Output:\n{output}")


class TestPendingMaxAttemptsRollback(_ProbeTestBase):
    """TC-BOOT-08 — Reaching MAX_BOOT_ATTEMPTS triggers rollback to factory."""

    def test_max_attempts_rolls_back(self):
        """boot_count == MAX_BOOT_ATTEMPTS must invalidate App B and boot factory."""
        meta = pack_metadata(
            app_b_status=APP_STATUS_PENDING,
            app_b_boot_count=MAX_BOOT_ATTEMPTS,
        )
        write_metadata_via_probe(meta)
        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('exceeded max attempts', output,
                      f"Output:\n{output}")
        self.assertIn('Boot App A', output,
                      f"Output:\n{output}")


class TestForceFactoryPersistsAfterRollback(_ProbeTestBase):
    """TC-BOOT-09 — force_factory_boot is written during rollback and persists."""

    def test_second_boot_uses_force_factory(self):
        """After rollback, the next cold boot must also see force_factory_boot=1."""
        # Trigger the rollback (same as TC-BOOT-08)
        meta = pack_metadata(
            app_b_status=APP_STATUS_PENDING,
            app_b_boot_count=MAX_BOOT_ATTEMPTS,
        )
        write_metadata_via_probe(meta)
        # First boot — rollback happens and writes force_factory_boot=1
        wait_for_boot_banner(timeout=5.0)

        # Use DTR toggle to trigger a second reset if the adapter supports
        # it, otherwise the test relies on a manual reset within 5 s
        try:
            with serial.Serial(TEST_PORT, TEST_BAUD) as ser:
                ser.dtr = False
                time.sleep(0.1)
                ser.dtr = True
        except serial.SerialException:
            pass

        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('Force factory boot', output,
                      f"Rollback should persist across reboots.\nOutput:\n{output}")
        self.assertIn('Boot App A', output,
                      f"Output:\n{output}")


# ---------------------------------------------------------------------------
# CLI + unittest runner
# ---------------------------------------------------------------------------

def _parse_args():
    parser = argparse.ArgumentParser(
        description="OTA bootloader boot-decision tests.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument('--port', default='/dev/ttyUSB0',
                        help="Serial port connected to USART1")
    parser.add_argument('--baud', type=int, default=115200,
                        help="Baud rate")
    return parser.parse_known_args()


if __name__ == '__main__':
    args, remaining = _parse_args()
    TEST_PORT = args.port
    TEST_BAUD = args.baud
    unittest.main(argv=[sys.argv[0]] + remaining, verbosity=2)
