#!/usr/bin/env python3
"""
Automated test suite for OTA and bootloader error recovery scenarios.
Covers TC-RECOV-01 through TC-RECOV-08 from OTA_TEST_PLAN.md.

Test groups:
  TestOTAInterruptRecovery — partial OTA transfers abandoned mid-flight
  TestBootAttemptRollback  — PENDING App B boot-count exhaustion (requires st-flash)
  TestAppBCRCFailover      — wrong stored CRC32 causes App B rejection (requires st-flash)
  TestChunkRetryBehavior   — DATA-level CRC retry within one OTA session

Run:
    python tests/test_recovery.py --port /dev/ttyUSB0

Dependencies:
    pip install pyserial
    Optional: stlink-tools (st-flash) for probe tests
"""

import argparse
import struct
import sys
import time
import unittest
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).parent))
from ota_host import (
    OTAHost,
    OTAError,
    build_packet,
    crc32,
    DEFAULT_CHUNK_SIZE,
    ERR_CRC_FAIL,
    PACKET_TYPE_ACK,
    PACKET_TYPE_DATA,
    PACKET_TYPE_NACK,
    PACKET_TYPE_START,
)
from test_bootloader import (
    APP_B_OTA,
    APP_FACTORY,
    APP_STATUS_INVALID,
    APP_STATUS_PENDING,
    APP_STATUS_VALID,
    MAX_BOOT_ATTEMPTS,
    TEST_BAUD,
    TEST_PORT,
    pack_metadata,
    st_flash_available,
    wait_for_boot_banner,
    write_metadata_via_probe,
)

# ---------------------------------------------------------------------------
# Global test configuration set via CLI (overrides test_bootloader defaults)
# ---------------------------------------------------------------------------

# TEST_PORT and TEST_BAUD are imported from test_bootloader; reassigned below
# when __main__ parses CLI arguments.


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_minimal_valid_fw(size: int = DEFAULT_CHUNK_SIZE) -> bytes:
    """Return firmware with a valid Cortex-M4 vector table of *size* bytes.

    SP  = 0x20004000 — inside SRAM (0x20000000–0x20020000).
    RST = 0x08040009 — App B base (0x08040000) + 9, Thumb bit set,
                       inside flash range (0x08000000–0x08080000).
    Rest of the image is zero-filled.
    """
    fw = bytearray(size)
    struct.pack_into('<I', fw, 0, 0x20004000)
    struct.pack_into('<I', fw, 4, 0x08040009)
    return bytes(fw)


def _reset_via_dtr(port: str, baud: int) -> None:
    """Toggle DTR to attempt a hardware reset (nRST on some adapters)."""
    try:
        with serial.Serial(port, baud) as ser:
            ser.dtr = False
            time.sleep(0.15)
            ser.dtr = True
    except serial.SerialException:
        pass


# ---------------------------------------------------------------------------
# TC-RECOV-01 / TC-RECOV-02: Interrupted OTA recovery (serial only)
# ---------------------------------------------------------------------------

