#!/usr/bin/env python3
"""
decode_frame.py – Capture and decode a JPEG frame from the Arducam MEGA sample.

Usage:
    python3 decode_frame.py --port /dev/ttyUSB0 --baud 115200 --output frame.jpg

The script opens the serial port, waits for the JPEG_BEGIN marker emitted
by samples/capture/src/main.c, collects the hex-encoded JPEG data up to
the JPEG_END marker, decodes it, and writes the result to the output file.
"""

import argparse
import binascii
import sys

try:
    import serial
except ImportError:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Decode Arducam MEGA JPEG output")
    parser.add_argument("--port",   required=True, help="Serial port (e.g. /dev/ttyUSB0)")
    parser.add_argument("--baud",   type=int, default=115200, help="Baud rate")
    parser.add_argument("--output", required=True, help="Output JPEG file path")
    parser.add_argument("--timeout", type=int, default=30,
                        help="Seconds to wait for JPEG_BEGIN marker")
    args = parser.parse_args()

    print(f"Opening {args.port} at {args.baud} baud …")
    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        # --- Wait for JPEG_BEGIN ---
        print(f"Waiting for JPEG_BEGIN (timeout {args.timeout}s) …")
        import time
        deadline = time.time() + args.timeout
        hex_lines: list[str] = []
        in_frame = False

        while time.time() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()

            if line == "JPEG_BEGIN":
                in_frame = True
                print("  → JPEG_BEGIN received")
                continue

            if in_frame:
                if line == "JPEG_END":
                    print("  → JPEG_END received")
                    break
                hex_lines.append(line)
        else:
            print("ERROR: Timed out waiting for frame", file=sys.stderr)
            return 1

    # --- Decode hex data ---
    hex_data = "".join(hex_lines)
    try:
        jpeg_bytes = binascii.unhexlify(hex_data)
    except binascii.Error as exc:
        print(f"ERROR: Failed to decode hex data: {exc}", file=sys.stderr)
        return 1

    # --- Basic JPEG validation ---
    if len(jpeg_bytes) < 4:
        print("ERROR: Received data too short to be a JPEG", file=sys.stderr)
        return 1
    if jpeg_bytes[0] != 0xFF or jpeg_bytes[1] != 0xD8:
        print(f"WARNING: Data does not start with JPEG SOI (got "
              f"{jpeg_bytes[0]:02x} {jpeg_bytes[1]:02x})", file=sys.stderr)

    # --- Write output ---
    with open(args.output, "wb") as f:
        f.write(jpeg_bytes)

    print(f"Saved {len(jpeg_bytes)} bytes → {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
