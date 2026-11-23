#!/usr/bin/env python3
"""Adjacent-channel power measured with a real SSB receiver: a second Phoenix.

The transmitter (radio 2) sends the looped W9GR clip from AD2 W1, as in
cessb_acp_test.py. The receiver (radio 1) listens on the 60 dB tap through
``--atten-db`` of fixed attenuation. In digital mode its demodulated audio goes to the
PC over USB, tapped before the volume control, at a fixed gain when AGC is off.
For each channel, radio 1 is tuned to the carrier + offset (same sideband) and
the audio is recorded for a whole number of clip loops. The channel power is the
mean square of that audio, so every instant of the clip contributes once per
loop at every frequency in the passband. The receive filter is radio 1's own
SSB filter, set to 300-2700 Hz with FL/FW for the test.

    AD2 W1            -> radio 2 microphone input
    radio 2 TX output -> dummy load, 60 dB tap -> fixed attenuator -> radio 1 RX
    radio 2 CAT       -> --tx-port      radio 1 CAT -> --rx-port
    radio 1 USB audio -> this PC (--rx-audio picks the PipeWire source)

Radio 1 setup the script cannot do over CAT (it checks them in the ED dump and
stops if they are wrong): AGC off, and the receive attenuator as wanted.

Also measured: radio 1's floor with radio 2 unkeyed, and radio 2's single 1 kHz
test tone (``TT1;``). The tone gives the frequency offset between the two
radios, which is then corrected in radio 1's tuning, and, because a clean tone
has no splatter, the floor of the method itself (reciprocal mixing, receiver
sideband rejection).

The receiver's linearity is checked by repeating a point with a different
receive attenuator setting (``--label``): the ratios must not change.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time

import numpy as np
from scipy import signal

_TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from cessb_hil.acp_bench import DEFAULT_WAV, HERE, ClipPlayer  # noqa: E402
from cessb_hil.cessb_hil_test import (get_cessb, get_pl, get_power, query,  # noqa: E402
                                      set_cessb, set_level_db, set_power)
from cessb_hil.cessb_splatter_matrix import set_tt  # noqa: E402
from filter_hil.radio import ED_END, CatError  # noqa: E402
from tx_filter_hil.tx_passband_rate_compare import SinglePortRadio  # noqa: E402

RATE = 44100
CHANNELS = {"wanted": 0.0, "upper": 3000.0, "lower": -3000.0, "upper_alt": 6000.0,
            "lower_alt": -6000.0}
ED_KEYS = ["agc", "RAtten", "nrOptionSelect", "ANR_notchOn", "equalizerRec",
           "digitalRxLevel", "rfGainAllBands_dB", "currentBand[0]"]


def read_ed(radio, keys=ED_KEYS):
    radio.drain_diag()
    radio.send("ED;", expect_reply=False)
    out = {}
    for line in radio.read_diag_until(ED_END, 5.0):
        for k in keys:
            if line.startswith(k + ":"):
                out[k] = line.split(":", 1)[1].strip()
    radio.drain_diag()
    return out


def get_fa(radio) -> int:
    return int(query(radio, "FA;", r"FA(\d{11});").group(1))


def set_fa(radio, hz: int) -> None:
    radio.send(f"FA{int(hz):011d};", expect_reply=False)
    time.sleep(0.1)
    got = get_fa(radio)
    if got != int(hz):
        raise CatError(f"radio 1 did not tune: asked {hz}, read {got}")


def find_source(pattern: str) -> str:
    """The PipeWire/Pulse capture source whose name contains ``pattern`` (radio 1's
    USB serial number, say). PipeWire holds the Teensy capture devices, so audio is
    recorded through it rather than from ALSA directly."""
    import subprocess
    out = subprocess.run(["pactl", "list", "short", "sources"], capture_output=True,
                         text=True, check=True).stdout
    names = [ln.split("\t")[1] for ln in out.splitlines() if "\t" in ln]
    hits = [n for n in names if pattern in n and n.startswith("alsa_input")]
    if len(hits) != 1:
        raise RuntimeError(f"--rx-audio {pattern!r} matches {len(hits)} capture sources:\n  "
                           + "\n  ".join(names))
    return hits[0]


def source_volume(source: str, set_to: str | None = None) -> str:
    """Read (and optionally set) the PipeWire volume of a capture source. PipeWire
    applies it after the radio's 16-bit stream, so anything under 100 % hides clipping
    in the radio: the stream clips at its own full scale and then reads low."""
    import re
    import subprocess
    if set_to is not None:
        subprocess.run(["pactl", "set-source-volume", source, set_to], check=True)
    out = subprocess.run(["pactl", "get-source-volume", source], capture_output=True,
                         text=True, check=True).stdout
    return re.search(r"(\d+)%", out).group(1) + "%"


def record(source: str, seconds: float) -> np.ndarray:
    """``seconds`` of radio 1's USB audio (left channel), float, full scale 1.0."""
    import subprocess
    n = int(round(seconds * RATE))
    proc = subprocess.Popen(["parecord", f"--device={source}", "--raw", f"--rate={RATE}",
                             "--channels=2", "--format=float32le", "--latency-msec=20"],
                            stdout=subprocess.PIPE)
    try:
        skip = int(0.1 * RATE) * 8                   # the stream's first 100 ms
        need = skip + n * 8
        buf = bytearray()
        while len(buf) < need:
            chunk = proc.stdout.read(min(65536, need - len(buf)))
            if not chunk:
                raise RuntimeError("parecord stopped early")
            buf += chunk
    finally:
        proc.terminate()
        proc.wait(5)
    x = np.frombuffer(bytes(buf[skip:]), dtype="<f4").reshape(-1, 2)
    return x[:, 0].astype(np.float64)