class TestOTAInterruptRecovery(unittest.TestCase):
    """Tests for incomplete OTA transfers that leave ota_in_progress=1 in flash.

    These tests trigger OTA mode, send a partial sequence, then drop the
    connection.  After a board reset (via DTR toggle) the bootloader must
    clear ota_in_progress and boot the factory app.
    """

    def _enter_ota_mode(self, host: OTAHost) -> None:
        output = host.trigger_ota(wait=1.5)
        if 'OTA mode' not in output and 'START' not in output:
            self.skipTest(
                "Factory app did not enter OTA mode — "
                "verify ota_factory_app is running at 0x0800C000"
            )

    def test_start_only_sets_ota_in_progress(self):
        """TC-RECOV-01: Abandoning after START ACK leaves ota_in_progress=1.

        On the next boot the bootloader must detect the interrupted flag,
        clear it, mark App B invalid, and boot the factory app.
        """
        with OTAHost(TEST_PORT, TEST_BAUD) as host:
            self._enter_ota_mode(host)
            fw   = make_minimal_valid_fw()
            resp = host.send_start_packet(fw, fw_version=1)
            self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                             "START must be ACKed to confirm ota_in_progress was written")
            # Drop connection — simulates power loss before DATA packets arrive

        # Give the MCU time to register the UART idle before resetting
        time.sleep(0.5)
        _reset_via_dtr(TEST_PORT, TEST_BAUD)

        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('OTA interrupted', output,
                      f"Bootloader should detect interrupted OTA.\nOutput:\n{output}")
        self.assertIn('Boot App A', output,
                      f"Bootloader should fall back to factory.\nOutput:\n{output}")

    def test_partial_data_sets_ota_in_progress(self):
        """TC-RECOV-02: Abandoning after sending some DATA chunks must also recover.

        Ensures ota_in_progress=1 written before the first erase is still
        present after a mid-transfer abort.
        """
        fw           = make_minimal_valid_fw(DEFAULT_CHUNK_SIZE * 3)
        total_chunks = 3

        with OTAHost(TEST_PORT, TEST_BAUD) as host:
            self._enter_ota_mode(host)
            resp = host.send_start_packet(fw, fw_version=1,
                                          chunk_size=DEFAULT_CHUNK_SIZE)
            self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                             "START must be ACKed")

            # Send the first chunk only then drop the connection
            chunk = fw[:DEFAULT_CHUNK_SIZE]
            resp  = host.send_data_packet(
                chunk, 0, total_chunks, len(fw), 1, seq=1
            )
            self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                             "First DATA chunk must be ACKed")

        time.sleep(0.5)
        _reset_via_dtr(TEST_PORT, TEST_BAUD)

        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('OTA interrupted', output,
                      f"Bootloader should detect interrupted OTA.\nOutput:\n{output}")
        self.assertIn('Boot App A', output,
                      f"Bootloader should fall back to factory.\nOutput:\n{output}")


# ---------------------------------------------------------------------------
# TC-RECOV-04 – TC-RECOV-07: Boot-attempt rollback (requires st-flash)
# ---------------------------------------------------------------------------

class _ProbeTestBase(unittest.TestCase):
    """Base class: skip when st-flash is not available."""

    def setUp(self):
        if not st_flash_available():
            self.skipTest(
                "st-flash not found on PATH — skipping probe test. "
                "Install stlink-tools to enable these tests."
            )


class TestBootAttemptRollback(_ProbeTestBase):
    """PENDING App B boot-count tracking and rollback (TC-RECOV-04 to TC-RECOV-07)."""

    def test_first_pending_attempt_shows_count_1(self):
        """TC-RECOV-04: boot_count=0 + PENDING must log 'attempt 1' and attempt App B."""
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

    def test_second_pending_attempt_shows_count_2(self):
        """TC-RECOV-05: boot_count=1 + PENDING must log 'attempt 2'."""
        meta = pack_metadata(
            app_b_status=APP_STATUS_PENDING,
            app_b_boot_count=1,
        )
        write_metadata_via_probe(meta)
        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('attempt 2/', output,
                      f"Output:\n{output}")

    def test_max_attempts_triggers_rollback(self):
        """TC-RECOV-06: boot_count == MAX_BOOT_ATTEMPTS must roll back to factory."""
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

    def test_force_factory_persists_after_rollback(self):
        """TC-RECOV-07: force_factory_boot written during rollback persists on next boot.

        After the first boot that triggers rollback, the bootloader writes
        force_factory_boot=1 to metadata.  A second reset must take the
        'Force factory boot' path — not attempt App B again.
        """
        meta = pack_metadata(
            app_b_status=APP_STATUS_PENDING,
            app_b_boot_count=MAX_BOOT_ATTEMPTS,
        )
        write_metadata_via_probe(meta)

        # First boot triggers rollback and writes force_factory_boot=1
        first_output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('exceeded max attempts', first_output,
                      "First boot should trigger rollback")

        # Second reset — board should now take the force_factory_boot branch
        _reset_via_dtr(TEST_PORT, TEST_BAUD)
        second_output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('Force factory boot', second_output,
                      f"force_factory_boot should persist after rollback.\n"
                      f"Second boot output:\n{second_output}")
        self.assertIn('Boot App A', second_output,
                      f"Second boot output:\n{second_output}")


# ---------------------------------------------------------------------------
# TC-RECOV-08: App B CRC mismatch in metadata (requires st-flash)
# ---------------------------------------------------------------------------

