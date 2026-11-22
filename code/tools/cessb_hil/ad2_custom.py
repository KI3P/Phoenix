"""The transmit rig's AD2, plus custom-waveform playback on W1.

:class:`tx_filter_hil.ad2.TxAd2` drives W1 with a sine. CESSB has to be tested
with signals whose envelope peaks - a two-tone, a vowel - so this adds the AWG's
custom mode: one period of the stimulus is loaded into the AWG buffer and
repeated continuously.
"""

from __future__ import annotations

import ctypes

import numpy as np

from filter_hil.ad2 import ANALOG_OUT_NODE_CARRIER, DwfError, last_error
from tx_filter_hil.ad2 import TxAd2

FUNC_CUSTOM = 30


class CessbAd2(TxAd2):
    """TxAd2 with :meth:`set_custom` for periodic arbitrary waveforms."""

    def open(self) -> None:
        super().open()
        d = self.dwf
        c_int = ctypes.c_int
        d.FDwfAnalogOutNodeDataInfo.argtypes = [c_int, c_int, c_int,
                                                 ctypes.POINTER(c_int), ctypes.POINTER(c_int)]
        d.FDwfAnalogOutNodeDataSet.argtypes = [c_int, c_int, c_int,
                                                ctypes.POINTER(ctypes.c_double), c_int]
        d.FDwfAnalogOutRunSet.argtypes = [c_int, c_int, ctypes.c_double]
        d.FDwfAnalogOutRepeatSet.argtypes = [c_int, c_int, c_int]
        d.FDwfAnalogOutConfigure.argtypes = [c_int, c_int, c_int]

    def custom_buffer_size(self) -> int:
        """Largest custom waveform the AWG holds in the opened configuration."""
        assert self.dwf is not None
        lo, hi = ctypes.c_int(), ctypes.c_int()
        self.dwf.FDwfAnalogOutNodeDataInfo(self.hdwf, ctypes.c_int(0),
                                           ctypes.c_int(ANALOG_OUT_NODE_CARRIER),
                                           ctypes.byref(lo), ctypes.byref(hi))
        return int(hi.value)

    def set_custom(self, samples: np.ndarray, period_hz: float, peak_v: float) -> None:
        """Play one period of ``samples`` (peak 1.0) on W1, ``period_hz`` times a second.

        ``peak_v`` is the waveform's peak in volts: the AWG multiplies the buffer,
        which must lie within -1..+1, by its amplitude setting.
        """
        assert self.dwf is not None
        x = np.asarray(samples, dtype=np.float64)
        if np.max(np.abs(x)) > 1.0 + 1e-9:
            raise ValueError("custom waveform must lie within -1..+1")
        n = len(x)
        cap = self.custom_buffer_size()
        if n > cap:
            raise DwfError(f"custom waveform of {n} samples exceeds the AWG buffer ({cap})")
        d, h = self.dwf, self.hdwf
        c_int, c_double = ctypes.c_int, ctypes.c_double
        ch, node = c_int(0), c_int(ANALOG_OUT_NODE_CARRIER)
        buf = (c_double * n)(*x)
        d.FDwfAnalogOutNodeEnableSet(h, ch, node, c_int(1))
        d.FDwfAnalogOutNodeFunctionSet(h, ch, node, ctypes.c_ubyte(FUNC_CUSTOM))
        if d.FDwfAnalogOutNodeDataSet(h, ch, node, buf, c_int(n)) != 1:
            raise DwfError(f"FDwfAnalogOutNodeDataSet failed: {last_error(d)}")
        d.FDwfAnalogOutNodeFrequencySet(h, ch, node, c_double(period_hz))
        d.FDwfAnalogOutNodeAmplitudeSet(h, ch, node, c_double(peak_v))
        d.FDwfAnalogOutNodeOffsetSet(h, ch, node, c_double(self.cfg.awg_offset_v))
        d.FDwfAnalogOutNodePhaseSet(h, ch, node, c_double(0.0))
        d.FDwfAnalogOutRunSet(h, ch, c_double(0.0))      # run continuously
        d.FDwfAnalogOutRepeatSet(h, ch, c_int(0))
        d.FDwfAnalogOutConfigure(h, ch, c_int(1))         # (re)start with the new data