def psd(x: np.ndarray):
    f, p = signal.welch(x, RATE, nperseg=8192, scaling="spectrum", average="mean")
    sel = f <= 4000
    return f[sel], p[sel]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tx-port", default="/dev/ttyACM0")
    ap.add_argument("--rx-port", default="/dev/ttyACM1")
    ap.add_argument("--rx-audio", required=True,
                    help="substring of radio 1's PipeWire capture source (its USB serial)")
    ap.add_argument("--atten-db", type=float, default=40.0, help="fixed attenuator, dB")
    ap.add_argument("--label", default="", help="free text, e.g. the RX attenuator setting")
    ap.add_argument("--power", type=int, default=10)
    ap.add_argument("--peak-v", type=float, default=0.042)
    ap.add_argument("--cessb", default="off,0,2,4,6,8,10",
                    help="'off' or levels in dB; 'none' for the tone and floor only")
    ap.add_argument("--channels", default=",".join(CHANNELS))
    ap.add_argument("--loops", type=float, default=2.0,
                    help="clip loops recorded per channel (alternates get half)")
    ap.add_argument("--filter", default="300,2700", help="radio 1 low,high cut, Hz")
    ap.add_argument("--target-peak-dbfs", type=float, default=-8.0,
                    help="the test tone's peak in radio 1's USB audio, set with DR")
    ap.add_argument("--settle", type=float, default=1.0)
    ap.add_argument("--rest", type=float, default=20.0)
    ap.add_argument("--no-refs", action="store_true")
    ap.add_argument("--wav", default=DEFAULT_WAV)
    ap.add_argument("--out-dir", default=os.path.join(HERE, "results"))
    args = ap.parse_args()

    settings = [] if args.cessb.strip() in ("", "none") else \
        [None if c.strip() == "off" else int(c) for c in args.cessb.split(",")]
    chans = [c.strip() for c in args.channels.split(",")]
    lo_cut, hi_cut = (int(v) for v in args.filter.split(","))
    stamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs(args.out_dir, exist_ok=True)
    out_path = os.path.join(args.out_dir, f"cessb_acp_rx_{stamp}.json")
    log = lambda m="": print(m, file=sys.stderr, flush=True)  # noqa: E731
    doc = {"started": stamp, "args": vars(args), "points": [], "key_down_s": 0.0,
           "channels": {c: CHANNELS[c] for c in chans}}
    restores = []
    dev = find_source(args.rx_audio)
    vol0 = source_volume(dev)
    source_volume(dev, "100%")
    doc["rx_source"] = {"name": dev, "volume_before": vol0}

    def save():
        with open(out_path, "w") as fh:
            json.dump(doc, fh, default=lambda o: o.tolist() if hasattr(o, "tolist") else float(o))

    with SinglePortRadio(args.tx_port, None, 38400, 115200, 0.5, log=log) as tx, \
            SinglePortRadio(args.rx_port, None, 38400, 115200, 0.5, log=log) as rx:
        try:
            time.sleep(0.3)
            tx.drain_diag()
            rx.drain_diag()
            fa = get_fa(tx)
            md = int(query(tx, "MD;", r"MD(\d);").group(1))
            pc0, pr0, pl0 = get_power(tx), get_cessb(tx), get_pl(tx)
            tt0 = int(query(tx, "TT;", r"TT(\d);").group(1))
            rfa0 = get_fa(rx)
            rmd0 = int(query(rx, "MD;", r"MD(\d);").group(1))
            rdg0 = int(query(rx, "DG;", r"DG(\d);").group(1))
            rfl0 = int(query(rx, "FL;", r"FL(-?\d+);").group(1))
            rfw0 = int(query(rx, "FW;", r"FW(-?\d+);").group(1))
            rdr0 = int(query(rx, "DR;", r"DR(\d{3});").group(1))
            ed = read_ed(rx)
            doc["tx_before"] = dict(fa=fa, md=md, pc=pc0, pr=pr0, pl=pl0, tt=tt0)
            doc["rx_before"] = dict(fa=rfa0, md=rmd0, dg=rdg0, fl=rfl0, fw=rfw0, dr=rdr0, ed=ed)
            log(f"TX radio 2: {fa} Hz MD{md} PC{pc0} PR{int(pr0)} PL{pl0} TT{tt0}")
            log(f"RX radio 1: {rfa0} Hz MD{rmd0} DG{rdg0} FL{rfl0} FW{rfw0} DR{rdr0}; ED {ed}")
            if md not in (1, 2):
                log("error: radio 2 not in SSB")
                return 2
            if ed.get("agc", "?").split()[0] not in ("0", "AGCOff"):
                log(f"error: radio 1 AGC is {ed.get('agc')!r}; switch it off on the touchscreen")
                return 2
            sign = 1 if md == 2 else -1
            restores += [("TX source", lambda: tx.send(f"TT{tt0};", expect_reply=False)),
                         ("TX CESSB", lambda: tx.send(f"PR{int(pr0)};", expect_reply=False)),
                         ("TX level", lambda: tx.send(f"PL{pl0[0]:03d}{pl0[1]:03d};",
                                                      expect_reply=False)),
                         ("TX power", lambda: set_power(tx, pc0)),
                         ("TX unkey", lambda: tx.ptt_release()),
                         ("RX frequency", lambda: tx_safe(rx, f"FA{rfa0:011d};")),
                         ("RX filter", lambda: (tx_safe(rx, f"FL{rfl0:04d};"),
                                                tx_safe(rx, f"FW{rfw0:04d};"))),
                         ("RX DR", lambda: tx_safe(rx, f"DR{rdr0:03d};")),
                         ("RX mode", lambda: (tx_safe(rx, f"MD{rmd0};"),
                                              tx_safe(rx, f"DG{rdg0};"), time.sleep(1.5)))]
            set_power(tx, args.power)
            if get_power(tx) != args.power:
                raise CatError("power did not take")
            # Radio 1: same sideband, digital mode, the test filter, NR and notch off
            rx.send(f"MD{md};", expect_reply=False)
            time.sleep(0.3)
            rx.send("DG1;", expect_reply=False)
            time.sleep(1.5)
            for cmd in ("NR0;", "NT0;", f"FL{lo_cut:04d};", f"FW{hi_cut:04d};"):
                rx.send(cmd, expect_reply=False)
                time.sleep(0.15)
            got = (int(query(rx, "DG;", r"DG(\d);").group(1)),
                   int(query(rx, "FL;", r"FL(-?\d+);").group(1)),
                   int(query(rx, "FW;", r"FW(-?\d+);").group(1)))
            log(f"RX radio 1 now DG{got[0]} FL{got[1]} FW{got[2]}")
            if got != (1, lo_cut, hi_cut):
                raise CatError(f"radio 1 setup did not take: {got}")
            doc["rx_setup"] = read_ed(rx)
            offset_hz = 0
            clip_s = None

            T0 = time.monotonic()
            events = doc.setdefault("events", [])

            def ev(what, **kw):
                events.append(dict(t=round(time.monotonic() - T0, 3), what=what, **kw))

            def tune(c):
                set_fa(rx, fa + sign * c + offset_hz)
                ev("tuned", offset_hz=c)
                time.sleep(1.0)       # retune and let the receive filters settle

            def envelope_db(x, block_s=0.05):
                n = int(block_s * RATE)
                m = len(x) // n
                return 10 * np.log10(np.mean(x[:m * n].reshape(m, n) ** 2, axis=1) + 1e-20)

            def measure_channels(seconds_of):
                out = {}
                for name in chans:
                    tune(CHANNELS[name])
                    t_a = time.monotonic() - T0
                    x = record(dev, seconds_of(name))
                    t_b = time.monotonic() - T0
                    ev("recorded", channel=name, start=round(t_a, 3), end=round(t_b, 3))
                    f, p = psd(x)
                    out[name] = {"power": float(np.mean(x ** 2)),
                                 "peak": float(np.max(np.abs(x))), "seconds": len(x) / RATE,
                                 "psd_f": f, "psd": p, "env_db": envelope_db(x),
                                 "t_start": t_a, "t_end": t_b}
                return out

            def keyed(fn):
                tx.ptt_press(args.settle)
                ev("keyed (after settle)", settle_s=args.settle)
                t0 = time.monotonic()
                try:
                    return fn()
                finally:
                    tx.ptt_release()
                    ev("unkeyed")
                    doc["key_down_s"] += time.monotonic() - t0 + args.settle

            def retune_latency():
                """With the tone keyed: sit on the upper channel, record 3 s, and tune to
                the wanted channel 1 s in. Returns when the tone appeared after the FA."""
                import threading
                tune(CHANNELS["upper"])
                res = {}
                th = threading.Thread(target=lambda: res.update(x=record(dev, 3.0)))
                t_rec = time.monotonic()
                th.start()
                time.sleep(1.0)
                t_fa = time.monotonic()
                set_fa(rx, fa + offset_hz)
                th.join()
                env = envelope_db(res["x"], 0.01)
                final = np.median(env[-50:])
                k = int(np.argmax(env > final - 3.0))
                # sample k was captured at about t_rec + 0.1 + k * 0.01 (record() drops its
                # first 100 ms); parecord's own start-up delay makes this an underestimate
                lat = k * 0.01 + 0.1 - (t_fa - t_rec)
                ev("retune latency", latency_s=round(lat, 3))
                return {"latency_s": lat, "env10ms_db": env, "fa_at_s": t_fa - t_rec - 0.1}

            def show(label, ch):
                w = ch.get("wanted", {}).get("power")
                parts = []
                for k, v in ch.items():
                    rel = 10 * np.log10(v["power"] / w) if w else float("nan")
                    parts.append(f"{k} {10*np.log10(max(v['power'],1e-20)):.1f} dBFS"
                                 + (f" ({rel:+.1f} dB)" if k != "wanted" and w else ""))
                log(f"  {label}: " + ", ".join(parts)
                    + f"; wanted peak {20*np.log10(max(ch.get('wanted',{}).get('peak',1e-10),1e-10)):.1f} dBFS")

            if not args.no_refs:
                # Tone: frequency offset and the DR setting
                log("\n== Single 1 kHz test tone (TT1), CESSB off ==")
                set_cessb(tx, False, 0.3)
                set_tt(tx, 1)
                try:
                    def tone_cal():
                        nonlocal offset_hz
                        tune(0.0)
                        x = record(dev, 2.0)
                        for _ in range(3):           # clipped: drop DR 20 dB and retry
                            if np.max(np.abs(x)) < 0.9:
                                break
                            dr = int(query(rx, "DR;", r"DR(\d{3});").group(1))
                            rx.send(f"DR{max(1, dr // 10):03d};", expect_reply=False)
                            time.sleep(0.3)
                            x = record(dev, 2.0)
                        win = np.hanning(len(x))
                        sp = np.abs(np.fft.rfft(x * win))
                        fr = np.fft.rfftfreq(len(x), 1 / RATE)
                        k = int(np.argmax(sp))
                        # parabolic interpolation of the peak
                        a, b, c = np.log(sp[k - 1:k + 2] + 1e-30)
                        f_tone = fr[k] + 0.5 * (a - c) / (a - 2 * b + c) * (fr[1] - fr[0])
                        offset_hz = int(round(sign * (f_tone - 1000.0)))
                        if abs(offset_hz) > 100:
                            raise CatError(f"the 1 kHz tone came out at {f_tone:.1f} Hz: radio 1 "
                                           "is mistuned (or not hearing radio 2)")
                        pk = 20 * np.log10(np.max(np.abs(x)) + 1e-12)
                        dr = int(query(rx, "DR;", r"DR(\d{3});").group(1))
                        new = int(min(100, max(1, round(dr * 10 ** ((args.target_peak_dbfs - pk) / 20)))))
                        log(f"  tone at {f_tone:.1f} Hz -> radio 1 offset {offset_hz:+d} Hz; "
                            f"peak {pk:.1f} dBFS at DR{dr:03d} -> DR{new:03d}")
                        if new == 100 and pk + 20 * np.log10(100 / dr) < args.target_peak_dbfs - 10:
                            log("  WARNING: tone weak even at DR100; less receive attenuation?")
                        rx.send(f"DR{new:03d};", expect_reply=False)
                        time.sleep(0.2)
                        doc["tone_cal"] = dict(f_tone=f_tone, offset_hz=offset_hz, peak_dbfs=pk,
                                               dr_before=dr, dr=new)
                        chs = measure_channels(lambda name: 3.0)
                        doc["retune"] = retune_latency()
                        return chs
                    ch = keyed(tone_cal)
                finally:
                    set_tt(tx, tt0)
                doc["points"].append(dict(label="single tone", kind="tone", cessb_db=None,
                                          channels=ch))
                show("single tone", ch)
                env = np.asarray(ch["wanted"]["env_db"])
                log(f"  tone stability, wanted channel, 50 ms blocks over {len(env)*0.05:.1f} s: "
                    f"min {env.min()-np.median(env):+.2f}, max {env.max()-np.median(env):+.2f} dB "
                    f"re median; first block {env[0]-np.median(env):+.2f}, last {env[-1]-np.median(env):+.2f}")
                log(f"  retune latency (FA to tone within 3 dB): {doc['retune']['latency_s']*1000:.0f} ms "
                    f"(the script waits 1000 ms after each retune)")
                save()
                time.sleep(args.rest / 2)
                log("\n== Radio 1 floor (radio 2 unkeyed) ==")
                ch = measure_channels(lambda name: 3.0)
                doc["points"].append(dict(label="receiver floor", kind="rx_floor", cessb_db=None,
                                          channels=ch))
                show("receiver floor", ch)
                save()

            for c in settings:
                if c is None:
                    set_cessb(tx, False, 0.3)
                else:
                    set_cessb(tx, True, 0.3)
                    set_level_db(tx, c, pl0[1])
                label = "CESSB off" if c is None else f"CESSB {c} dB"
                log(f"\n== Speech, {label} ==")
                with ClipPlayer(args.wav, args.peak_v) as pl:
                    clip_s = pl.n / pl.rate
                    time.sleep(0.5)
                    loops = lambda name: clip_s * args.loops * (0.5 if "alt" in name else 1.0)  # noqa: E731
                    t0 = time.monotonic()
                    ch = keyed(lambda: measure_channels(loops))
                    st = pl.stat()
                doc["points"].append(dict(label=label, kind="speech", cessb_db=c, channels=ch,
                                          keyed_s=time.monotonic() - t0, player=st,
                                          clip_s=clip_s))
                show(label, ch)
                save()
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
                doc["tx_after"] = dict(fa=get_fa(tx), pc=get_power(tx), pr=get_cessb(tx),
                                       pl=get_pl(tx))
                doc["rx_after"] = dict(fa=get_fa(rx),
                                       dg=int(query(rx, "DG;", r"DG(\d);").group(1)),
                                       dr=int(query(rx, "DR;", r"DR(\d{3});").group(1)))
                log(f"After: TX {doc['tx_after']}, RX {doc['rx_after']}")
            except Exception as exc:  # noqa: BLE001
                log(f"WARNING: could not read the radios back: {exc}")
            try:
                source_volume(dev, vol0)
            except Exception as exc:  # noqa: BLE001
                log(f"WARNING: could not restore the capture volume ({vol0}): {exc}")
            save()
            log(f"\nKey-down {doc['key_down_s']:.0f} s. Saved {out_path}")
    return 0


def tx_safe(radio, cmd):
    radio.send(cmd, expect_reply=False)
    time.sleep(0.15)


if __name__ == "__main__":
    sys.exit(main())
