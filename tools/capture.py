#!/usr/bin/env python3
"""Capture raw ESP32 Marauder serial output for building parsers against.

Auto-detects the board's /dev/cu.* port (or pass one), runs a sequence of
Marauder CLI commands with delays, and prints + saves everything verbatim.

Usage:
    python tools/capture.py                 # scan, then list -a (default)
    python tools/capture.py --port /dev/cu.usbserial-0001
    python tools/capture.py --cmds "scanap:25" "stopscan:1" "list -a:2"
    python tools/capture.py -o dumps/list_a.txt

Each --cmds entry is "COMMAND:SECONDS_TO_WAIT_AFTER".
"""
import argparse, glob, sys, time
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip install pyserial")

BAUD = 115200
DEFAULT_SEQ = ["stopscan:1", "scanap:25", "stopscan:1", "list -a:3"]
SKIP = {"/dev/cu.Bluetooth-Incoming-Port", "/dev/cu.debug-console"}


def autodetect():
    ports = [p for p in glob.glob("/dev/cu.*") if p not in SKIP]
    if not ports:
        sys.exit("No serial port found. Plug the board in, or pass --port. "
                 "Seen ports: " + (", ".join(glob.glob('/dev/cu.*')) or "none"))
    if len(ports) > 1:
        sys.exit(f"Multiple ports, pick one with --port: {ports}")
    return ports[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--cmds", nargs="+", default=DEFAULT_SEQ,
                    help='entries "CMD:SECONDS"')
    ap.add_argument("-o", "--out")
    args = ap.parse_args()

    port = args.port or autodetect()
    print(f"# port={port} baud={args.baud}", file=sys.stderr)

    captured = []
    with serial.Serial(port, args.baud, timeout=0.2) as s:
        time.sleep(0.5)
        s.reset_input_buffer()
        for entry in args.cmds:
            cmd, _, wait = entry.rpartition(":")
            cmd = cmd or entry
            wait = float(wait) if wait else 2.0
            print(f"# > {cmd!r} (wait {wait}s)", file=sys.stderr)
            s.write((cmd + "\n").encode())
            s.flush()
            deadline = time.time() + wait
            while time.time() < deadline:
                chunk = s.read(4096)
                if chunk:
                    text = chunk.decode("utf-8", "replace")
                    sys.stdout.write(text)
                    sys.stdout.flush()
                    captured.append(text)

    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text("".join(captured))
        print(f"\n# saved -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
