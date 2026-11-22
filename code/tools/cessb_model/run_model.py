#!/usr/bin/env python3
"""Run the Phoenix CESSB model and write results/ (tables, JSON, plots).

    python3 run_model.py --wav ~/Arduino/libraries/OpenAudio_ArduinoLibrary/utility/W9GR48.WAV

The W9GR48.WAV voice sample ships with the OpenAudio library (it is the clip
W9GR and W7PUA used); it is read in place, not copied into this repository.

Levels. Everything is normalised so the envelope ceiling is 1.0 and "drive 0 dB"
means the voice's highest linear peak at the 24 kHz node just touches the
ceiling. Drive N dB therefore means peaks N dB into clipping - W7PUA's "drive
ratio" 1.5 / 2.0 / 2.5 is 3.5 / 6.0 / 8.0 dB here.

"Talk-power gain" is the rise in average power at equal *measured* PEP: the
peak used is the true peak at the DAC rate, after the post-CESSB interpolators,
so any overshoot those regrow is charged against the variant.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np
from scipy import signal
from scipy.io import wavfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cessb  # noqa: E402
import phoenix_tx as tx  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_WAV = os.path.join(cessb.DEFAULT_OPENAUDIO, "utility", "W9GR48.WAV")
SKIP_S = 0.1                    # filter start-up transient left out of every measurement
DRIVES_DB = list(range(0, 9))
GAIN_COMPENSATE = [1.0, 1.4, 1.9, 2.2, 2.5]
GC_DEFAULT = {"Z": cessb.GC_Z, "C": cessb.GC_C}   # each variant's own default


# --------------------------------------------------------------------------
# Inputs
# --------------------------------------------------------------------------

def load_voice(path: str, fs: float) -> np.ndarray:
    rate, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x[:, 0]
    x /= 32768.0
    if rate != 48000:
        raise ValueError(f"{path}: expected 48 kHz, got {rate}")
    if fs == 192000.0:
        return signal.resample_poly(x, 4, 1)
    if fs == 176400.0:
        return signal.resample_poly(x, 147, 40)
    raise ValueError(fs)


def two_tone(fs: float, f1: float = 700.0, f2: float = 1900.0, seconds: float = 2.0) -> np.ndarray:
    n = np.arange(int(fs * seconds))
    return 0.25 * (np.sin(2 * np.pi * f1 * n / fs) + np.sin(2 * np.pi * f2 * n / fs))


# --------------------------------------------------------------------------
# Measurements
# --------------------------------------------------------------------------

def active_mask(z: np.ndarray, fs: float, floor_db: float = -30.0) -> np.ndarray:
    """Samples inside 20 ms frames whose RMS is within floor_db of the loudest frame."""
    frame = int(0.02 * fs)
    nf = len(z) // frame
    rms = np.sqrt(np.mean(np.abs(z[:nf * frame].reshape(nf, frame)) ** 2, axis=1))
    keep = rms > rms.max() * 10 ** (floor_db / 20)
    mask = np.zeros(len(z), dtype=bool)
    mask[:nf * frame] = np.repeat(keep, frame)
    return mask


def band_power(freqs: np.ndarray, psd: np.ndarray, lo: float, hi: float) -> float:
    sel = (freqs >= min(lo, hi)) & (freqs <= max(lo, hi))
    return float(psd[sel].sum())


def spectrum(z: np.ndarray, fs: float):
    nper = int(round(fs / 11.71875))    # ~11.7 Hz bins at both node and DAC rates
    f, p = signal.welch(z, fs=fs, nperseg=nper, return_onesided=False, window="hann")
    order = np.argsort(f)
    return f[order], p[order]


def measure(z24: np.ndarray, zdac: np.ndarray, flt: tx.TxFilters, sign: int,
            mask_dac: np.ndarray) -> dict:
    s24 = int(SKIP_S * flt.fs24)
    sd = int(SKIP_S * flt.fs)
    peak24 = float(np.abs(z24[s24:]).max())
    a = np.abs(zdac[sd:])
    peak = float(a.max())
    pavg = float(np.mean(a[mask_dac[sd:]] ** 2))
    f, p = spectrum(zdac[sd:], flt.fs)
    wanted = band_power(f, p, sign * 200, sign * 3000)
    opposite = band_power(f, p, -sign * 200, -sign * 3000)
    ooc = (band_power(f, p, sign * 3300, sign * 12000)
           + band_power(f, p, -sign * 3300, -sign * 12000))
    total = float(p.sum())
    return {
        # average power in the wanted sideband only, relative to PEP: the part of
        # the talk power a receiver tuned to this signal actually hears
        "wanted_norm": (wanted / total) * float(np.mean(a ** 2)) / peak ** 2,
        "peak_node": peak24,
        "peak_dac": peak,
        "papr_db": 10 * np.log10(peak ** 2 / pavg),
        "opposite_dbc": 10 * np.log10(opposite / wanted),
        "out_of_channel_dbc": 10 * np.log10(ooc / wanted),
    }


def tone_level(z: np.ndarray, fs: float, f_hz: float) -> float:
    k = np.arange(len(z))
    w = np.hanning(len(z))
    return float(abs(np.sum(w * z * np.exp(-2j * np.pi * f_hz * k / fs))) / w.sum())


# --------------------------------------------------------------------------
# Experiments
# --------------------------------------------------------------------------

def apply_variant(name: str, z_in: np.ndarray, flt: tx.TxFilters, sign: int,
                  gc: float, h_z: np.ndarray, h_c: np.ndarray):
    """Return (node output, DAC output) for one variant."""
    if name == "linear":
        z24 = z_in
        return z24, tx.cessb_node_to_dac(z24, flt)
    if name == "q15_overload":
        return z_in, cessb.q15_overload(tx.cessb_node_to_dac(z_in, flt))
    if name == "Z":
        z24 = cessb.cessb_z(z_in, gc, h_z)
    elif name == "C":
        z24 = cessb.cessb_c(z_in, flt.fs24, sign, gc, h=h_c)
    else:
        raise ValueError(name)
    return z24, tx.cessb_node_to_dac(z24, flt)


def sweep_voice(x_fs: np.ndarray, flt: tx.TxFilters, sign: int, h_z, h_c,
                drives=DRIVES_DB, gcs=GAIN_COMPENSATE):
    z_lin = tx.mic_to_cessb_node(x_fs, flt)
    s24 = int(SKIP_S * flt.fs24)
    z_norm = z_lin / np.abs(z_lin[s24:]).max()
    ref_dac = tx.cessb_node_to_dac(z_norm, flt)
    mask = active_mask(ref_dac, flt.fs)
    ref = measure(z_norm, ref_dac, flt, sign, mask)

    rows = []
    for d in drives:
        z_in = z_norm * 10 ** (d / 20)
        runs = [("linear", None), ("q15_overload", None)]
        runs += [("Z", g) for g in gcs] + [("C", g) for g in gcs]
        for name, gc in runs:
            z24, zdac = apply_variant(name, z_in, flt, sign, gc or 1.0, h_z, h_c)
            m = measure(z24, zdac, flt, sign, mask)
            m.update({"variant": name, "drive_db": d, "gain_compensate": gc,
                      "talk_power_gain_db": ref["papr_db"] - m["papr_db"],
                      "wanted_talk_power_gain_db": 10 * np.log10(m["wanted_norm"] / ref["wanted_norm"]),
                      "overshoot_node_pct": 100 * (m["peak_node"] - 1),
                      "overshoot_dac_pct": 100 * (m["peak_dac"] - 1)})
            rows.append(m)
    return ref, rows, z_norm, mask


def two_tone_test(flt: tx.TxFilters, sign: int, h_z, h_c, drives=(0, 3, 6)):
    z_lin = tx.mic_to_cessb_node(two_tone(flt.fs), flt)
    s24 = int(0.5 * flt.fs24)          # longer settle: steady state is what matters here
    z_norm = z_lin / np.abs(z_lin[s24:]).max()
    rows = []
    for d in drives:
        z_in = z_norm * 10 ** (d / 20)
        for name in ("linear", "Z", "C"):
            z24, _ = apply_variant(name, z_in, flt, sign, GC_DEFAULT.get(name, 1.0), h_z, h_c)
            z = z24[s24:]
            tone = 0.5 * (tone_level(z, flt.fs24, sign * 700) + tone_level(z, flt.fs24, sign * 1900))
            imd_lo = tone_level(z, flt.fs24, sign * (2 * 700 - 1900))     # lands in the opposite sideband
            imd_hi = tone_level(z, flt.fs24, sign * (2 * 1900 - 700))     # just past the passband edge
            imd5_lo = tone_level(z, flt.fs24, sign * (3 * 700 - 2 * 1900))
            rows.append({
                "variant": name, "drive_db": d,
                "peak_node": float(np.abs(z).max()),
                "imd3_low_dbc": 20 * np.log10(imd_lo / tone),
                "imd3_high_dbc": 20 * np.log10(imd_hi / tone),
                "imd5_low_dbc": 20 * np.log10(imd5_lo / tone),
                "f_imd3_low_hz": 2 * 700 - 1900, "f_imd3_high_hz": 2 * 1900 - 700,
            })
    return rows


def transparent_passband(flt: tx.TxFilters, sign: int, h_z, h_c):
    """-3 dB edges of the whole chain with each variant driven 12 dB below the ceiling."""
    out = {}
    freqs = np.arange(100.0, 3600.0, 25.0)
    n = np.arange(int(0.25 * flt.fs))
    for name in ("linear", "Z", "C"):
        resp = []
        for fr in freqs:
            z_lin = tx.mic_to_cessb_node(0.1 * np.cos(2 * np.pi * fr * n / flt.fs), flt)
            z24, _ = apply_variant(name, z_lin, flt, sign, GC_DEFAULT.get(name, 1.0), h_z, h_c)
            resp.append(tone_level(z24[len(z24) // 2:], flt.fs24, sign * fr))
        resp = np.array(resp)
        db = 20 * np.log10(resp / resp[np.argmin(abs(freqs - 1000))])
        above = np.where(db >= -3)[0]
        out[name] = {"low_3db_hz": float(freqs[above[0]]), "high_3db_hz": float(freqs[above[-1]]),
                     "freqs": freqs.tolist(), "db": db.tolist()}
    return out


def measured_delay_ms(flt: tx.TxFilters, sign: int, h_z, h_c) -> dict:
    rng = np.random.default_rng(1)
    x = signal.lfilter(*signal.butter(4, 2500 / (flt.fs24 / 2)), rng.standard_normal(int(flt.fs24)))
    z = tx.audio24_to_cessb_node(x, flt)
    z *= 0.25 / np.abs(z[1000:]).max()               # well below the ceiling: linear
    out = {}
    for name in ("Z", "C"):
        y, _ = apply_variant(name, z, flt, sign, GC_DEFAULT[name], h_z, h_c)
        c = np.abs(signal.correlate(y, z, mode="full", method="fft"))
        lag = int(np.argmax(c)) - (len(z) - 1)
        out[name] = {"samples": lag, "ms": 1000 * lag / flt.fs24}
    return out


def cpu_estimate(h_len: int, n_block: int = 256) -> dict:
    """MACs per TransmitProcessing() call for the CESSB stage (one 256-sample block at Fs/8)."""
    fir = 4 * h_len * n_block                  # two filters, I and Q each
    per_sample = 30                            # |z|, sqrt, divide, 3-way max, delay lines, (+mix for C)
    return {"fir_macs": fir, "other_ops": per_sample * n_block,
            "us_at_1_mac_per_cycle_600MHz": (fir + per_sample * n_block) / 600.0}


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def plot_all(outdir, flt, sign, rows, z_norm, h_z, h_c, tt_rows, passband):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    def pick(variant, gc=None, drive=None):
        return [r for r in rows if r["variant"] == variant
                and (gc is None or r["gain_compensate"] == gc)
                and (drive is None or r["drive_db"] == drive)]

    # 1. Talk power and overshoot against drive
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))
    for v, gc, style in (("Z", cessb.GC_Z, "o-"), ("C", cessb.GC_C, "s-"), ("q15_overload", None, "x--")):
        r = pick(v, gc)
        lab = v if gc is None else f"{v} (gc {gc})"
        ax[0].plot([q["drive_db"] for q in r], [q["wanted_talk_power_gain_db"] for q in r], style, label=lab)
        ax[1].plot([q["drive_db"] for q in r], [q["overshoot_dac_pct"] for q in r], style, label=lab)
    ax[0].set(xlabel="drive into clipper (dB)", ylabel="wanted-sideband power gain at equal PEP (dB)",
              title="Average power gained")
    ax[1].set(xlabel="drive into clipper (dB)", ylabel="peak above ceiling at DAC (%)",
              title="PEP overshoot after interpolators")
    ax[1].set_ylim(-2, 25)
    for a in ax:
        a.grid(True, alpha=0.3)
        a.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "gain_vs_drive.png"), dpi=110)
    plt.close(fig)

    # 2. gainCompensate sweep at 6 dB drive
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))
    for v, style in (("Z", "o-"), ("C", "s-")):
        r = pick(v, drive=6)
        ax[0].plot([q["gain_compensate"] for q in r], [q["wanted_talk_power_gain_db"] for q in r], style, label=v)
        ax[1].plot([q["gain_compensate"] for q in r], [q["overshoot_dac_pct"] for q in r], style, label=v)
    ax[0].set(xlabel="gainCompensate", ylabel="talk-power gain (dB)", title="Drive 6 dB")
    ax[1].set(xlabel="gainCompensate", ylabel="overshoot at DAC (%)", title="Drive 6 dB")
    for a in ax:
        a.grid(True, alpha=0.3)
        a.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "gain_compensate_sweep.png"), dpi=110)
    plt.close(fig)

    # 3. Voice spectra at 6 dB drive
    fig, ax = plt.subplots(figsize=(11, 4.5))
    z6 = z_norm * 10 ** (6 / 20)
    sd = int(SKIP_S * flt.fs)
    for name, zin, lab in (("linear", z_norm, "linear, drive 0 dB (today, not overdriven)"),
                           ("q15_overload", z6, "today overdriven 6 dB (I/Q saturate)"),
                           ("Z", z6, "CESSB Z, 6 dB"), ("C", z6, "CESSB centred, 6 dB")):
        _, zdac = apply_variant(name, zin, flt, sign, GC_DEFAULT.get(name, 1.0), h_z, h_c)
        f, p = spectrum(zdac[sd:], flt.fs)
        ax.plot(sign * f / 1000, 10 * np.log10(p / p.max()), lw=0.8, label=lab)
    ax.set(xlim=(-8, 8), ylim=(-110, 5), xlabel="frequency re carrier, wanted sideband positive (kHz)",
           ylabel="PSD (dB re peak)", title="W9GR voice at the DAC")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "voice_spectrum.png"), dpi=110)
    plt.close(fig)

    # 4. Envelope snippet
    fig, ax = plt.subplots(figsize=(11, 3.5))
    t0 = int(np.argmax(np.abs(z_norm))) - int(0.025 * flt.fs24)
    seg = slice(t0, t0 + int(0.05 * flt.fs24))
    zc, _ = apply_variant("C", z6, flt, sign, cessb.GC_C, h_z, h_c)
    d = (len(h_c) - 1) // 2 + 2
    t = 1000 * np.arange(seg.stop - seg.start) / flt.fs24
    ax.plot(t, np.abs(z6[seg]), lw=0.8, label="input envelope, 6 dB drive")
    ax.plot(t, np.abs(zc[seg.start + d:seg.stop + d]), lw=1.0, label="CESSB centred output")
    ax.axhline(1.0, color="k", ls="--", lw=0.8)
    ax.set(xlabel="ms", ylabel="envelope (ceiling = 1)", title="Envelope around the loudest peak")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "envelope.png"), dpi=110)
    plt.close(fig)

    # 5. Transparent passband
    fig, ax = plt.subplots(figsize=(8, 4))
    for name, pb in passband.items():
        ax.plot(pb["freqs"], pb["db"], label=f"{name}: -3 dB {pb['low_3db_hz']:.0f}-{pb['high_3db_hz']:.0f} Hz")
    ax.set(ylim=(-20, 3), xlabel="Hz", ylabel="dB re 1 kHz", title=f"Chain passband below the ceiling, {flt.fs/1000:g} ksps")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(outdir, f"passband_{int(flt.fs)}.png"), dpi=110)
    plt.close(fig)


def write_markdown(path, results):
    L = []
    w = L.append
    w("# CESSB model results\n")
    w("Generated by `run_model.py`. Ceiling = 1.0; drive 0 dB = the voice's highest linear peak "
      "touches the ceiling. Talk-power gain is average-power gain at equal *measured* PEP "
      "(true peak at the DAC rate); the 'wanted SB' column counts only power in the wanted "
      "sideband, 200-3000 Hz. Spectra are taken at the DAC rate (two-sided, I + jQ). "
      f"Z runs at gainCompensate {cessb.GC_Z} (W7PUA), C at {cessb.GC_C} (Hershberger) "
      f"with a {cessb.CENTRED_ASTOP_DB:g} dB, +/-{cessb.HALF_BW_HZ:g} Hz filter centred on {cessb.CENTRE_HZ:g} Hz.\n")
    for rate, r in results["rates"].items():
        w(f"\n## {rate} ({r['fs24']:.0f} Hz node)\n")
        w(f"Reference (linear, drive 0): PAPR {r['reference']['papr_db']:.2f} dB, "
          f"opposite sideband {r['reference']['opposite_dbc']:.1f} dBc, "
          f"out of channel {r['reference']['out_of_channel_dbc']:.1f} dBc.\n")
        w("\n### Voice, each variant at its default gainCompensate\n")
        w("| drive dB | variant | talk-power gain dB | wanted SB gain dB | overshoot node % | overshoot DAC % | opposite SB dBc | out of channel dBc |")
        w("|---:|---|---:|---:|---:|---:|---:|---:|")
        for row in r["voice"]:
            if row["variant"] in ("Z", "C") and row["gain_compensate"] != GC_DEFAULT[row["variant"]]:
                continue
            if row["variant"] == "linear":
                continue
            w(f"| {row['drive_db']} | {row['variant']} | {row['talk_power_gain_db']:.2f} | "
              f"{row['wanted_talk_power_gain_db']:.2f} | "
              f"{row['overshoot_node_pct']:.1f} | {row['overshoot_dac_pct']:.1f} | "
              f"{row['opposite_dbc']:.1f} | {row['out_of_channel_dbc']:.1f} |")
        w("\n### gainCompensate sweep, drive 6 dB\n")
        w("| variant | gc | wanted SB gain dB | overshoot DAC % | opposite SB dBc | out of channel dBc |")
        w("|---|---:|---:|---:|---:|---:|")
        for row in r["voice"]:
            if row["variant"] in ("Z", "C") and row["drive_db"] == 6:
                w(f"| {row['variant']} | {row['gain_compensate']} | {row['wanted_talk_power_gain_db']:.2f} | "
                  f"{row['overshoot_dac_pct']:.1f} | {row['opposite_dbc']:.1f} | {row['out_of_channel_dbc']:.1f} |")
        if "two_tone" in r:
            w("\n### Two tone, 700 + 1900 Hz, default gc (levels re one tone)\n")
            w("| drive dB | variant | peak | IMD3 -500 Hz (opposite SB) dBc | IMD3 3100 Hz dBc | IMD5 -1700 Hz dBc |")
            w("|---:|---|---:|---:|---:|---:|")
            for row in r["two_tone"]:
                w(f"| {row['drive_db']} | {row['variant']} | {row['peak_node']:.3f} | {row['imd3_low_dbc']:.1f} | "
                  f"{row['imd3_high_dbc']:.1f} | {row['imd5_low_dbc']:.1f} |")
        w("\n### Passband with the stage below the ceiling\n")
        w("| variant | -3 dB low Hz | -3 dB high Hz |")
        w("|---|---:|---:|")
        for name, pb in r["passband"].items():
            w(f"| {name} | {pb['low_3db_hz']:.0f} | {pb['high_3db_hz']:.0f} |")
        w("\n### Added delay\n")
        for name, d in r["delay"].items():
            w(f"- {name}: {d['samples']} samples = {d['ms']:.2f} ms")
    w("\n## CPU estimate per TransmitProcessing() call\n")
    c = results["cpu"]
    w(f"{c['fir_macs']} FIR MACs + ~{c['other_ops']} other ops: about "
      f"{c['us_at_1_mac_per_cycle_600MHz']:.0f} us at one MAC per cycle, 600 MHz, "
      "per 10.7 ms block.\n")
    with open(path, "w") as fh:
        fh.write("\n".join(L) + "\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wav", default=DEFAULT_WAV)
    ap.add_argument("--out", default=os.path.join(HERE, "results"))
    ap.add_argument("--rates", nargs="+", default=["192k", "176k"], choices=list(tx.RATES))
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)

    h_z = cessb.w7pua_clipper_filter()
    results = {"rates": {}, "cpu": cpu_estimate(len(h_z))}
    for i, rate in enumerate(args.rates):
        flt = tx.TxFilters.for_rate(tx.RATES[rate])
        sign = tx.wanted_sign(flt)
        h_c = cessb.centred_filter(flt.fs24)
        print(f"[{rate}] voice sweep ...", flush=True)
        full = i == 0
        ref, rows, z_norm, _ = sweep_voice(load_voice(args.wav, flt.fs), flt, sign, h_z, h_c,
                                           drives=DRIVES_DB if full else [6],
                                           gcs=GAIN_COMPENSATE if full else sorted(set(GC_DEFAULT.values())))
        r = {"fs24": flt.fs24, "wanted_sign": sign, "reference": ref, "voice": rows}
        print(f"[{rate}] passband, delay ...", flush=True)
        r["passband"] = transparent_passband(flt, sign, h_z, h_c)
        r["delay"] = measured_delay_ms(flt, sign, h_z, h_c)
        if full:
            print(f"[{rate}] two tone, plots ...", flush=True)
            r["two_tone"] = two_tone_test(flt, sign, h_z, h_c)
            plot_all(args.out, flt, sign, rows, z_norm, h_z, h_c, r["two_tone"], r["passband"])
        results["rates"][rate] = r

    with open(os.path.join(args.out, "results.json"), "w") as fh:
        json.dump(results, fh, indent=1, default=float)
    write_markdown(os.path.join(args.out, "results.md"), results)
    print(f"wrote {args.out}/results.md")


if __name__ == "__main__":
    main()
