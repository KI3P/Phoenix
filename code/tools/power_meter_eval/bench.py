#!/usr/bin/env python3
"""Bench check of the transmit power meter (PowerMeter.cpp) against the CMU200.

    venv/bin/python power_meter_eval/bench.py           (from code/tools)

    TX output -> 60 dB tap (through to the dummy load) -> CMU200 RF1
    CAT       -> /dev/ttyACM0 (single-port usb=serialmidiaudio build)
    Speech    -> the radio's USB audio sink, in digital mode (W9GR clip, looped)

At every point the radio is keyed, and while the CMU200 runs NPOWer (max = PEP,
avg = average power over the statistics run) the script polls ``PW;`` every
0.25 s. It also clears and reads the raw bridge statistics (``BR0;``/``BR;``):
their mean in mV is what the old firmware's running average in dB settled to,
so the old meter's reading is measured on the same key-down.

Compared:
  new PEP     max of the polled PEP (1.5 s hold) vs CMU max
  new AVG     mean of the polled 1 s averages vs CMU avg
  digital     the same with the bridge correction k taken out
  bridge AVG  mean of the polled bridge averages (watts) vs CMU avg
  old         10^(mean mV converted to dBm) vs CMU avg and max

Every setting touched (power, CESSB, TX source, digital mode) is read first and
restored at the end, on error and on Ctrl-C.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
import threading
import time

import numpy as np
from scipy import signal
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)

from cessb_hil import rf as R  # noqa: E402
from cessb_hil.cessb_hil_test import get_cessb, get_power, query, set_cessb, set_power  # noqa: E402
from filter_hil.radio import ED_END, CatError  # noqa: E402
from tx_filter_hil.tx_passband_rate_compare import SinglePortRadio  # noqa: E402

DEFAULT_WAV = os.path.expanduser(
    "~/Arduino/libraries/OpenAudio_ArduinoLibrary/utility/W9GR48.WAV")
USB_RATE = 44100


def db10(x):
    return 10.0 * math.log10(max(float(x), 1e-12))


class LoopedClip:
    """A WAV clip looped forever on the radio's USB audio sink."""

    def __init__(self, path: str, device_pattern: str, peak: float):
        import sounddevice as sd
        rate, x = wavfile.read(path)
        x = x.astype(np.float64)
        if x.ndim > 1:
            x = x[:, 0]
        x = signal.resample_poly(x, 147, 160) if rate == 48000 else x
        self.x = (peak * x / np.max(np.abs(x))).astype(np.float32)
        self.pos = 0
        dev = next(i for i, d in enumerate(sd.query_devices())
                   if device_pattern.lower() in d["name"].lower() and d["max_output_channels"] >= 2)
        self.stream = sd.OutputStream(device=dev, samplerate=USB_RATE, channels=2,
                                      dtype="float32", blocksize=256, callback=self._cb)

    def _cb(self, out, frames, time_info, status):
        idx = (self.pos + np.arange(frames)) % len(self.x)
        self.pos = (self.pos + frames) % len(self.x)
        out[:, 0] = self.x[idx]
        out[:, 1] = self.x[idx]

    def __enter__(self):
        self.stream.start()
        return self

    def __exit__(self, *exc):
        self.stream.stop()
        self.stream.close()


def read_ed(radio, keys):
    radio.drain_diag()
    radio.send("ED;", expect_reply=False)
    out = {}
    for line in radio.read_diag_until(ED_END, 5.0):
        for k in keys:
            if line.startswith(k + ":"):
                out[k] = line.split(":", 1)[1].strip()
    radio.drain_diag()
    return out


def poll_pw(radio):
    m = query(radio, "PW;", r"PW(\d+),(\d+),(\d+),(-?[\d.]+),([\d.]+);")
    pep, avg, br = (int(m.group(i)) / 1000.0 for i in (1, 2, 3))
    return {"t": time.monotonic(), "pep": pep, "avg": avg, "bridge": br,
            "k": float(m.group(4)), "swr": float(m.group(5))}


