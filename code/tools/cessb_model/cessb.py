"""CESSB variants for the Phoenix 24 kHz transmit node, plus the overload baseline.

All of these act on the complex I + jQ the chain has after TXInterpolateBy2Again
(phoenix_tx.mic_to_cessb_node), with the envelope ceiling normalised to 1.0.

* ``cessb_z``: a faithful vectorised port of the clipper and overshoot controller
  of W7PUA's radioCESSB_Z_transmit_F32.cpp (lines 155-221). The signal sits at
  0..3 kHz on one side of DC and the filters are real lowpasses, so they pass
  -3..+3 kHz: clipping products that land in the *opposite* sideband are not
  removed.
* ``cessb_c``: "centred". Shift the SSB channel down by its centre frequency so it
  straddles DC, run the same clipper and overshoot controller with a lowpass of
  half the channel width, then shift back. The envelope is unchanged by a
  frequency shift, so the clipping decisions are identical to Z; only the filters
  now cut both edges of the channel - which is what Hershberger's Weaver
  formulation gets for free. The output is still zero-IF.
* ``q15_overload``: what the firmware does today when driven past full scale -
  arm_float_to_q15 saturating I and Q independently (PlayIQData, DSP.cpp:1096).

The overshoot controller is all feed-forward (the envelope it looks at comes from
the filtered clipper output, not from its own output), so everything vectorises.
"""

from __future__ import annotations

import os

import numpy as np
from scipy import signal

from phoenix_tx import calc_fir_lowpass, normalize_dc, parse_c_array

DEFAULT_OPENAUDIO = os.path.expanduser("~/Arduino/libraries/OpenAudio_ArduinoLibrary")
W7PUA_HEADER = "radioCESSB_Z_transmit_F32.h"

CENTRE_HZ = 1500.0       # channel centre; with HALF_BW_HZ the -6 dB edges are 50 and 2950 Hz
HALF_BW_HZ = 1450.0      # 3050 Hz upper edge let the 2f2-f1 two-tone product through at -18 dBc
CENTRED_ASTOP_DB = 40.0  # like W7PUA's 40.5 dB table: a gentler filter rings (overshoots) less
GC_Z = 1.4               # W7PUA's choice for the Z class
GC_C = 1.9               # Hershberger's value; the centred form behaves like his Weaver one


def w7pua_clipper_filter(openaudio_dir: str = DEFAULT_OPENAUDIO) -> np.ndarray:
    """The 123-tap clipperOut table (24 ksps, pass 2800 Hz, stop 3200 Hz at 40.5 dB)."""
    return parse_c_array(os.path.join(openaudio_dir, W7PUA_HEADER), "clipperOut")


def envelope_clip(z: np.ndarray, ceiling: float = 1.0) -> np.ndarray:
    mag = np.abs(z)
    scale = np.where(mag > ceiling, ceiling / np.maximum(mag, 1e-30), 1.0)
    return z * scale


def _delay(x: np.ndarray, d: int) -> np.ndarray:
    out = np.zeros_like(x)
    if d < len(x):
        out[d:] = x[:len(x) - d]
    return out


def clip_and_control(z: np.ndarray, h: np.ndarray, gain_compensate: float) -> np.ndarray:
    """Clip, filter, overshoot-control, as in radioCESSB_Z_transmit_F32::update().

    W7PUA's code with a 123-tap filter (group delay 61):
      y        = h * clip(x)
      eMax[k]  = max(|y[k]|, |y[k-1]|, |y[k-2]|, 1)
      diff[k]  = y[k-2] - y[k-2] / eMax[k]
      out[k]   = y[k-63] - gc * (h * diff)[k]
    The 63 is 2 + the filter's group delay, generalised here for any odd length.
    """
    if len(h) % 2 != 1:
        raise ValueError("overshoot controller needs a linear-phase odd-length filter")
    gd = (len(h) - 1) // 2
    y = signal.lfilter(h, 1.0, envelope_clip(z))
    env = np.abs(y)
    e_max = np.maximum.reduce([env, _delay(env, 1), _delay(env, 2), np.ones_like(env)])
    y2 = _delay(y, 2)
    diff = y2 - y2 / e_max
    return _delay(y, 2 + gd) - gain_compensate * signal.lfilter(h, 1.0, diff)


def cessb_z(z: np.ndarray, gain_compensate: float = GC_Z, h: np.ndarray | None = None) -> np.ndarray:
    return clip_and_control(z, w7pua_clipper_filter() if h is None else h, gain_compensate)


def centred_filter(fs: float, half_bw_hz: float = HALF_BW_HZ, taps: int = 123,
                   astop_db: float = CENTRED_ASTOP_DB) -> np.ndarray:
    """Lowpass for the centred variant, from the firmware's own CalcFIRCoeffs port.

    Designed in Hz at the node rate, so like FIR_int3 it holds its corners when
    the radio runs at 176.4 ksps. CalcFIRCoeffs centres an odd-length design on a
    half sample, which would misalign the overshoot controller's delay line, so
    design one tap longer and drop the first: the rest is exactly symmetric.
    This is what InitializeCESSB() in DSP_CESSB.cpp does.
    """
    return normalize_dc(calc_fir_lowpass(taps + 1, half_bw_hz, astop_db, fs)[1:])


def cessb_c(z: np.ndarray, fs: float, sign: int, gain_compensate: float = GC_C,
            centre_hz: float = CENTRE_HZ, h: np.ndarray | None = None) -> np.ndarray:
    """Centred CESSB. ``sign`` is the side of DC the wanted audio occupies (+1 or -1)."""
    if h is None:
        h = centred_filter(fs)
    n = np.arange(len(z))
    lo = np.exp(-1j * 2 * np.pi * sign * centre_hz * n / fs)
    return clip_and_control(z * lo, h, gain_compensate) * np.conj(lo)


def q15_overload(z_dac: np.ndarray, ceiling: float = 1.0) -> np.ndarray:
    """Independent I and Q saturation, as arm_float_to_q15 does at the DAC."""
    return np.clip(z_dac.real, -ceiling, ceiling) + 1j * np.clip(z_dac.imag, -ceiling, ceiling)
