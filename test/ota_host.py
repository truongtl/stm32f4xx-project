#!/usr/bin/env python3
"""
OTA Host Tool for STM32F411CEU6 OTA Factory App.

Implements the binary packet protocol defined in
examples/ota_factory_app/packet.h.  Can be used as a library (import
OTAHost) or as a standalone CLI tool.

Packet structures (little-endian, __attribute__((packed))):

  OTA_Packet_t   — host → device, 1052 bytes total
    magic         uint16   0xAA55
    packet_type   uint8    PACKET_TYPE_START/DATA/END
    seq_number    uint8    wraps at 255
    total_chunks  uint32
    chunk_index   uint32
    chunk_size    uint32   bytes in this chunk's payload
    total_fw_size uint32   total firmware bytes
    fw_version    uint32
    payload       uint8[1024]
    crc32         uint32   CRC32 over all preceding bytes

  OTA_Response_t — device → host, 13 bytes total
    magic         uint16   0x55AA
    response_type uint8    PACKET_TYPE_ACK/NACK
    seq_number    uint8
    chunk_index   uint32
    error_code    uint8
    crc32         uint32   CRC32 over all preceding bytes

Usage (CLI):
    python ota_host.py --port /dev/ttyUSB0 --firmware app.bin --version 2
    python ota_host.py --port COM3 --firmware app.bin --no-trigger -v
"""

import argparse
import struct
import sys
import time
import zlib
from pathlib import Path
from typing import Optional

import serial


# ---------------------------------------------------------------------------
# Constants from examples/ota_factory_app/packet.h
# ---------------------------------------------------------------------------

OTA_PACKET_MAGIC    = 0xAA55
OTA_RESPONSE_MAGIC  = 0x55AA
OTA_TRIGGER_BYTE    = 0x7F

PACKET_TYPE_START   = 0x01
PACKET_TYPE_DATA    = 0x02
PACKET_TYPE_END     = 0x03
PACKET_TYPE_ACK     = 0x10
PACKET_TYPE_NACK    = 0x11
PACKET_TYPE_ABORT   = 0x12

ERR_NONE            = 0x00
ERR_CRC_FAIL        = 0x01
ERR_FLASH_WRITE_FAIL = 0x02
ERR_SEQUENCE_ERROR  = 0x03
ERR_TIMEOUT         = 0x04
ERR_FLASH_FULL      = 0x05

ERR_NAMES = {
    ERR_NONE:             "NONE",
    ERR_CRC_FAIL:         "CRC_FAIL",
    ERR_FLASH_WRITE_FAIL: "FLASH_WRITE_FAIL",
    ERR_SEQUENCE_ERROR:   "SEQUENCE_ERROR",
    ERR_TIMEOUT:          "TIMEOUT",
    ERR_FLASH_FULL:       "FLASH_FULL",
}

# Maximum App B size in bytes — from mem_layout.h (256 KB)
APP_B_MAX_SIZE = 256 * 1024

# Default chunk payload size in bytes (matches sizeof(payload) in OTA_Packet_t)
DEFAULT_CHUNK_SIZE = 1024

# ---------------------------------------------------------------------------
# Struct layout constants
#   OTA_Packet_t:   '<HBBIIIII' header (24 B) + 1024 B payload + 4 B CRC
#   OTA_Response_t: '<HBBIB'    header (9 B)  + 4 B CRC
# ---------------------------------------------------------------------------

_PKT_HDR_FMT  = '<HBBIIIII'                                # 24 bytes
_PKT_HDR_SIZE = struct.calcsize(_PKT_HDR_FMT)              # 24
PACKET_SIZE   = _PKT_HDR_SIZE + DEFAULT_CHUNK_SIZE + 4     # 1052

_RESP_HDR_FMT  = '<HBBIB'                                  # 9 bytes
_RESP_HDR_SIZE = struct.calcsize(_RESP_HDR_FMT)            # 9
RESPONSE_SIZE  = _RESP_HDR_SIZE + 4                        # 13

# Little-endian encoding of OTA_RESPONSE_MAGIC (0x55AA) as two bytes
_RESPONSE_MAGIC_BYTES = struct.pack('<H', OTA_RESPONSE_MAGIC)  # b'\xaa\x55'


# ---------------------------------------------------------------------------
# Standalone functions (can be imported without instantiating OTAHost)
# ---------------------------------------------------------------------------

def crc32(data: bytes) -> int:
    """Return CRC32 matching the firmware's CRC32_Calculate().

    Uses the standard Ethernet polynomial (0x04C11DB7 reflected),
    initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF — identical to
    Python's zlib.crc32().
    """
    return zlib.crc32(data) & 0xFFFFFFFF


