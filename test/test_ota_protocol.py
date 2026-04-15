#!/usr/bin/env python3
"""
Automated test suite for OTA UART protocol compliance.
Covers TC-PROTO-01 through TC-PROTO-12 from OTA_TEST_PLAN.md.

Every test requires the STM32 board to be running ota_factory_app at
0x0800C000 with USART1 connected via a USB serial adapter.  Each test
setUp() sends the OTA trigger byte (0x7F) to put the factory app into
OTA_Receive() mode before sending protocol packets.

Run:
    python tests/test_ota_protocol.py --port /dev/ttyUSB0

Dependencies:
    pip install pyserial
"""

import argparse
import struct
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ota_host import (
    OTAHost,
    OTAError,
    build_packet,
    crc32,
    DEFAULT_CHUNK_SIZE,
    ERR_CRC_FAIL,
    ERR_FLASH_FULL,
    ERR_SEQUENCE_ERROR,
    OTA_PACKET_MAGIC,
    OTA_TRIGGER_BYTE,
    PACKET_TYPE_ACK,
    PACKET_TYPE_DATA,
    PACKET_TYPE_END,
    PACKET_TYPE_NACK,
    PACKET_TYPE_START,
)

# ---------------------------------------------------------------------------
# Global test configuration set via CLI
# ---------------------------------------------------------------------------

TEST_PORT = '/dev/ttyUSB0'
TEST_BAUD = 115200

# App B limit from mem_layout.h (256 KB)
APP_B_MAX_SIZE = 256 * 1024


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_minimal_valid_fw(size: int = DEFAULT_CHUNK_SIZE) -> bytes:
    """Return a firmware image of *size* bytes with a valid Cortex-M4 vector table.

    The stack pointer is set to 0x20004000 (within SRAM 0x20000000–0x20020000)
    and the reset handler to 0x08040009 (App B base + 9, Thumb bit set, within
    the accepted flash range 0x08000000–0x08080000).  The rest of the image is
    zero-filled, which is valid for exercising the OTA receive and flash-write
    path without actually running the image.
    """
    fw = bytearray(size)
    struct.pack_into('<I', fw, 0, 0x20004000)   # valid SP in SRAM
    struct.pack_into('<I', fw, 4, 0x08040009)   # reset handler in App B, Thumb
    return bytes(fw)


# ---------------------------------------------------------------------------
# Base class: enters OTA mode before each test
# ---------------------------------------------------------------------------

class OTAProtocolTestBase(unittest.TestCase):
    """Open serial, send OTA trigger, skip test if OTA mode is not confirmed."""

    # Seconds to wait after sending 0x7F before reading feedback
    _TRIGGER_WAIT = 1.5

    def setUp(self):
        self.host = OTAHost(TEST_PORT, TEST_BAUD, verbose=False)
        self.host.connect()
        output = self.host.trigger_ota(wait=self._TRIGGER_WAIT)
        if 'OTA mode' not in output and 'START' not in output:
            self.host.disconnect()
            self.skipTest(
                "Factory app did not enter OTA mode — "
                "check that ota_factory_app is running at 0x0800C000"
            )

    def tearDown(self):
        self.host.disconnect()

    def _send_raw(self, data: bytes) -> None:
        """Transmit raw bytes directly to the target."""
        self.host._send(data)

    def _read_resp(self, timeout: float = 5.0) -> dict:
        """Read the next OTA_Response_t from the target."""
        return self.host._read_response(timeout=timeout)


# ---------------------------------------------------------------------------
# TC-PROTO-01: OTA trigger byte
# ---------------------------------------------------------------------------

class TestOTATrigger(unittest.TestCase):
    """TC-PROTO-01 — Sending 0x7F must activate OTA mode in the factory app."""

    def test_trigger_activates_ota_mode(self):
        """Factory app must print 'OTA mode triggered' after receiving 0x7F."""
        with OTAHost(TEST_PORT, TEST_BAUD) as host:
            output = host.trigger_ota(wait=1.5)
        self.assertTrue(
            'OTA mode' in output or 'START' in output,
            f"OTA mode not confirmed in UART output:\n{output}",
        )


# ---------------------------------------------------------------------------
# TC-PROTO-02 – TC-PROTO-06: START packet tests
# ---------------------------------------------------------------------------

