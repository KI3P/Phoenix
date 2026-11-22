#!/usr/bin/env python3
"""Stream a WAV file, looped, out of AD2 W1 (the AWG's play mode).

The AWG's custom buffer holds 4096 samples, about 85 ms at 48 kHz, so a speech
clip has to be streamed. This runs as its own process so that a long blocking
call elsewhere (a CMU200 sweep over GPIB) can never starve the AWG.

    ad2_play.py WAV PEAK_V [--rate HZ]

The clip is normalised so its largest sample is PEAK_V volts at W1. Commands on
stdin, one per line:

    seek N   jump to sample N of the clip (applied on the next buffer refill)
    stat     print "stat <position> <lost> <corrupted> <loops>"
    quit     silence W1 and exit (EOF or SIGTERM do the same)

On start-up it prints "ready <n_samples> <rate> <device_buffer_samples>" once streaming has begun.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import select
import signal
import sys

import numpy as np
from scipy import signal as sps
from scipy.io import wavfile

_TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from filter_hil.ad2 import (ANALOG_OUT_NODE_CARRIER, DEFAULT_DWF_PATH, HDWF_NONE,  # noqa: E402
                            DwfError, bind_argtypes, last_error, load_dwf)

FUNC_PLAY = 31


def load_clip(path: str, rate: float) -> np.ndarray:
    """The clip as float64 in -1..+1 (peak exactly 1), resampled to ``rate``."""
    fs, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x[:, 0]
    x -= np.mean(x)
    if int(round(rate)) != fs:
        g = np.gcd(int(round(rate)), int(fs))
        x = sps.resample_poly(x, int(round(rate)) // g, int(fs) // g)
    return x / np.max(np.abs(x))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("wav")
    ap.add_argument("peak_v", type=float)
    ap.add_argument("--rate", type=float, default=48000.0)
    ap.add_argument("--dwf", default=DEFAULT_DWF_PATH)
    args = ap.parse_args()

    clip = load_clip(args.wav, args.rate)
    n = len(clip)
    d = load_dwf(args.dwf)
    bind_argtypes(d)
    c_int, c_double = ctypes.c_int, ctypes.c_double
    p_int = ctypes.POINTER(c_int)
    d.FDwfAnalogOutNodeDataInfo.argtypes = [c_int, c_int, c_int, p_int, p_int]
    d.FDwfAnalogOutNodeDataSet.argtypes = [c_int, c_int, c_int, ctypes.POINTER(c_double), c_int]
    d.FDwfAnalogOutNodePlayStatus.argtypes = [c_int, c_int, c_int, p_int, p_int, p_int]
    d.FDwfAnalogOutStatus.argtypes = [c_int, c_int, ctypes.POINTER(ctypes.c_ubyte)]
    d.FDwfAnalogOutNodePlayData.argtypes = [c_int, c_int, c_int, ctypes.POINTER(c_double), c_int]

    h = c_int(HDWF_NONE)
    if d.FDwfDeviceOpen(c_int(-1), ctypes.byref(h)) != 1 or h.value == HDWF_NONE:
        raise DwfError(f"could not open an Analog Discovery: {last_error(d)}")
    ch, node = c_int(0), c_int(ANALOG_OUT_NODE_CARRIER)
    stopping = {"now": False}

    def stop(*_):
        stopping["now"] = True
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    pos = 0
    lost_total = corrupt_total = loops = 0

    def take(k: int) -> np.ndarray:
        nonlocal pos, loops
        idx = (pos + np.arange(k)) % n
        loops += (pos + k) // n
        pos = (pos + k) % n
        return clip[idx]

    try:
        lo, hi = c_int(), c_int()
        d.FDwfAnalogOutNodeDataInfo(h, ch, node, ctypes.byref(lo), ctypes.byref(hi))
        first = take(int(hi.value))
        d.FDwfAnalogOutNodeEnableSet(h, ch, node, c_int(1))
        d.FDwfAnalogOutNodeFunctionSet(h, ch, node, ctypes.c_ubyte(FUNC_PLAY))
        d.FDwfAnalogOutNodeFrequencySet(h, ch, node, c_double(args.rate))
        d.FDwfAnalogOutNodeAmplitudeSet(h, ch, node, c_double(args.peak_v))
        d.FDwfAnalogOutNodeOffsetSet(h, ch, node, c_double(0.0))
        buf = (c_double * len(first))(*first)
        if d.FDwfAnalogOutNodeDataSet(h, ch, node, buf, c_int(len(first))) != 1:
            raise DwfError(f"FDwfAnalogOutNodeDataSet failed: {last_error(d)}")
        d.FDwfAnalogOutRunSet(h, ch, c_double(0.0))
        d.FDwfAnalogOutRepeatSet(h, ch, c_int(0))
        if d.FDwfAnalogOutConfigure(h, ch, c_int(1)) != 1:
            raise DwfError(f"FDwfAnalogOutConfigure failed: {last_error(d)}")
        print(f"ready {n} {args.rate:g} {int(hi.value)}", flush=True)

        free, lost, corrupt = c_int(), c_int(), c_int()
        line = b""
        sts = ctypes.c_ubyte()
        while not stopping["now"]:
            # Status() fetches the device's state; PlayStatus() only reads that copy.
            if d.FDwfAnalogOutStatus(h, ch, ctypes.byref(sts)) != 1:
                raise DwfError(f"FDwfAnalogOutStatus failed: {last_error(d)}")
            if d.FDwfAnalogOutNodePlayStatus(h, ch, node, ctypes.byref(free),
                                             ctypes.byref(lost), ctypes.byref(corrupt)) != 1:
                raise DwfError(f"FDwfAnalogOutNodePlayStatus failed: {last_error(d)}")
            lost_total += lost.value
            corrupt_total += corrupt.value
            if free.value > 0:
                chunk = take(free.value)
                cbuf = (c_double * len(chunk))(*chunk)
                d.FDwfAnalogOutNodePlayData(h, ch, node, cbuf, c_int(len(chunk)))
            r, _, _ = select.select([sys.stdin], [], [], 0.005)
            if r:
                data = os.read(sys.stdin.fileno(), 4096)
                if not data:
                    break
                line += data
                while b"\n" in line:
                    cmd, line = line.split(b"\n", 1)
                    parts = cmd.decode().split()
                    if not parts:
                        continue
                    if parts[0] == "quit":
                        stopping["now"] = True
                    elif parts[0] == "seek":
                        pos = int(parts[1]) % n
                    elif parts[0] == "stat":
                        print(f"stat {pos} {lost_total} {corrupt_total} {loops}", flush=True)
    finally:
        d.FDwfAnalogOutReset(h, c_int(-1))
        d.FDwfDeviceCloseAll()
    return 0


if __name__ == "__main__":
    sys.exit(main())
