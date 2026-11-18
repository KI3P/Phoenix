"""Offline model of the Phoenix SSB transmit DSP chain, TransmitProcessing() in DSP.cpp.

Built from the firmware's own filter coefficients, not look-alikes:

* The fixed tables (the 192k/48k decimate/interpolate lowpasses and the 100-tap
  +/-45 degree Hilbert pair) are parsed straight out of DSP_FIR.cpp.
* The two stages generated at run time (TX_DECIMATE3_FC_HZ and TX_AUDIO_LPF_FC_HZ)
  are recomputed with a line-for-line port of CalcFIRCoeffs() and
  NormalizeFIRDCGain(), at Fs/8 exactly as InitializeTransmitFilterCoeffs() does.

Not modelled:

* BandEQ. It is a linear stage that only reshapes the voice spectrum before the
  Hilbert pair; the model treats it as flat. The firmware's default EQ is not flat
  (about 1.9 dB of tilt, see wiki/firmware/dsp-chain.md), so absolute tone
  amplitudes differ from mic_tx_passband.csv - shapes and corners do not.
* TXGain, IQ correction and sideband selection. All three are linear, per-sample
  scalings or sign flips that do not change the envelope, so they cannot change
  any CESSB measurement here.
* The codec, the analog exciter and the PA.

The rate-changing stages follow the CMSIS semantics the firmware relies on:
arm_fir_decimate_f32 filters then keeps every M-th output, and
arm_fir_interpolate_f32 is zero-stuffing followed by the FIR, which the firmware
then rescales by L (TXInterpolateBy*).
"""

from __future__ import annotations

import math
import os
import re
from dataclasses import dataclass

import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DSP_FIR_CPP = os.path.join(REPO, "code", "src", "PhoenixSketch", "DSP_FIR.cpp")

# DSP_FIR.cpp constants (kept in step by test_cessb_model.TestConstantsMatchFirmware)
TX_DECIMATE3_FC_HZ = 3500.0
TX_AUDIO_LPF_FC_HZ = 3039.6
GENERATED_FIR_ATTENUATION_DB = 90.0
DECIMATION_FACTOR = 8          # RXfilters.DF: the generated TX stages are designed at Fs/8

RATES = {"192k": 192000.0, "176k": 176400.0}


# --------------------------------------------------------------------------
# Coefficient sources
# --------------------------------------------------------------------------

def parse_c_array(path: str, name: str) -> np.ndarray:
    """Return the float initialiser list of ``name[...] = { ... };`` in a C/C++ file."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    # Strip comments so numbers inside them are not picked up.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    m = re.search(re.escape(name) + r"\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", text, flags=re.S)
    if not m:
        raise KeyError(f"{name} not found in {path}")
    body = m.group(1).replace("f", "").replace("F", "")
    values = [float(v) for v in re.split(r"[,\s]+", body) if v.strip()]
    return np.asarray(values, dtype=np.float64)


def izero(x: float) -> float:
    """Port of Izero() in DSP_FIR.cpp: modified Bessel function of the first kind, order 0."""
    x2 = x / 2.0
    summe, ds, di = 1.0, 1.0, 1.0
    while True:
        tmp = (x2 / di) ** 2
        ds *= tmp
        summe += ds
        di += 1.0
        if ds < 1e-9 * summe:
            return summe


def msinc(m: int, fc: float) -> float:
    """Port of MSinc() in DSP_FIR.cpp."""
    if m == 0:
        return 1.0
    x = m * math.pi / 2.0
    return math.sin(x * fc) / (fc * x)


def calc_fir_lowpass(num_coeffs: int, fc_hz: float, astop_db: float, fs_hz: float) -> np.ndarray:
    """Port of CalcFIRCoeffs(..., Lowpass, ...) in DSP_FIR.cpp. ``fc_hz`` is the -6 dB point."""
    if astop_db < 20.96:
        beta = 0.0
    elif astop_db >= 50.0:
        beta = 0.1102 * (astop_db - 8.71)
    else:
        beta = 0.5842 * (astop_db - 20.96) ** 0.4 + 0.07886 * (astop_db - 20.96)
    izb = izero(beta)
    fcf = 2.0 * fc_hz / fs_hz
    nc = num_coeffs
    out = np.zeros(num_coeffs)
    for jj, ii in enumerate(range(-nc, nc, 2)):
        x = ii / nc
        w = izero(beta * math.sqrt(max(0.0, 1.0 - x * x))) / izb
        out[jj] = fcf * msinc(ii, fcf) * w
    return out


def normalize_dc(taps: np.ndarray) -> np.ndarray:
    """Port of NormalizeFIRDCGain()."""
    s = taps.sum()
    return taps / s if s != 0 else taps


# --------------------------------------------------------------------------
# Rate changers with CMSIS semantics
# --------------------------------------------------------------------------

def fir_decimate(x: np.ndarray, h: np.ndarray, m: int) -> np.ndarray:
    return signal.lfilter(h, 1.0, x)[m - 1::m]


def fir_interpolate(x: np.ndarray, h: np.ndarray, l: int) -> np.ndarray:
    """Zero-stuff by L, filter, and apply the firmware's xL make-up gain."""
    up = np.zeros(len(x) * l, dtype=np.result_type(x, np.float64))
    up[::l] = x
    return l * signal.lfilter(h, 1.0, up)