class TestAppBCRCFailover(_ProbeTestBase):
    """TC-RECOV-08 — Wrong app_b_crc32 in metadata must cause App_VerifyCRC failure."""

    def test_bad_stored_crc_falls_back_to_factory(self):
        """Metadata with app_b_status=VALID + wrong CRC must fall back to App A.

        The bootloader calls App_VerifyCRC() which computes the CRC of the
        flash region and compares it to the stored app_b_crc32.  A mismatch
        must produce a 'CRC mismatch' log entry and select App A instead.
        """
        meta = pack_metadata(
            app_b_status=APP_STATUS_VALID,
            app_b_size=4096,           # plausible non-zero size
            app_b_crc32=0xDEADBEEF,    # deliberately wrong value
        )
        write_metadata_via_probe(meta)
        output = wait_for_boot_banner(timeout=5.0)
        self.assertIn('CRC mismatch', output,
                      f"Bootloader should report a CRC mismatch.\nOutput:\n{output}")
        self.assertIn('Boot App A', output,
                      f"Bootloader should fall back to factory after CRC failure.\n"
                      f"Output:\n{output}")


# ---------------------------------------------------------------------------
# TC-RECOV-03: DATA-level CRC retry within a single OTA session
# ---------------------------------------------------------------------------

class TestChunkRetryBehavior(unittest.TestCase):
    """TC-RECOV-03 — Firmware retries a bad DATA chunk; correct resend succeeds."""

    def setUp(self):
        self.host = OTAHost(TEST_PORT, TEST_BAUD)
        self.host.connect()
        output = self.host.trigger_ota(wait=1.5)
        if 'OTA mode' not in output and 'START' not in output:
            self.host.disconnect()
            self.skipTest("Factory app did not enter OTA mode")

    def tearDown(self):
        self.host.disconnect()

    def test_retry_after_crc_fail_succeeds(self):
        """Sending a corrupt DATA chunk (NACK) then resending it correctly must ACK.

        The firmware retries up to OTA_CHUNK_RETRIES (3) times per chunk.
        This test exercises the first retry path: bad → NACK, then good → ACK.
        """
        fw           = make_minimal_valid_fw(DEFAULT_CHUNK_SIZE)
        total_chunks = 1

        resp = self.host.send_start_packet(fw, fw_version=1,
                                           chunk_size=DEFAULT_CHUNK_SIZE)
        self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                         "START must be ACKed before retry test")

        chunk = fw[:DEFAULT_CHUNK_SIZE]

        # --- Send a corrupt DATA packet (flip a CRC byte) ---
        bad_pkt = bytearray(build_packet(
            PACKET_TYPE_DATA, 1, total_chunks, 0,
            len(chunk), len(fw), 1, chunk,
        ))
        bad_pkt[-1] ^= 0xFF
        self.host._send(bytes(bad_pkt))
        nack = self.host._read_response(timeout=5.0)
        self.assertEqual(nack['response_type'], PACKET_TYPE_NACK,
                         "Corrupt DATA packet must produce NACK")
        self.assertEqual(nack['error_code'], ERR_CRC_FAIL,
                         "Error code must be ERR_CRC_FAIL (0x01)")

        # --- Resend the same chunk correctly ---
        resp = self.host.send_data_packet(chunk, 0, total_chunks, len(fw), 1, seq=2)
        self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                         "Correct resend should succeed within the retry window")


# ---------------------------------------------------------------------------
# CLI + unittest runner
# ---------------------------------------------------------------------------

def _parse_args():
    parser = argparse.ArgumentParser(
        description="OTA error-recovery tests.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument('--port', default='/dev/ttyUSB0',
                        help="Serial port connected to USART1")
    parser.add_argument('--baud', type=int, default=115200,
                        help="Baud rate")
    return parser.parse_known_args()


if __name__ == '__main__':
    import test_bootloader   # allow mutation of its module-level globals

    args, remaining = _parse_args()
    # Propagate CLI port/baud to both this module and the imported helpers
    TEST_PORT = args.port
    TEST_BAUD = args.baud
    test_bootloader.TEST_PORT = args.port
    test_bootloader.TEST_BAUD = args.baud

    unittest.main(argv=[sys.argv[0]] + remaining, verbosity=2)
