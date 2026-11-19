#!/usr/bin/env python3
"""Capture UART logs. Run with the ESP-IDF Python environment."""
import argparse
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--seconds", type=float, default=25)
parser.add_argument("--reset", action="store_true")
args = parser.parse_args()

port = serial.Serial(port=None, baudrate=115200, timeout=0.1, exclusive=True)
port.dtr = False
port.rts = False
port.port = args.port
path = Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
with port, path.open("wb") as output:
    if args.reset:
        port.rts = True
        time.sleep(0.1)
        port.rts = False
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            output.write(chunk)
            output.flush()
print(path)