# --------------------------------------------------------------------------
# The chain
# --------------------------------------------------------------------------

@dataclass
class TxFilters:
    fs: float
    dec1: np.ndarray      # coeffs192K_10K_LPF_FIR, also int2
    dec2: np.ndarray      # coeffs48K_8K_LPF_FIR, also int1
    dec3: np.ndarray      # coeffs12K_8K_LPF_FIR, generated
    hilb_p45: np.ndarray  # FIR_Hilbert_coeffs_45
    hilb_n45: np.ndarray  # FIR_Hilbert_coeffs_neg_45
    int3: np.ndarray      # FIR_int3_12ksps_48tap_2k7, generated

    @property
    def fs24(self) -> float:
        """Rate of the CESSB insertion point (24 kHz at 192 ksps, 22.05 kHz at 176.4 ksps)."""
        return self.fs / DECIMATION_FACTOR

    @classmethod
    def for_rate(cls, fs: float, dsp_fir_cpp: str = DSP_FIR_CPP) -> "TxFilters":
        audio_fs = fs / DECIMATION_FACTOR
        dec3 = normalize_dc(calc_fir_lowpass(48, TX_DECIMATE3_FC_HZ,
                                             GENERATED_FIR_ATTENUATION_DB, audio_fs))
        int3 = normalize_dc(calc_fir_lowpass(48, TX_AUDIO_LPF_FC_HZ,
                                             GENERATED_FIR_ATTENUATION_DB, audio_fs))
        return cls(
            fs=fs,
            dec1=parse_c_array(dsp_fir_cpp, "coeffs192K_10K_LPF_FIR"),
            dec2=parse_c_array(dsp_fir_cpp, "coeffs48K_8K_LPF_FIR"),
            dec3=dec3,
            hilb_p45=parse_c_array(dsp_fir_cpp, "FIR_Hilbert_coeffs_45"),
            hilb_n45=parse_c_array(dsp_fir_cpp, "FIR_Hilbert_coeffs_neg_45"),
            int3=int3,
        )


def mic_to_cessb_node(x_fs: np.ndarray, f: TxFilters) -> np.ndarray:
    """Microphone samples at Fs -> complex I + jQ at the 24 kHz node.

    Mirrors DSP.cpp:1212-1224: TXDecimateBy4, TXDecimateBy2, [BandEQ: flat],
    Q := I, TXDecimateBy2Again, HilbertTransform, TXInterpolateBy2Again.
    """
    x = fir_decimate(x_fs, f.dec1, 4)
    x = fir_decimate(x, f.dec2, 2)
    return audio24_to_cessb_node(x, f)


def audio24_to_cessb_node(x24: np.ndarray, f: TxFilters) -> np.ndarray:
    """Real audio at Fs/8 (I == Q) -> complex I + jQ at Fs/8, after TXInterpolateBy2Again."""
    x12 = fir_decimate(x24, f.dec3, 2)
    i12 = signal.lfilter(f.hilb_p45, 1.0, x12)
    q12 = signal.lfilter(f.hilb_n45, 1.0, x12)
    return fir_interpolate(i12, f.int3, 2) + 1j * fir_interpolate(q12, f.int3, 2)


def cessb_node_to_dac(z24: np.ndarray, f: TxFilters) -> np.ndarray:
    """Complex I + jQ at Fs/8 -> Fs, via TXInterpolateBy2 and TXInterpolateBy4."""
    i = fir_interpolate(fir_interpolate(z24.real, f.dec2, 2), f.dec1, 4)
    q = fir_interpolate(fir_interpolate(z24.imag, f.dec2, 2), f.dec1, 4)
    return i + 1j * q


def wanted_sign(f: TxFilters) -> int:
    """+1 if the chain (before SidebandSelection) puts the audio at positive frequencies.

    Found by experiment rather than assumed, so a sign convention slip in the
    model cannot silently swap 'wanted' and 'opposite' in every measurement.
    """
    n = np.arange(8192)
    tone = np.cos(2 * np.pi * 1000.0 * n / f.fs24)
    z = audio24_to_cessb_node(tone, f)[2048:]
    k = np.arange(len(z))
    pos = abs(np.vdot(np.exp(2j * np.pi * 1000.0 * k / f.fs24), z))
    neg = abs(np.vdot(np.exp(-2j * np.pi * 1000.0 * k / f.fs24), z))
    return 1 if pos > neg else -1