class TestStartPacket(OTAProtocolTestBase):
    """START packet validation tests (TC-PROTO-02 through TC-PROTO-06)."""

    def test_valid_start_acked(self):
        """TC-PROTO-02: Well-formed START must be ACKed with ERR_NONE."""
        fw   = make_minimal_valid_fw()
        resp = self.host.send_start_packet(fw, fw_version=1)
        self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                         "Valid START should receive ACK")
        self.assertEqual(resp['error_code'], 0x00,
                         "ACK error_code should be ERR_NONE")
        self.assertTrue(resp['crc_valid'],
                        "ACK response CRC must be valid")

    def test_start_wrong_magic_nacked(self):
        """TC-PROTO-03: START with wrong magic must be NACKed."""
        fw = make_minimal_valid_fw()
        # Build a valid packet then corrupt the magic bytes
        pkt = bytearray(build_packet(
            PACKET_TYPE_START, 0, 1, 0,
            DEFAULT_CHUNK_SIZE, len(fw), 1,
            struct.pack('<I', crc32(fw)),
        ))
        # Overwrite magic (offset 0, uint16 LE) with 0xBBAA
        struct.pack_into('<H', pkt, 0, 0xBBAA)
        self._send_raw(bytes(pkt))
        resp = self._read_resp(timeout=5.0)
        self.assertEqual(resp['response_type'], PACKET_TYPE_NACK,
                         "Wrong magic should produce NACK")

    def test_start_bad_crc_nacked_with_crc_fail(self):
        """TC-PROTO-04: Corrupt packet CRC must produce NACK with ERR_CRC_FAIL."""
        fw  = make_minimal_valid_fw()
        pkt = bytearray(build_packet(
            PACKET_TYPE_START, 0, 1, 0,
            DEFAULT_CHUNK_SIZE, len(fw), 1,
            struct.pack('<I', crc32(fw)),
        ))
        pkt[-1] ^= 0xFF     # flip one bit in the packet CRC field
        self._send_raw(bytes(pkt))
        resp = self._read_resp(timeout=5.0)
        self.assertEqual(resp['response_type'], PACKET_TYPE_NACK,
                         "Bad packet CRC should produce NACK")
        self.assertEqual(resp['error_code'], ERR_CRC_FAIL,
                         "Error code should be ERR_CRC_FAIL (0x01)")

    def test_start_oversized_firmware_nacked_flash_full(self):
        """TC-PROTO-05: total_fw_size > APP_B_MAX_SIZE must produce NACK ERR_FLASH_FULL."""
        oversized = APP_B_MAX_SIZE + 1
        pkt = build_packet(
            PACKET_TYPE_START, 0, 1, 0,
            DEFAULT_CHUNK_SIZE, oversized, 1,
            struct.pack('<I', 0xDEADBEEF),
        )
        self._send_raw(pkt)
        resp = self._read_resp(timeout=5.0)
        self.assertEqual(resp['response_type'], PACKET_TYPE_NACK,
                         "Oversized firmware should produce NACK")
        self.assertEqual(resp['error_code'], ERR_FLASH_FULL,
                         "Error code should be ERR_FLASH_FULL (0x05)")

    def test_start_zero_firmware_size_nacked(self):
        """TC-PROTO-06: total_fw_size=0 must be rejected."""
        pkt = build_packet(
            PACKET_TYPE_START, 0, 1, 0,
            DEFAULT_CHUNK_SIZE, 0, 1,
            struct.pack('<I', 0),
        )
        self._send_raw(pkt)
        resp = self._read_resp(timeout=5.0)
        self.assertEqual(resp['response_type'], PACKET_TYPE_NACK,
                         "Zero-size firmware should produce NACK")


# ---------------------------------------------------------------------------
# TC-PROTO-07 – TC-PROTO-10: DATA packet tests
# ---------------------------------------------------------------------------

