#!/usr/bin/env python3
"""Drive the TestATUBoard sketch over USB serial and verify the relay state.

Usage: atu.py CMD [CMD ...]   e.g.  atu.py ALL0 IN "LM 32" "CM 16"
Each command is sent, the reply is read until the sketch is waiting again, and
the final register / readback line is printed. Exits 1 on a mismatch.
"""
import re
import sys
import time

import serial

PORT = "/dev/ttyACM0"
PROMPT_END = "?          - This help"


def read_until_prompt(ser, timeout=20.0):
    buf = ""
    t0 = time.time()
    while time.time() - t0 < timeout:
        chunk = ser.read(ser.in_waiting or 1).decode(errors="replace")
        buf += chunk
        if PROMPT_END in buf:
            # small grace period for trailing bytes
            time.sleep(0.05)
            buf += ser.read(ser.in_waiting).decode(errors="replace")
            return buf
    raise TimeoutError("no prompt from sketch; got:\n" + buf[-2000:])


def send(ser, cmd):
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    return read_until_prompt(ser)


def main(cmds):
    ok = True
    with serial.Serial(PORT, 115200, timeout=0.2) as ser:
        for cmd in cmds:
            out = send(ser, cmd)
            m = re.search(r"ATU register = 0x([0-9a-fA-F]+)(?:, readback = 0x([0-9a-fA-F]+))?", out)
            reg = int(m.group(1), 16) if m else None
            rb = int(m.group(2), 16) if (m and m.group(2)) else None
            mism = "MISMATCH" in out
            summary = [l for l in out.splitlines() if l.startswith(("Total L", "Total C", "Network"))]
            print(f"{cmd!r}: reg=0x{reg:05x} readback=" + (f"0x{rb:05x}" if rb is not None else "n/a")
                  + (" MISMATCH" if mism else "") + " | " + " | ".join(summary))
            if mism or rb is None or rb != reg:
                ok = False
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main(sys.argv[1:] or ["?"])
