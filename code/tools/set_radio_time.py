#!/usr/bin/env python3
"""Set the Phoenix radio's clock over USB serial.

Sends a PJRC-standard time packet - 'T' + 10-digit timestamp + newline - to the
radio, which sets both the coin-cell-backed hardware RTC and the software clock
that drives the front-panel display.

WHAT THE RADIO DOES WITH THE NUMBER
-----------------------------------
The firmware feeds the timestamp straight to TimeLib and the display reads
hour()/minute()/second() back out of it. Nothing applies a time-zone offset. The
MY_TIMEZONE setting in Config.h is only a label - "EST: " and friends are pasted
in front of the digits as a string, and changing it does not shift the clock.

So the radio shows the *UTC decomposition of whatever number it is given*. To
make the display read local wall-clock time, the timestamp has to be shifted by
the local UTC offset before it is sent. That is what this script does by
default. Pass --utc to send a true UTC timestamp instead, which is what you want
if MY_TIMEZONE is set to "UTC: ".

Either way this clock is cosmetic: it drives the display and nothing else. FT8
timing comes from the PC's clock, not the radio's, so WSJT-X is unaffected by
what you set here.

USAGE
-----
    python3 set_radio_time.py                 # display shows local time
    python3 set_radio_time.py --utc           # display shows UTC
    python3 set_radio_time.py --list          # show candidate ports
    python3 set_radio_time.py --port COM5     # pick the port yourself
    python3 set_radio_time.py --offset -08:00 # override the local offset

Requires pyserial:  python3 -m pip install pyserial
"""

import argparse
import re
import sys
import time
from datetime import datetime, timezone

TEENSY_VID = 0x16C0          # PJRC
PACKET_DIGITS = 10           # the firmware accepts exactly 10, nothing else
SETTLE_S = 0.3               # let the CDC port come up before writing


def fail(message, hint=None):
    print(f"error: {message}", file=sys.stderr)
    if hint:
        print(f"       {hint}", file=sys.stderr)
    sys.exit(1)


def import_serial():
    try:
        import serial
        import serial.tools.list_ports as list_ports
    except ImportError:
        fail("pyserial is not installed",
             "install it with:  python3 -m pip install pyserial")
    return serial, list_ports


def port_sort_key(device):
    """Natural sort, so COM9 comes before COM10 and ttyACM2 before ttyACM10."""
    return [int(part) if part.isdigit() else part
            for part in re.split(r"(\d+)", device)]


def find_ports(list_ports):
    """Teensy ports, most likely first.

    With Dual Serial the radio presents two: the lower-numbered one is Serial,
    which is the port the firmware's own time-sync reader watches. With the
    serial+midi+audio USB type there is only one, shared with CAT - the CAT
    reader recognises the time packet there, so either way the first port works.
    """
    candidates = [p for p in list_ports.comports() if p.vid == TEENSY_VID]
    return sorted(candidates, key=lambda p: port_sort_key(p.device))


def describe(port):
    label = port.description or "serial port"
    return f"{port.device}  ({label})"


def plausible_ports(list_ports):
    """Ports worth showing a user.

    Motherboards enumerate dozens of legacy /dev/ttyS* that are never a radio, so
    prefer USB devices and only fall back to the full list if there are none.
    """
    every = sorted(list_ports.comports(), key=lambda p: port_sort_key(p.device))
    usb = [p for p in every if p.vid is not None]
    return usb or every


def local_offset_seconds(at):
    """Seconds east of UTC at the given moment, from the OS time-zone database."""
    offset = datetime.fromtimestamp(at).astimezone().utcoffset()
    if offset is None:                      # no zone info available at all
        return 0
    return int(offset.total_seconds())


def format_offset(seconds):
    """Render an offset as UTC+HH:MM. timedelta's own repr turns a negative
    offset into '-1 day, 20:00:00', which is not what anyone wants to read."""
    if seconds == 0:
        return "UTC"
    sign = "+" if seconds > 0 else "-"
    magnitude = abs(seconds)
    return f"UTC{sign}{magnitude // 3600:02d}:{magnitude % 3600 // 60:02d}"


def parse_offset(text):
    """Accept +HH:MM, -HH:MM, +HH, or a bare number of hours."""
    match = re.fullmatch(r"([+-]?)(\d{1,2})(?::?([0-5]\d))?", text.strip())
    if not match:
        fail(f"cannot read time-zone offset {text!r}",
             "use a form like +05:30, -08:00 or +1")
    sign = -1 if match.group(1) == "-" else 1
    hours = int(match.group(2))
    minutes = int(match.group(3) or 0)
    if hours > 14:
        fail(f"time-zone offset {text!r} is out of range")
    return sign * (hours * 3600 + minutes * 60)


def choose_port(args, list_ports):
    if args.port:
        return args.port

    found = find_ports(list_ports)
    if not found:
        every = plausible_ports(list_ports)
        hint = ("no serial ports at all - check the USB cable and that the radio is on"
                if not every else
                "ports seen, none of them a Teensy:\n       " +
                "\n       ".join(describe(p) for p in every) +
                "\n       pass one explicitly with --port if you know which it is")
        fail("no Teensy found", hint)

    if len(found) > 1:
        print(f"found {len(found)} Teensy ports, using the first:")
        for p in found:
            print(f"  {describe(p)}")
    return found[0].device


