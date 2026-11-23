"""Shared bench pieces for the adjacent-channel power test: the AD2 clip player
(a subprocess running ``ad2_play.py``) and the radio/CMU200 setup."""

from __future__ import annotations

import math
import os
import subprocess
import sys
import time

import numpy as np

_TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from cessb_hil import rf as R  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_WAV = os.path.expanduser(
    "~/Arduino/libraries/OpenAudio_ArduinoLibrary/utility/W9GR48.WAV")


class ClipPlayer:
    """The W9GR clip looped out of AD2 W1 at ``peak_v`` volts peak."""

    def __init__(self, wav: str, peak_v: float, rate: float = 48000.0):
        self.proc = subprocess.Popen(
            [sys.executable, os.path.join(HERE, "ad2_play.py"), wav, f"{peak_v:.6f}",
             "--rate", f"{rate:g}"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        line = self.proc.stdout.readline().split()
        if not line or line[0] != "ready":
            self.close()
            raise RuntimeError(f"AD2 player did not start: {line}")
        self.n = int(line[1])
        self.rate = float(line[2])
        self.buffer = int(line[3])      # samples queued in the AD2: a seek's latency

    def _cmd(self, s: str) -> None:
        self.proc.stdin.write(s + "\n")
        self.proc.stdin.flush()

    def seek(self, sample: int, wait: bool = True) -> None:
        """Jump to ``sample``; with ``wait``, return once it is playing at W1 (the
        samples already queued in the AD2 have to play out first)."""
        self._cmd(f"seek {int(sample) % self.n}")
        if wait:
            time.sleep(self.buffer / self.rate + 0.03)

    def stat(self) -> dict:
        self._cmd("stat")
        _, pos, lost, corrupt, loops = self.proc.stdout.readline().split()
        return {"pos": int(pos), "lost": int(lost), "corrupted": int(corrupt),
                "loops": int(loops)}

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self._cmd("quit")
                self.proc.wait(5)
            except Exception:  # noqa: BLE001
                self.proc.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def npower_long(cmu: R.Cmu, freq_hz: float, seconds: float) -> R.NPower:
    """NPOWer over ``seconds``: back-to-back 1000-statistic runs (the instrument's
    limit), max of the maxima and mean of the averages."""
    t0 = time.monotonic()
    chunks = []
    while time.monotonic() - t0 < seconds or not chunks:
        chunks.append(cmu.npower(freq_hz, 1000))
    avg = R.db10(np.mean([10 ** (c.avg_dbm / 10) for c in chunks]))
    return R.NPower(avg_dbm=avg, max_dbm=max(c.max_dbm for c in chunks),
                    min_dbm=min(c.min_dbm for c in chunks), freq_hz=chunks[-1].freq_hz)


def watts(dbm: float, tap_db: float) -> float:
    return 10 ** ((dbm + tap_db) / 10) / 1000


def dbm_of_watts(w: float, tap_db: float) -> float:
    return 10 * math.log10(w * 1000) - tap_db


def sweep_traces(cmu: R.Cmu, start_hz: float, stop_hz: float, rbw_hz: float, ref_dbm: float,
                 sweeps: int = 1, traces=("current",), detector: str = "RMS",
                 timeout_s: float = 600.0) -> dict:
    """One spectrum statistics cycle of ``sweeps`` sweeps, fetching several traces.

    Returns {"f": Hz array, trace name: dBm array, ..., "rbw": reported RBW,
    "elapsed_s": cycle time}. Same configuration as the driver's spectrum_sweep.
    """
    from cmu200 import SPECTRUM_FREQ_RANGE_HZ, SPECTRUM_TRACES, _parse_float  # noqa: E402
    d = cmu.dev

    def _run():
        d.power_config = None
        d._select_input(cmu.rf_input)
        d._write("ABORt:SPECtrum")
        d._write_checked(f"CONFigure:SPECtrum:CONTrol ARRay,{sweeps}")
        d._write_checked("CONFigure:SPECtrum:CONTrol:REPetition SINGleshot,NONE,NONE")
        d._write_checked("SENSe:LEVel:MODE MANual")
        d._write_checked(f"SENSe:LEVel:REFerence {ref_dbm}")
        d._write_checked("SENSe:SPECtrum:LEVel:RANGe 100")
        d._write_checked(f"SENSe:SPECtrum:FREQuency:STARt {SPECTRUM_FREQ_RANGE_HZ[0]}")
        d._write_checked(f"SENSe:SPECtrum:FREQuency:STOP {stop_hz}")
        d._write_checked(f"SENSe:SPECtrum:FREQuency:STARt {start_hz}")
        d._write_checked(f"SENSe:SPECtrum:FREQuency:BANDwidth:RESolution {rbw_hz}")
        d._write_checked(f"SENSe:SPECtrum:DETector {detector}")
        t0 = time.monotonic()
        d._write_checked("INITiate:SPECtrum")
        while True:
            time.sleep(0.02)   # fine polling: elapsed_s sets the tile length
            state = d._query("FETCh:SPECtrum:STATus?").split(",")[0].strip()
            if state in ("RDY", "STOP", "OFF"):
                break
            if state == "ERR":
                raise RuntimeError(f"spectrum error: {d._drain_errors()}")
            if time.monotonic() - t0 > timeout_s:
                d._write("ABORt:SPECtrum")
                raise RuntimeError("spectrum cycle timed out")
        out = {"elapsed_s": time.monotonic() - t0}
        for tr in traces:
            parts = d._query_long(f"FETCh:ARRay:SPECtrum:{SPECTRUM_TRACES[tr]}?").split(",")
            try:
                float(parts[0])
            except ValueError:
                parts = parts[1:]
            out[tr] = np.array([-200.0 if (v := _parse_float(p)) is None else v for p in parts])
        start = d._query_float("SENSe:SPECtrum:FREQuency:STARt?")
        stop = d._query_float("SENSe:SPECtrum:FREQuency:STOP?")
        out["rbw"] = d._query("SENSe:SPECtrum:FREQuency:BANDwidth:RESolution?").split(",")[-1]
        n = len(out[traces[0]])
        out["f"] = np.linspace(start, stop, n)
        return out

    return d._run(_run)


class Sweeper:
    """Repeated single RMS sweeps of one span, configured once.

    Each :meth:`sweep` is one INITiate, a status poll and a fetch of the Current
    trace, so the per-sweep overhead is the fetch alone. (The CMU200's own
    AVERage trace is not used: on this instrument it is the mean of the dB values,
    which reads bursty signals low.)
    """

    def __init__(self, cmu: R.Cmu, start_hz: float, stop_hz: float, rbw_hz: float,
                 ref_dbm: float, detector: str = "RMS"):
        from cmu200 import SPECTRUM_FREQ_RANGE_HZ  # noqa: E402
        self.cmu = cmu
        d = cmu.dev

        def _cfg():
            d.power_config = None
            d._select_input(cmu.rf_input)
            d._write("ABORt:SPECtrum")
            d._write_checked("CONFigure:SPECtrum:CONTrol ARRay,1")
            d._write_checked("CONFigure:SPECtrum:CONTrol:REPetition SINGleshot,NONE,NONE")
            d._write_checked("SENSe:LEVel:MODE MANual")
            d._write_checked(f"SENSe:LEVel:REFerence {ref_dbm}")
            d._write_checked("SENSe:SPECtrum:LEVel:RANGe 100")
            d._write_checked(f"SENSe:SPECtrum:FREQuency:STARt {SPECTRUM_FREQ_RANGE_HZ[0]}")
            d._write_checked(f"SENSe:SPECtrum:FREQuency:STOP {stop_hz}")
            d._write_checked(f"SENSe:SPECtrum:FREQuency:STARt {start_hz}")
            d._write_checked(f"SENSe:SPECtrum:FREQuency:BANDwidth:RESolution {rbw_hz}")
            d._write_checked(f"SENSe:SPECtrum:DETector {detector}")
            start = d._query_float("SENSe:SPECtrum:FREQuency:STARt?")
            stop = d._query_float("SENSe:SPECtrum:FREQuency:STOP?")
            rbw = d._query("SENSe:SPECtrum:FREQuency:BANDwidth:RESolution?").split(",")[-1]
            return start, stop, rbw

        self.start, self.stop, self.rbw = d._run(_cfg)
        self.f = None
        self.last_sweep_s = float("nan")

    def sweep(self, timeout_s: float = 120.0) -> np.ndarray:
        from cmu200 import _parse_float  # noqa: E402
        d = self.cmu.dev

        def _one():
            t0 = time.monotonic()
            d._write("INITiate:SPECtrum")
            while True:
                time.sleep(0.01)
                state = d._query("FETCh:SPECtrum:STATus?").split(",")[0].strip()
                if state in ("RDY", "STOP", "OFF"):
                    break
                if state == "ERR":
                    raise RuntimeError(f"spectrum error: {d._drain_errors()}")
                if time.monotonic() - t0 > timeout_s:
                    d._write("ABORt:SPECtrum")
                    raise RuntimeError("sweep timed out")
            self.last_sweep_s = time.monotonic() - t0      # INITiate to done, no fetch
            parts = d._query_long("FETCh:ARRay:SPECtrum:CURRent?").split(",")
            try:
                float(parts[0])
            except ValueError:
                parts = parts[1:]
            return np.array([-200.0 if (v := _parse_float(p)) is None else v for p in parts])

        p = d._run(_one)
        if self.f is None:
            self.f = np.linspace(self.start, self.stop, len(p))
        return p