def build_packet(
    packet_type: int,
    seq: int,
    total_chunks: int,
    chunk_index: int,
    chunk_size: int,
    total_fw_size: int,
    fw_version: int,
    payload: bytes,
) -> bytes:
    """Build a serialised OTA_Packet_t with CRC32 appended.

    The payload is padded to exactly DEFAULT_CHUNK_SIZE (1024) bytes
    with 0xFF, matching the erased-flash state so the CRC remains
    meaningful if the host pads a short final chunk.
    """
    # Pad payload to the fixed 1024-byte field size
    padded = (payload[:DEFAULT_CHUNK_SIZE]).ljust(DEFAULT_CHUNK_SIZE, b'\xff')

    header = struct.pack(
        _PKT_HDR_FMT,
        OTA_PACKET_MAGIC,
        packet_type,
        seq & 0xFF,
        total_chunks,
        chunk_index,
        chunk_size,
        total_fw_size,
        fw_version,
    )
    body = header + padded
    return body + struct.pack('<I', crc32(body))


def parse_response(raw: bytes) -> dict:
    """Parse a raw 13-byte OTA_Response_t into a dictionary.

    Returns keys: magic, response_type, seq_number, chunk_index,
    error_code, crc32, crc_valid.  Raises OTAError if the data is
    shorter than RESPONSE_SIZE or has a wrong magic.
    """
    if len(raw) < RESPONSE_SIZE:
        raise OTAError(
            f"Response too short: {len(raw)} bytes (expected {RESPONSE_SIZE})"
        )

    magic, rtype, seq, chunk_idx, err = struct.unpack_from(_RESP_HDR_FMT, raw, 0)
    stored_crc = struct.unpack_from('<I', raw, _RESP_HDR_SIZE)[0]
    calc_crc   = crc32(raw[:_RESP_HDR_SIZE])

    if magic != OTA_RESPONSE_MAGIC:
        raise OTAError(f"Response has wrong magic: {magic:#06x}")

    return {
        'magic':         magic,
        'response_type': rtype,
        'seq_number':    seq,
        'chunk_index':   chunk_idx,
        'error_code':    err,
        'crc32':         stored_crc,
        'crc_valid':     stored_crc == calc_crc,
    }


# ---------------------------------------------------------------------------
# Exception
# ---------------------------------------------------------------------------

class OTAError(Exception):
    """Raised on protocol violations, NACK responses, or serial timeouts."""


# ---------------------------------------------------------------------------
# High-level host class
# ---------------------------------------------------------------------------

