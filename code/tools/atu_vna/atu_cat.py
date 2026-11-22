#!/usr/bin/env python3
"""Drive the ATU through the Phoenix firmware's AT CAT command and verify the relays.

The counterpart of atu.py, which drives the standalone TestATUBoard sketch. This
one talks to the full Phoenix firmware, built with ATU_PRESENT in Config.h.

Usage: atu_cat.py [--port /dev/ttyACM0] CMD [CMD ...]
       e.g.  atu_cat.py ALL0 IN "LM 32" "CM 16"

Commands (the same vocabulary as atu.py / TestATUBoard):
  ALL0          safe state: every relay off (bypassed)
  ALL1          every relay on
  IN / BYP      network in line / bypassed          [K19+K20]
  CL / LC       caps at input / caps at output      [K17]
  BAL / UNB     balun output / coax output          [K18]
  LM <0-255>    inductor mask
  CM <0-255>    capacitor mask
  L<n> / C<n>   toggle one inductor / capacitor (1-8)
  ?             just print the state
Each command reads the current state with AT;, applies the change with one
ATnbsLLLCCC; write, and reads the state back. Exits 1 if the radio refuses a write
(e.g. while transmitting) or the I2C readback differs from the register.

The port: with usb=serialmidiaudio (USB audio builds) CAT shares the single
serial port, /dev/ttyACM0. With usb=serial2 it is the second port, /dev/ttyACM1.
"""
import argparse
import re
import sys
import time

import serial

STATE_RE = re.compile(r"AT([01])([01])([01])(\d{3})(\d{3}),([0-9A-Fa-f]{5}),([0-9A-Fa-f]{6}),([01]);")


class State:
    def __init__(self, network, balun, caps_in, l_mask, c_mask):
        self.network, self.balun, self.caps_in = network, balun, caps_in
        self.l_mask, self.c_mask = l_mask, c_mask

    def command(self):
        return "AT%d%d%d%03d%03d;" % (self.network, self.balun, self.caps_in,
                                       self.l_mask, self.c_mask)


def transact(ser, text, timeout=2.0):
    """Send text and return everything received until a full AT state reply arrives."""
    ser.reset_input_buffer()
    ser.write(text.encode())
    buf = ""
    t0 = time.time()
    while time.time() - t0 < timeout:
        buf += ser.read(ser.in_waiting or 1).decode(errors="replace")
        if STATE_RE.search(buf):
            return buf
        if buf.endswith("?;") and text == "AT;":
            break
    raise RuntimeError("no AT reply (is the firmware built with ATU_PRESENT?); got %r" % buf)


def read_state(ser, prefix=""):
    """Send prefix (optionally a write) then AT;, and parse the state reply.

    Returns (state, register, readback, present, refused).
    """
    reply = transact(ser, prefix + "AT;")
    m = STATE_RE.search(reply)
    refused = reply[:m.start()].find("?;") >= 0
    st = State(int(m.group(1)), int(m.group(2)), int(m.group(3)),
               int(m.group(4)), int(m.group(5)))
    return st, int(m.group(6), 16), int(m.group(7), 16), m.group(8) == "1", refused


def apply(cmd, st):
    """Return the new State for one atu.py-style command, or None if not recognised."""
    c = cmd.strip().upper()
    n = None
    m = re.fullmatch(r"(LM|CM|L|C)\s*(\d+)", c)
    if m:
        c, n = m.group(1), int(m.group(2))
    if c == "?":
        pass
    elif c == "ALL0":
        st = State(0, 0, 0, 0, 0)
    elif c == "ALL1":
        st = State(1, 1, 1, 255, 255)
    elif c in ("IN", "BYP"):
        st.network = int(c == "IN")
    elif c in ("CL", "LC"):
        st.caps_in = int(c == "CL")
    elif c in ("BAL", "UNB"):
        st.balun = int(c == "BAL")
    elif c == "LM" and 0 <= n <= 255:
        st.l_mask = n
    elif c == "CM" and 0 <= n <= 255:
        st.c_mask = n
    elif c == "L" and 1 <= n <= 8:
        st.l_mask ^= 1 << (n - 1)
    elif c == "C" and 1 <= n <= 8:
        st.c_mask ^= 1 << (n - 1)
    else:
        return None
    return st


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("cmds", nargs="*", default=["?"])
    args = ap.parse_args()

    ok = True
    with serial.Serial(args.port, 38400, timeout=0.2) as ser:
        for cmd in args.cmds:
            st, _, _, _, _ = read_state(ser)
            new = apply(cmd, st)
            if new is None:
                print(f"{cmd!r}: unknown command")
                ok = False
                continue
            st, reg, rb, present, refused = read_state(ser, "" if cmd.strip() == "?" else new.command())
            line = (f"{cmd!r}: {st.command()} reg=0x{reg:05x} "
                    + (f"readback=0x{rb & 0x7FFFF:05x}" if present else "readback=n/a (ATU not found)")
                    + f" | network {'IN' if st.network else 'BYPASSED'}, "
                    + f"{'C-L' if st.caps_in else 'L-C'}, {'balun' if st.balun else 'coax'} output, "
                    + f"L mask {st.l_mask}, C mask {st.c_mask}")
            if refused:
                line += "  REFUSED (transmitting?)"
                ok = False
            if not present or (rb & 0x7FFFF) != reg:
                line += "  MISMATCH" if present else ""
                ok = False
            print(line)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