class TestDataPackets(OTAProtocolTestBase):
    """DATA packet sequence and validation tests (TC-PROTO-07 through TC-PROTO-10)."""

    # 2-chunk firmware for these tests
    FW_SIZE    = DEFAULT_CHUNK_SIZE * 2
    CHUNK_SIZE = DEFAULT_CHUNK_SIZE

    def _start_transfer(self, fw_data: bytes) -> None:
        """Send a valid START packet and assert ACK before any DATA test."""
        resp = self.host.send_start_packet(fw_data, fw_version=1,
                                           chunk_size=self.CHUNK_SIZE)
        self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                         "START must be ACKed before DATA tests")

    def test_data_chunks_acked_in_sequence(self):
        """TC-PROTO-07: Every DATA chunk must receive an ACK with the correct chunk_index."""
        fw = make_minimal_valid_fw(self.FW_SIZE)
        self._start_transfer(fw)

        total_chunks = self.FW_SIZE // self.CHUNK_SIZE
        for i in range(total_chunks):
            chunk = fw[i * self.CHUNK_SIZE:(i + 1) * self.CHUNK_SIZE]
            resp  = self.host.send_data_packet(
                chunk, i, total_chunks, len(fw), 1, seq=i + 1
            )
            self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                             f"Chunk {i} should be ACKed")
            self.assertEqual(resp['chunk_index'], i,
                             f"ACK chunk_index should echo {i}")

    def test_wrong_chunk_index_nacked_sequence_error(self):
        """TC-PROTO-08: chunk_index=1 when 0 is expected must produce ERR_SEQUENCE_ERROR."""
        fw = make_minimal_valid_fw(self.FW_SIZE)
        self._start_transfer(fw)

        chunk = fw[:self.CHUNK_SIZE]
        # Send chunk_index=1 when firmware expects index 0
        pkt = build_packet(
            PACKET_TYPE_DATA, 1, 2, 1,
            len(chunk), len(fw), 1, chunk,
        )
        self._send_raw(pkt)
        resp = self._read_resp(timeout=5.0)
        self.assertEqual(resp['response_type'], PACKET_TYPE_NACK,
                         "Wrong chunk_index should produce NACK")
        self.assertEqual(resp['error_code'], ERR_SEQUENCE_ERROR,
                         "Error code should be ERR_SEQUENCE_ERROR (0x03)")

    def test_data_bad_crc_nacked_crc_fail(self):
        """TC-PROTO-09: DATA with flipped CRC byte must produce ERR_CRC_FAIL."""
        fw = make_minimal_valid_fw(self.FW_SIZE)
        self._start_transfer(fw)

        chunk = fw[:self.CHUNK_SIZE]
        pkt   = bytearray(build_packet(
            PACKET_TYPE_DATA, 1, 2, 0,
            len(chunk), len(fw), 1, chunk,
        ))
        pkt[-2] ^= 0xFF   # flip a byte in the CRC field
        self._send_raw(bytes(pkt))
        resp = self._read_resp(timeout=5.0)
        self.assertEqual(resp['response_type'], PACKET_TYPE_NACK,
                         "Bad DATA CRC should produce NACK")
        self.assertEqual(resp['error_code'], ERR_CRC_FAIL,
                         "Error code should be ERR_CRC_FAIL (0x01)")

    def test_end_packet_instead_of_data_nacked(self):
        """TC-PROTO-10: Sending PACKET_TYPE_END when DATA is expected must fail."""
        fw = make_minimal_valid_fw(self.FW_SIZE)
        self._start_transfer(fw)

        # Send an END where a first DATA chunk is expected
        pkt = build_packet(
            PACKET_TYPE_END, 1, 2, 0,
            self.CHUNK_SIZE, len(fw), 1, b'',
        )
        self._send_raw(pkt)
        resp = self._read_resp(timeout=5.0)
        self.assertEqual(resp['response_type'], PACKET_TYPE_NACK,
                         "PACKET_TYPE_END in place of DATA should produce NACK")


# ---------------------------------------------------------------------------
# TC-PROTO-11 – TC-PROTO-12: END packet tests
# ---------------------------------------------------------------------------

class TestEndPacket(OTAProtocolTestBase):
    """END packet and firmware CRC verification tests (TC-PROTO-11, TC-PROTO-12)."""

    CHUNK_SIZE = DEFAULT_CHUNK_SIZE

    def test_complete_transfer_acked(self):
        """TC-PROTO-11: A fully valid START→DATA→END sequence must ACK the END."""
        fw      = make_minimal_valid_fw(self.CHUNK_SIZE)   # exactly 1 chunk
        success = self.host.send_firmware(fw, fw_version=2,
                                          chunk_size=self.CHUNK_SIZE)
        self.assertTrue(success, "send_firmware should return True on success")

    def test_end_wrong_expected_crc_nacked(self):
        """TC-PROTO-12: START carrying a wrong expected_fw_crc must cause END NACK.

        The START packet stores the caller-supplied expected firmware CRC in
        payload[0:4].  The factory app checks the written flash against this
        value at the END stage.  Sending a deliberately wrong expected CRC
        must produce NACK ERR_CRC_FAIL on the END packet.
        """
        fw           = make_minimal_valid_fw(self.CHUNK_SIZE)
        total_chunks = 1
        seq          = 0

        # Build a START with a wrong expected firmware CRC
        bad_expected_crc = crc32(fw) ^ 0xFFFFFFFF
        pkt = build_packet(
            PACKET_TYPE_START, seq, total_chunks, 0,
            self.CHUNK_SIZE, len(fw), 1,
            struct.pack('<I', bad_expected_crc),
        )
        self._send_raw(pkt)
        resp = self._read_resp(timeout=35.0)
        self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                         "START with bad expected CRC should still be ACKed "
                         "(mismatch is only detected at END)")
        seq += 1

        # Send the correct DATA chunk
        resp = self.host.send_data_packet(fw, 0, total_chunks, len(fw), 1, seq=seq)
        self.assertEqual(resp['response_type'], PACKET_TYPE_ACK,
                         "DATA chunk should be ACKed")
        seq += 1

        # END must be NACKed because CRC of written flash != bad_expected_crc
        resp = self.host.send_end_packet(seq=seq)
        self.assertEqual(resp['response_type'], PACKET_TYPE_NACK,
                         "END should be NACKed when expected CRC does not match flash")
        self.assertEqual(resp['error_code'], ERR_CRC_FAIL,
                         "Error code must be ERR_CRC_FAIL (0x01)")


# ---------------------------------------------------------------------------
# CLI + unittest runner
# ---------------------------------------------------------------------------

def _parse_args():
    parser = argparse.ArgumentParser(
        description="OTA protocol compliance tests.",
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