def open_port(serial, device):
    try:
        return serial.Serial(device, 115200, timeout=0.3)
    except PermissionError:
        fail(f"permission denied opening {device}",
             "on Linux:  sudo usermod -a -G dialout $USER   (then log out and back in)")
    except Exception as exc:                       # noqa: BLE001 - report anything
        # Distinguish "there is no such port" from "something else has it", since
        # the fix is completely different.
        text = str(exc).lower()
        missing = ("no such file" in text or "cannot find" in text
                   or getattr(exc, "errno", None) == 2)
        if missing:
            fail(f"no such port: {device}", "run with --list to see what is connected")
        fail(f"cannot open {device}: {exc}",
             "something else may already have the port. On the serial+midi+audio USB "
             "type\n       there is only one, so close WSJT-X or rigctld and try again")


def read_reply(handle, seconds=1.0):
    deadline, buffer = time.time() + seconds, b""
    while time.time() < deadline:
        waiting = handle.in_waiting
        if waiting:
            buffer += handle.read(waiting)
            deadline = time.time() + 0.25       # keep reading while it is talking
        else:
            time.sleep(0.02)
    return buffer


def glue_negative_offset(argv):
    """Let "--offset -08:00" work as well as "--offset=-08:00".

    argparse reads any token starting with '-' as another option, so a western
    hemisphere offset - which is most of them - would otherwise be rejected.
    Splice the two tokens together before argparse sees them.
    """
    out = []
    index = 0
    while index < len(argv):
        token = argv[index]
        if token == "--offset" and index + 1 < len(argv) \
                and re.fullmatch(r"[+-]?\d{1,2}(?::?\d\d)?", argv[index + 1]):
            out.append(f"--offset={argv[index + 1]}")
            index += 2
            continue
        out.append(token)
        index += 1
    return out


def main():
    parser = argparse.ArgumentParser(
        description="Set the Phoenix radio's clock over USB serial.",
        epilog="The radio applies no time-zone offset of its own, so by default "
               "this sends a timestamp shifted into local time to make the "
               "display read local wall-clock time.")
    parser.add_argument("--port", metavar="DEV",
                        help="serial port to use (e.g. COM5, /dev/ttyACM0)")
    parser.add_argument("--utc", action="store_true",
                        help="send a true UTC timestamp - use if MY_TIMEZONE is \"UTC: \"")
    parser.add_argument("--offset", metavar="TZ",
                        help="override the local UTC offset, e.g. +05:30")
    parser.add_argument("--list", action="store_true",
                        help="list candidate ports and exit")
    args = parser.parse_args(glue_negative_offset(sys.argv[1:]))

    serial, list_ports = import_serial()

    if args.list:
        found = find_ports(list_ports)
        if found:
            print("Teensy ports:")
            for p in found:
                print(f"  {describe(p)}")
        else:
            print("no Teensy ports found")
        every = plausible_ports(list_ports)
        others = [p for p in every if p.vid != TEENSY_VID]
        if others:
            print("other serial ports:")
            for p in others:
                print(f"  {describe(p)}")
        return 0

    if args.utc and args.offset:
        fail("--utc and --offset contradict each other", "pass one or the other")

    # Validate the offset before touching hardware - no point opening a port only
    # to reject the command line.
    manual_offset = parse_offset(args.offset) if args.offset else None

    device = choose_port(args, list_ports)
    handle = open_port(serial, device)

    try:
        time.sleep(SETTLE_S)
        handle.reset_input_buffer()             # drop any boot chatter

        # Transmit on a second boundary so the radio's seconds line up with the
        # PC's rather than landing mid-tick.
        send_at = int(time.time()) + 1

        if args.utc:
            offset = 0
        elif manual_offset is not None:
            offset = manual_offset
        else:
            offset = local_offset_seconds(send_at)

        stamp = send_at + offset
        if len(str(stamp)) != PACKET_DIGITS:
            fail(f"timestamp {stamp} is not {PACKET_DIGITS} digits",
                 "the firmware accepts exactly 10 - check the PC's clock")

        while time.time() < send_at:
            time.sleep(0.001)
        handle.write(f"T{stamp}\n".encode("ascii"))
        handle.flush()

        shown = datetime.fromtimestamp(stamp, timezone.utc)
        zone = format_offset(offset)
        print(f"sent T{stamp} on {device}")
        print(f"radio should now display {shown:%Y-%m-%d %H:%M:%S}  ({zone})")

        reply = read_reply(handle)
        if b"Time set" in reply:
            print("radio confirmed:", reply.decode("ascii", "replace").strip())
        elif reply:
            print("radio said:", reply.decode("ascii", "replace").strip())
        else:
            print("no confirmation - expected on the serial+midi+audio USB type, "
                  "where the\nconfirmation is suppressed to keep it out of the CAT "
                  "stream. Check the display.")
    finally:
        handle.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