def read_br(radio):
    m = query(radio, "BR;", r"BR(\d+),([\d.]+),([\d.]+),([\d.]+),([\d.]+);")
    return {"n": int(m.group(1)), "fwd_mV": float(m.group(2)), "fwd_sd": float(m.group(3)),
            "ref_mV": float(m.group(4))}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cat-port", default="/dev/ttyACM0")
    ap.add_argument("--rf-input", default="RF1")
    ap.add_argument("--tap-db", type=float, default=60.0)
    ap.add_argument("--stats", type=int, default=300, help="NPOWer statistics for the tones")
    ap.add_argument("--voice-s", type=float, default=25.0, help="key-down for the speech points")
    ap.add_argument("--voice-peak", type=float, default=0.06, help="USB audio peak, full scale")
    ap.add_argument("--wav", default=DEFAULT_WAV)
    ap.add_argument("--device", default="Teensy")
    ap.add_argument("--rest", type=float, default=4.0)
    ap.add_argument("--settle", type=float, default=2.0, help="keyed before measuring")
    ap.add_argument("--powers", default="5,10")
    ap.add_argument("--speech-only", action="store_true")
    ap.add_argument("--out-dir", default=os.path.join(HERE, "results"))
    args = ap.parse_args()

    log = lambda m="": print(m, file=sys.stderr, flush=True)  # noqa: E731
    stamp = time.strftime("%Y%m%d_%H%M%S")
    doc = {"started": stamp, "args": vars(args), "points": []}
    restores = []
    powers = [int(p) for p in args.powers.split(",")]

    with SinglePortRadio(args.cat_port, None, 38400, 115200, 0.5, log=log) as radio, \
            R.Cmu(args.rf_input, 0.0) as cmu:
        try:
            time.sleep(0.3)
            radio.drain_diag()
            fa = int(query(radio, "FA;", r"FA(\d{11});").group(1))
            md = int(query(radio, "MD;", r"MD(\d);").group(1))
            pc0, pr0 = get_power(radio), get_cessb(radio)
            tt0 = int(query(radio, "TT;", r"TT(\d);").group(1))
            dg0 = int(query(radio, "DG;", r"DG(\d);").group(1))
            ed = read_ed(radio, ["currentBand[0]", "SWR_F_SlopeAdj", "SWR_F_Offset"])
            band = int(ed["currentBand[0]"])
            slope = 25.0 + float(ed["SWR_F_SlopeAdj"].split(",")[band])
            offset = float(ed["SWR_F_Offset"].split(",")[band])
            doc["radio_before"] = dict(fa=fa, md=md, pc=pc0, pr=pr0, tt=tt0, dg=dg0,
                                       band=band, fwd_slope=slope, fwd_offset=offset)
            log(f"CMU200: {cmu.identity()}")
            log(f"Radio: {fa/1e6:.6f} MHz MD{md} PC{pc0} PR{int(pr0)} TT{tt0} DG{dg0}; "
                f"band {band} forward line {slope:.2f} mV/dB, offset {offset:+.3f} dB")
            if md not in (1, 2) or dg0 != 0:
                log("error: start from SSB with digital mode off")
                return 2
            sign = 1 if md == 2 else -1
            restores += [("TX source", lambda: radio.send(f"TT{tt0};", expect_reply=False)),
                         ("digital mode", lambda: (radio.send(f"DG{dg0};", expect_reply=False),
                                                   time.sleep(1.5))),
                         ("CESSB", lambda: radio.send(f"PR{int(pr0)};", expect_reply=False)),
                         ("power", lambda: set_power(radio, pc0)),
                         ("unkey", lambda: radio.ptt_release())]

            def old_w(mv):
                return 10 ** ((mv / slope - 84.0 + offset + 46.0) / 10) / 1000

            def measure(label, stats=None, seconds=None):
                radio.ptt_press(0.2)
                t_key = time.monotonic()
                polls = []
                result = {}
                try:
                    time.sleep(args.settle)
                    radio.send("BR0;", expect_reply=False)
                    t0 = time.monotonic()

                    def run():
                        # NPOWer takes at most 1000 statistics (about 5 s); a longer
                        # run is several of those back to back: the max of their
                        # maxima, the mean of their averages (equal lengths).
                        try:
                            if stats is not None:
                                result["np"] = cmu.npower(fa + sign * 1300.0, stats)
                                return
                            chunks = []
                            while time.monotonic() - t0 < seconds:
                                chunks.append(cmu.npower(fa + sign * 1300.0, 1000))
                            avg = db10(np.mean([10 ** (c.avg_dbm / 10) for c in chunks]))
                            result["np"] = R.NPower(avg_dbm=avg,
                                                    max_dbm=max(c.max_dbm for c in chunks),
                                                    min_dbm=min(c.min_dbm for c in chunks),
                                                    freq_hz=chunks[-1].freq_hz)
                            result["chunks"] = len(chunks)
                        except Exception as exc:  # noqa: BLE001
                            result["err"] = exc
                    th = threading.Thread(target=run)
                    th.start()
                    while th.is_alive():
                        polls.append(poll_pw(radio))
                        time.sleep(0.25)
                    th.join()
                    br = read_br(radio)
                    elapsed = time.monotonic() - t0
                finally:
                    radio.ptt_release()
                if "err" in result:
                    raise result["err"]
                npw = result["np"]
                cmu_pep = 10 ** ((npw.max_dbm + args.tap_db) / 10) / 1000
                cmu_avg = 10 ** ((npw.avg_dbm + args.tap_db) / 10) / 1000
                pep = np.array([p["pep"] for p in polls])
                avg = np.array([p["avg"] for p in polls])
                brw = np.array([p["bridge"] for p in polls])
                kk = np.array([p["k"] for p in polls])
                corr = 10 ** (kk / 10)
                row = dict(label=label, keyed_s=time.monotonic() - t_key, measure_s=elapsed,
                           cmu_pep_w=cmu_pep, cmu_avg_w=cmu_avg, cmu_papr_db=npw.papr_db,
                           new_pep_w=float(pep.max()), new_avg_w=float(avg.mean()),
                           dig_pep_w=float((pep / corr).max()), dig_avg_w=float((avg / corr).mean()),
                           bridge_avg_w=float(brw.mean()), k_start=float(kk[0]), k_end=float(kk[-1]),
                           swr=float(np.median([p["swr"] for p in polls])),
                           old_w=old_w(br["fwd_mV"]), br=br, polls=polls)
                for key in ("new_pep_w", "dig_pep_w"):
                    row[key.replace("_w", "_db")] = db10(row[key] / cmu_pep)
                for key in ("new_avg_w", "dig_avg_w", "bridge_avg_w"):
                    row[key.replace("_w", "_db")] = db10(row[key] / cmu_avg)
                row["old_vs_avg_db"] = db10(row["old_w"] / cmu_avg)
                row["old_vs_pep_db"] = db10(row["old_w"] / cmu_pep)
                doc["points"].append(row)
                log(f"  {label}: CMU PEP {cmu_pep:.2f} W, avg {cmu_avg:.2f} W "
                    f"(PAPR {npw.papr_db:.2f} dB), {elapsed:.0f} s, {len(polls)} polls")
                log(f"    new PEP {row['new_pep_db']:+.2f} dB, AVG {row['new_avg_db']:+.2f} dB | "
                    f"digital PEP {row['dig_pep_db']:+.2f}, AVG {row['dig_avg_db']:+.2f} | "
                    f"bridge AVG {row['bridge_avg_db']:+.2f} | old {row['old_vs_avg_db']:+.2f} re avg, "
                    f"{row['old_vs_pep_db']:+.2f} re PEP | k {row['k_start']:+.2f}->{row['k_end']:+.2f} dB, "
                    f"SWR {row['swr']:.2f}")
                time.sleep(args.rest)
                return row

            def tt(src):
                radio.send(f"TT{src};", expect_reply=False)
                time.sleep(0.2)
                got = int(query(radio, "TT;", r"TT(\d);").group(1))
                if got != src:
                    raise CatError(f"TT did not take: asked {src}, read {got}")

            # Each power level: the tones, then speech, so the bridge correction k the
            # speech is shown with was learned (from the single tone) at that power.
            for pw in powers:
                set_power(radio, pw)
                if get_power(radio) != pw:
                    raise CatError("power setting did not take")
                if not args.speech_only:
                    log(f"\n== {pw} W setting, firmware test tones ==")
                    set_cessb(radio, False, 0.3)
                    tt(1)
                    measure(f"{pw} W single tone", stats=args.stats)
                    tt(2)
                    measure(f"{pw} W two tones", stats=args.stats)
                    set_cessb(radio, True, 0.3)
                    measure(f"{pw} W two tones, CESSB on", stats=args.stats)
                    set_cessb(radio, False, 0.3)
                    tt(tt0)

                log(f"\n== {pw} W setting, speech: W9GR clip over USB audio, digital mode ==")
                radio.send("DG1;", expect_reply=False)
                time.sleep(1.5)
                if int(query(radio, "DG;", r"DG(\d);").group(1)) != 1:
                    raise CatError("digital mode did not switch on")
                with LoopedClip(args.wav, args.device, args.voice_peak):
                    ds0 = query(radio, "DS;", r"DS[\d,]+;").group(0)
                    row = measure(f"{pw} W speech", seconds=args.voice_s)
                    ds1 = query(radio, "DS;", r"DS[\d,]+;").group(0)
                    row["ds"] = [ds0, ds1]
                    log(f"    USB audio stats {ds0} -> {ds1}")
                radio.send(f"DG{dg0};", expect_reply=False)
                time.sleep(1.5)
        except KeyboardInterrupt:
            log("aborted")
            return 3
        finally:
            for name, fn in reversed(restores):
                try:
                    fn()
                except Exception as exc:  # noqa: BLE001
                    log(f"WARNING: could not restore {name}: {exc}")
            time.sleep(0.3)
            try:
                doc["radio_after"] = dict(
                    pc=get_power(radio), pr=get_cessb(radio),
                    tt=int(query(radio, "TT;", r"TT(\d);").group(1)),
                    dg=int(query(radio, "DG;", r"DG(\d);").group(1)),
                    fa=int(query(radio, "FA;", r"FA(\d{11});").group(1)))
                log(f"Radio after: {doc['radio_after']}")
            except Exception as exc:  # noqa: BLE001
                log(f"WARNING: could not read the radio back: {exc}")
            os.makedirs(args.out_dir, exist_ok=True)
            out = os.path.join(args.out_dir, f"bench_{stamp}.json")
            with open(out, "w") as fh:
                json.dump(doc, fh, indent=1, default=float)
            log(f"Saved {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