class OTAHost:
    """High-level OTA host that communicates with the STM32 OTA factory app.

    Opens a serial port, optionally triggers OTA mode on the target with the
    0x7F trigger byte, and orchestrates the full START → DATA × N → END
    packet exchange defined in packet.h.

    Usage as a context manager guarantees the serial port is closed::

        with OTAHost('/dev/ttyUSB0', verbose=True) as host:
            host.trigger_ota()
            host.send_firmware(fw_data, fw_version=2)
    """

    def __init__(self, port: str, baud: int = 115200, verbose: bool = False):
        self.port_name = port
        self.baud      = baud
        self.verbose   = verbose
        self._ser: Optional[serial.Serial] = None

    # ------------------------------------------------------------------
    # Connection lifecycle
    # ------------------------------------------------------------------

    def connect(self) -> None:
        """Open the serial port."""
        self._ser = serial.Serial(
            self.port_name,
            self.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=1.0,
        )
        # Drain any buffered bytes from a previous session
        time.sleep(0.1)
        self._ser.reset_input_buffer()

    def disconnect(self) -> None:
        """Close the serial port if it is open."""
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def __enter__(self) -> 'OTAHost':
        self.connect()
        return self

    def __exit__(self, *_) -> None:
        self.disconnect()

    # ------------------------------------------------------------------
    # Private helpers
    # ------------------------------------------------------------------

    def _log(self, msg: str) -> None:
        if self.verbose:
            print(f"[OTA] {msg}", flush=True)

    def _send(self, data: bytes) -> None:
        """Write bytes to the serial port."""
        self._ser.write(data)
        self._ser.flush()

    def _read_response(self, timeout: float = 5.0) -> dict:
        """Read and parse the next OTA_Response_t from the serial port.

        Scans the byte stream byte-by-byte, discarding firmware debug-print
        text, until the two-byte response magic (0x55AA in little-endian =
        [0xAA, 0x55]) is found.  Then reads the remaining 11 bytes and calls
        parse_response().

        Raises OTAError on timeout or a truncated response.
        """
        deadline     = time.time() + timeout
        old_timeout  = self._ser.timeout
        self._ser.timeout = 0.1   # short per-byte read timeout

        buf = bytearray()
        try:
            while time.time() < deadline:
                byte = self._ser.read(1)
                if not byte:
                    continue
                buf.append(byte[0])

                # Detect the little-endian magic: 0xAA followed by 0x55
                if len(buf) >= 2 and buf[-2] == _RESPONSE_MAGIC_BYTES[0] \
                        and buf[-1] == _RESPONSE_MAGIC_BYTES[1]:
                    # Read the 11 remaining bytes of OTA_Response_t
                    remaining = RESPONSE_SIZE - 2
                    self._ser.timeout = max(1.0, deadline - time.time())
                    rest = self._ser.read(remaining)
                    if len(rest) < remaining:
                        raise OTAError(
                            f"Response truncated: got {2 + len(rest)} of "
                            f"{RESPONSE_SIZE} bytes"
                        )
                    return parse_response(bytes(_RESPONSE_MAGIC_BYTES) + rest)
        finally:
            self._ser.timeout = old_timeout

        raise OTAError(f"Timed out waiting for response ({timeout:.0f}s)")

    def _read_uart_text(self, timeout: float = 2.0) -> str:
        """Read available UART text (newline-terminated debug prints) for up
        to *timeout* seconds and return it as a single string.
        """
        lines     = []
        deadline  = time.time() + timeout
        old_timeout = self._ser.timeout
        self._ser.timeout = 0.2   # short per-readline timeout
        try:
            while time.time() < deadline:
                line = self._ser.readline()
                if line:
                    lines.append(line.decode('ascii', errors='replace').rstrip())
        finally:
            self._ser.timeout = old_timeout
        return '\n'.join(lines)

    # ------------------------------------------------------------------
    # Protocol operations
    # ------------------------------------------------------------------

    def trigger_ota(self, wait: float = 1.5) -> str:
        """Send the OTA trigger byte (0x7F) and return UART text response.

        The factory app detects this byte in its interrupt-driven RX loop
        and calls OTA_Receive(), which then blocks waiting for a START packet.
        """
        self._log("Sending OTA trigger byte 0x7F")
        self._send(bytes([OTA_TRIGGER_BYTE]))
        time.sleep(wait)
        text = self._read_uart_text(timeout=1.5)
        self._log(f"Target: {text!r}")
        return text

    def send_start_packet(
        self,
        fw_data: bytes,
        fw_version: int = 1,
        chunk_size: int = DEFAULT_CHUNK_SIZE,
        seq: int = 0,
        timeout: float = 35.0,
    ) -> dict:
        """Send PACKET_TYPE_START and return the parsed ACK/NACK response.

        Encodes the full-firmware CRC32 as a uint32_t in payload[0:4],
        matching the firmware extraction:
            memcpy(&expected_fw_crc, pkt.payload, sizeof(uint32_t));
        """
        total_size   = len(fw_data)
        total_chunks = (total_size + chunk_size - 1) // chunk_size
        fw_crc       = crc32(fw_data)

        # Place expected firmware CRC in the first 4 bytes of the payload
        payload = struct.pack('<I', fw_crc)

        pkt = build_packet(
            packet_type=PACKET_TYPE_START,
            seq=seq,
            total_chunks=total_chunks,
            chunk_index=0,
            chunk_size=chunk_size,
            total_fw_size=total_size,
            fw_version=fw_version,
            payload=payload,
        )

        self._log(
            f"START: size={total_size} B, chunks={total_chunks}, "
            f"version={fw_version:#010x}, crc={fw_crc:#010x}"
        )
        self._send(pkt)
        resp = self._read_response(timeout=timeout)
        self._log(
            f"START resp: type={resp['response_type']:#04x} "
            f"err={resp['error_code']}"
        )
        return resp

    def send_data_packet(
        self,
        chunk: bytes,
        chunk_index: int,
        total_chunks: int,
        total_fw_size: int,
        fw_version: int,
        seq: int,
        timeout: float = 15.0,
    ) -> dict:
        """Send a single PACKET_TYPE_DATA chunk and return the parsed response."""
        pkt = build_packet(
            packet_type=PACKET_TYPE_DATA,
            seq=seq,
            total_chunks=total_chunks,
            chunk_index=chunk_index,
            chunk_size=len(chunk),
            total_fw_size=total_fw_size,
            fw_version=fw_version,
            payload=chunk,
        )
        self._send(pkt)
        resp = self._read_response(timeout=timeout)
        self._log(
            f"DATA {chunk_index + 1}/{total_chunks} resp: "
            f"type={resp['response_type']:#04x}"
        )
        return resp

    def send_end_packet(self, seq: int, timeout: float = 15.0) -> dict:
        """Send PACKET_TYPE_END to finalise the OTA transfer and return the response."""
        pkt = build_packet(
            packet_type=PACKET_TYPE_END,
            seq=seq,
            total_chunks=0,
            chunk_index=0,
            chunk_size=0,
            total_fw_size=0,
            fw_version=0,
            payload=b'',
        )
        self._log("Sending END packet")
        self._send(pkt)
        resp = self._read_response(timeout=timeout)
        self._log(f"END resp: type={resp['response_type']:#04x}")
        return resp

    def send_firmware(
        self,
        fw_data: bytes,
        fw_version: int = 1,
        chunk_size: int = DEFAULT_CHUNK_SIZE,
    ) -> bool:
        """Execute the full OTA transfer: START → DATA × N → END.

        Returns True on success.  Raises OTAError if the target sends a
        NACK at any stage or if the response CRC is invalid.  The board
        reboots automatically after the END ACK.
        """
        total_size   = len(fw_data)
        total_chunks = (total_size + chunk_size - 1) // chunk_size
        seq          = 0

        # --- START ---
        resp = self.send_start_packet(fw_data, fw_version, chunk_size, seq)
        if resp['response_type'] != PACKET_TYPE_ACK or not resp['crc_valid']:
            err = ERR_NAMES.get(resp['error_code'], f"0x{resp['error_code']:02x}")
            raise OTAError(f"START rejected: err={err}")
        seq += 1

        # --- DATA ---
        for i in range(total_chunks):
            chunk = fw_data[i * chunk_size:(i + 1) * chunk_size]
            resp  = self.send_data_packet(
                chunk, i, total_chunks, total_size, fw_version, seq
            )
            if resp['response_type'] != PACKET_TYPE_ACK or not resp['crc_valid']:
                err = ERR_NAMES.get(resp['error_code'], f"0x{resp['error_code']:02x}")
                raise OTAError(f"DATA chunk {i} rejected: err={err}")
            seq = (seq + 1) & 0xFF
            print(f"  Chunk {i + 1}/{total_chunks} OK", flush=True)

        # --- END ---
        resp = self.send_end_packet(seq)
        if resp['response_type'] != PACKET_TYPE_ACK or not resp['crc_valid']:
            err = ERR_NAMES.get(resp['error_code'], f"0x{resp['error_code']:02x}")
            raise OTAError(f"END rejected: err={err}")

        print("OTA transfer complete. Target rebooting.", flush=True)
        return True


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def _build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="OTA host tool for STM32F411CEU6 OTA factory application.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument('--port',       required=True,
                   help="Serial port, e.g. /dev/ttyUSB0 or COM3")
    p.add_argument('--baud',       type=int, default=115200,
                   help="Baud rate")
    p.add_argument('--firmware',   required=True,
                   help="Path to firmware .bin file")
    p.add_argument('--version',    type=int, default=1,
                   help="Firmware version number embedded in metadata")
    p.add_argument('--chunk-size', type=int, default=DEFAULT_CHUNK_SIZE,
                   help=f"Chunk payload size in bytes (1–{DEFAULT_CHUNK_SIZE})")
    p.add_argument('--no-trigger', action='store_true',
                   help="Skip sending 0x7F, assume target is already in OTA mode")
    p.add_argument('--verbose', '-v', action='store_true',
                   help="Print verbose protocol logs")
    return p


