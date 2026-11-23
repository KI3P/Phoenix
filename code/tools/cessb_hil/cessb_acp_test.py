#!/usr/bin/env python3
"""Adjacent-channel power on speech: CESSB off and at several CESSB levels.

The W9GR speech clip (W9GR48.WAV, 12.2 s) loops from AD2 W1 into the microphone
input at a fixed drive. ``--peak-v`` is the clip's peak at W1. Find it with
``cessb_acp_calibrate.py``: it is the drive at which the voice peaks just reach
CESSB's ceiling, the envelope a steady tone needs for the power setting.

    AD2 W1    -> radio microphone input
    TX output -> dummy load, 60 dB tap -> CMU200 RF1
    CAT       -> /dev/ttyACM0 (single-port build)

How the spectrum is measured. Speech is not stationary, and a swept analyzer
samples each frequency point only briefly, once per sweep (1/560 of the sweep
time). Two pilots showed what that does:

* one wide sweep over all channels, 12 sweeps: the adjacent channel is reached
  about a second after the wanted one, single-sweep ratios ran from -13 to -75 dB
  and the averaged spectrum read 2.1 dB below NPOWer;
* per-channel sweeps tiled through the clip, 2 passes: the clip was covered
  (total within 0.3 dB of NPOWer), but the upper ratio with CESSB off still
  differed by 6 dB between passes. Its energy is dominated by brief narrowband
  bursts just above the channel edge (about 3.2 kHz, the voice's sibilants on the
  transmit filter's skirt), which a 1.15 s sweep catches only by chance.

* the same segments swept back to back while the clip played on: the wanted
  channel's two halves differed by 2.6 dB, because a dozen 1.25 s sweep cycles
  cover the 12.2 s clip unevenly and speech loudness varies a lot.

So each point is swept in frequency segments (SEGMENTS below). The channel-wide
segments are tiled through the clip with seeks, which covers every instant of it
exactly once per pass. The narrow segment over the adjacent channel's lower edge
(2950-3550 Hz, 0.32 s per sweep) runs back to back for four loops, sampling each
of its points about every 0.3 s. Every sweep is fetched on its own and averaged
in linear power (the CMU200's AVERage trace is a mean of dB values, which reads
bursty signals low). Pass 0 and pass 1 (for the edge, the first and second half
of its sweeps) are independent estimates: their difference is the repeatability.

At every speech point NPOWer (1 MHz filter) also runs over ``--npower-s``
seconds, for PEP and average power. The analyzer floor (unkeyed), the
transmitter floor (keyed, no audio) and the firmware's single 1 kHz test tone
(``TT1;``) are measured with the same segments, a few sweeps each.

Analysis (receiver filter, channel powers, ratios): ``acp_analysis.py``.
CESSB on/off, level, power and test-tone source are read first and restored at
the end, on error and on Ctrl-C.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time


_TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from cessb_hil import rf as R  # noqa: E402
from cessb_hil.acp_bench import (DEFAULT_WAV, HERE, ClipPlayer, Sweeper,  # noqa: E402
                                 dbm_of_watts, npower_long, watts)
from cessb_hil.cessb_hil_test import (get_cessb, get_mg, get_pl, get_power, query,  # noqa: E402
                                      set_cessb, set_level_db, set_power)
from cessb_hil.cessb_splatter_matrix import set_tt  # noqa: E402
from filter_hil.radio import CatError  # noqa: E402
from tx_filter_hil.tx_passband_rate_compare import SinglePortRadio  # noqa: E402

# Segments: name -> (sweep from, to, own from, to, mode, amount), Hz relative to
# the carrier, positive towards the wanted sideband. Each segment's trace is used
# over its own range only; together they cover -6050..9050 Hz, every receiver
# channel's support (offset - 50 to offset + 3050 Hz).
#   "tiled", n:      n passes; in each, the clip is cut into J tiles of L/J <= one
#                    sweep and every tile is swept from its own start (seek), so
#                    every instant of the clip is covered once per pass. Pass p
#                    starts p/n of a tile later.
#   "continuous", m: back-to-back sweeps for m clip loops while the clip plays on.
#                    For the short edge sweep (about 0.32 s), where many sweeps
#                    are needed and seeking would double the time.
SEGMENTS = {
    "lower_alt":  (-6050.0, -2950.0, -6050.0, -3000.0, "tiled", 1),
    "lower":      (-3050.0, 50.0, -3000.0, 0.0, "tiled", 3),
    "wanted":     (-50.0, 3050.0, 0.0, 2950.0, "tiled", 2),
    "upper_edge": (2950.0, 3550.0, 2950.0, 3500.0, "continuous", 4),
    "upper":      (3450.0, 6050.0, 3500.0, 6000.0, "tiled", 2),
    "upper_alt":  (5950.0, 9050.0, 6000.0, 9050.0, "tiled", 1),
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cat-port", default="/dev/ttyACM0")
    ap.add_argument("--rf-input", default="RF1")
    ap.add_argument("--tap-db", type=float, default=60.0)
    ap.add_argument("--power", type=int, default=10, help="SSB power setting, W")
    ap.add_argument("--peak-v", type=float, default=0.042, help="clip peak at AD2 W1, V")
    ap.add_argument("--cessb", default="off,0,2,4,6,8,10", help="'off' or levels in dB")
    ap.add_argument("--segments", default=",".join(SEGMENTS))
    ap.add_argument("--loops-scale", type=float, default=1.0,
                    help="multiply every segment's passes or clip loops")
    ap.add_argument("--rbw-hz", type=float, default=100.0)
    ap.add_argument("--ref-above-db", type=float, default=13.0,
                    help="analyzer reference level above the power setting at the tap")
    ap.add_argument("--npower-s", type=float, default=14.0)
    ap.add_argument("--settle", type=float, default=1.0)
    ap.add_argument("--rest", type=float, default=30.0)
    ap.add_argument("--no-refs", action="store_true", help="skip floors and the test tone")
    ap.add_argument("--wav", default=DEFAULT_WAV)
    ap.add_argument("--out-dir", default=os.path.join(HERE, "results"))
    args = ap.parse_args()

    settings = [None if c.strip() == "off" else int(c) for c in args.cessb.split(",")]
    segs = [c.strip() for c in args.segments.split(",")]
    stamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs(args.out_dir, exist_ok=True)
    out_path = os.path.join(args.out_dir, f"cessb_acp_{stamp}.json")
    log = lambda m="": print(m, file=sys.stderr, flush=True)  # noqa: E731
    doc = {"started": stamp, "format": "segments", "args": vars(args), "points": [],
           "key_down_s": 0.0, "segments": {k: SEGMENTS[k] for k in segs}}
    restores = []

    def save():
        with open(out_path, "w") as fh:
            json.dump(doc, fh, default=lambda o: o.tolist() if hasattr(o, "tolist") else float(o))

    with SinglePortRadio(args.cat_port, None, 38400, 115200, 0.5, log=log) as radio, \
            R.Cmu(args.rf_input, 0.0) as cmu:
        try:
            time.sleep(0.3)
            radio.drain_diag()
            fa = int(query(radio, "FA;", r"FA(\d{11});").group(1))
            md = int(query(radio, "MD;", r"MD(\d);").group(1))
            pc0, pr0, pl0, mg0 = get_power(radio), get_cessb(radio), get_pl(radio), get_mg(radio)
            tt0 = int(query(radio, "TT;", r"TT(\d);").group(1))
            doc["radio_before"] = dict(fa=fa, md=md, pc=pc0, pr=pr0, pl=pl0, mg=mg0, tt=tt0)
            log(f"CMU200: {cmu.identity()}")
            log(f"Radio: {fa} Hz MD{md} PC{pc0} PR{int(pr0)} PL{pl0} MG{mg0:03d} TT{tt0}")
            if md not in (1, 2):
                log("error: radio not in SSB")
                return 2
            sign = 1 if md == 2 else -1
            doc["sign"] = sign
            restores += [("TX source", lambda: radio.send(f"TT{tt0};", expect_reply=False)),
                         ("CESSB", lambda: radio.send(f"PR{int(pr0)};", expect_reply=False)),
                         ("level", lambda: radio.send(f"PL{pl0[0]:03d}{pl0[1]:03d};",
                                                      expect_reply=False)),
                         ("power", lambda: set_power(radio, pc0)),
                         ("unkey", lambda: radio.ptt_release())]
            set_power(radio, args.power)
            if get_power(radio) != args.power:
                raise CatError("power did not take")
            ref = dbm_of_watts(args.power, args.tap_db) + args.ref_above_db
            f_np = fa + sign * 1300.0

            def sweeper(name):
                a, b = (fa + sign * x for x in SEGMENTS[name][:2])
                return Sweeper(cmu, min(a, b), max(a, b), args.rbw_hz, ref)

            def run_segments(player=None):
                """Sweep every segment: twice each without a player (floors, the tone),
                or by the segment's mode while ``player`` loops the clip."""
                out = {}
                for name in segs:
                    lo, hi, own_lo, own_hi, mode, amount = SEGMENTS[name]
                    sw = sweeper(name)
                    tiles, passes, times = [], [], []

                    def take(p):
                        t1 = time.monotonic()
                        tiles.append(sw.sweep())
                        times.append(time.monotonic() - t1)
                        passes.append(p)

                    if player is None:
                        take(0)
                        take(0)
                    elif mode == "continuous":
                        t0 = time.monotonic()
                        seconds = amount * args.loops_scale * player.n / player.rate
                        while time.monotonic() - t0 < seconds:
                            take(0)
                        h = len(tiles) // 2          # halves: first and second half
                        passes[h:] = [1] * (len(tiles) - h)
                    else:
                        sw.sweep()                   # sets the sweep time
                        n_tiles = math.ceil(player.n / player.rate / sw.last_sweep_s)
                        step = player.n / n_tiles    # samples, a little under one sweep
                        for p in range(max(1, round(amount * args.loops_scale))):
                            for j in range(n_tiles):
                                player.seek(int(round((j + p / amount) * step)))
                                take(p)
                    out[name] = {"f": R.rel(sw.f, fa, sign), "tiles": tiles, "pass": passes,
                                 "rbw": sw.rbw, "own": [own_lo, own_hi], "mode": mode,
                                 "sweep_s": times, "sweep_only_s": sw.last_sweep_s}
                return out

            def keyed(fn):
                radio.ptt_press(args.settle)
                t0 = time.monotonic()
                try:
                    return fn()
                finally:
                    radio.ptt_release()
                    doc["key_down_s"] += time.monotonic() - t0 + args.settle

            def record(row, npw=None):
                if npw is not None:
                    row.update(pep_dbm=npw.max_dbm, avg_dbm=npw.avg_dbm,
                               pep_w=watts(npw.max_dbm, args.tap_db),
                               avg_w=watts(npw.avg_dbm, args.tap_db), papr_db=npw.papr_db)
                doc["points"].append(row)
                save()
                msg = f"  {row['label']}"
                if "keyed_s" in row:
                    msg += f": keyed {row['keyed_s']:.0f} s"
                if npw is not None:
                    msg += (f"; PEP {row['pep_w']:.2f} W, avg {row['avg_w']:.2f} W, "
                            f"PAPR {row['papr_db']:.2f} dB")
                if "player" in row:
                    n = ", ".join(f"{k} {len(v['tiles'])}" for k, v in row["segments"].items())
                    msg += f"; sweeps: {n}; AD2 lost {row['player']['lost']}"
                log(msg)

            if not args.no_refs:
                log("\n== Analyzer floor (unkeyed) ==")
                ch = run_segments()
                record(dict(label="analyzer floor", cessb_db=None, kind="floor", segments=ch))
                log("\n== Transmitter floor (keyed, no audio), CESSB off ==")
                set_cessb(radio, False, 0.3)
                t0 = time.monotonic()
                ch = keyed(lambda: run_segments())
                record(dict(label="transmitter floor", cessb_db=None, kind="tx_floor",
                            segments=ch, keyed_s=time.monotonic() - t0))
                time.sleep(args.rest / 2)
                log("\n== Single 1 kHz test tone (TT1), CESSB off ==")
                set_tt(radio, 1)
                try:
                    t0 = time.monotonic()
                    npw, ch = keyed(lambda: (cmu.npower(f_np, 300), run_segments()))
                finally:
                    set_tt(radio, tt0)
                record(dict(label="single tone", cessb_db=None, kind="tone", segments=ch,
                            keyed_s=time.monotonic() - t0), npw)
                time.sleep(args.rest / 2)

            for c in settings:
                if c is None:
                    set_cessb(radio, False, 0.3)
                else:
                    set_cessb(radio, True, 0.3)
                    set_level_db(radio, c, pl0[1])
                label = "CESSB off" if c is None else f"CESSB {c} dB"
                log(f"\n== Speech, {label} ==")
                with ClipPlayer(args.wav, args.peak_v) as pl:
                    time.sleep(0.5)
                    t0 = time.monotonic()
                    clip_s = pl.n / pl.rate
                    npw, ch = keyed(lambda: (npower_long(cmu, f_np, args.npower_s),
                                             run_segments(pl)))
                    st = pl.stat()
                record(dict(label=label, cessb_db=c, kind="speech", segments=ch,
                            clip_s=clip_s, keyed_s=time.monotonic() - t0, player=st), npw)
                time.sleep(args.rest)
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
                    fa=int(query(radio, "FA;", r"FA(\d{11});").group(1)),
                    pc=get_power(radio), pr=get_cessb(radio), pl=get_pl(radio),
                    tt=int(query(radio, "TT;", r"TT(\d);").group(1)))
                log(f"Radio after: {doc['radio_after']}")
            except Exception as exc:  # noqa: BLE001
                log(f"WARNING: could not read the radio back: {exc}")
            save()
            log(f"\nKey-down {doc['key_down_s']:.0f} s. Saved {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