def main() -> int:
    args = _build_arg_parser().parse_args()

    fw_path = Path(args.firmware)
    if not fw_path.is_file():
        print(f"Error: firmware file not found: {fw_path}", file=sys.stderr)
        return 1

    if not (1 <= args.chunk_size <= DEFAULT_CHUNK_SIZE):
        print(
            f"Error: --chunk-size must be 1–{DEFAULT_CHUNK_SIZE}",
            file=sys.stderr,
        )
        return 1

    fw_data = fw_path.read_bytes()
    if len(fw_data) > APP_B_MAX_SIZE:
        print(
            f"Error: firmware {len(fw_data)} B exceeds App B max "
            f"{APP_B_MAX_SIZE} B",
            file=sys.stderr,
        )
        return 1

    print(f"Firmware : {fw_path.name}  ({len(fw_data)} bytes, "
          f"version {args.version})")
    print(f"CRC32    : {crc32(fw_data):#010x}")

    try:
        with OTAHost(args.port, args.baud, verbose=args.verbose) as host:
            if not args.no_trigger:
                print("Triggering OTA mode...")
                output = host.trigger_ota()
                if output:
                    print(output)

            print("Sending firmware...")
            host.send_firmware(fw_data, args.version, args.chunk_size)

    except OTAError as exc:
        print(f"OTA failed: {exc}", file=sys.stderr)
        return 1
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
