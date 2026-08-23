"""Analog Discovery 2 control for the transmit filter HIL suite.

The receive suite's :mod:`filter_hil.ad2` drives two AWG channels in quadrature
and reads one scope channel. The transmit rig is the mirror image: **one** AWG
channel into the microphone input, and **both** scope channels reading the
exciter's I and Q outputs at the same instant.

Capturing both channels synchronously is the whole point. The measurement of
interest is the complex signal ``I + jQ``, and its sideband - which side of DC
the transmitted energy sits on - is carried entirely in the phase between the two
channels. Two separate single-channel captures would have no defined phase
relationship, so opposite-sideband suppression could not be measured at all.

The ctypes binding, the SDK constants and the record-mode idiom are shared with
:mod:`filter_hil.ad2` rather than duplicated; only the configuration and the
capture loop are different.
"""

from __future__ import annotations

import atexit
import ctypes
import signal
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Callable, Optional

import numpy as np

from filter_hil.ad2 import (ACQMODE_RECORD, ANALOG_OUT_NODE_CARRIER,
                            DEFAULT_DWF_PATH, DWF_STATE_ARMED, DWF_STATE_CONFIG,
                            DWF_STATE_PREFILL, FUNC_SINE, HDWF_NONE, DwfError,
                            bind_argtypes, dwf_version, last_error, load_dwf)


@dataclass(frozen=True)
class TxAd2Config:
    """Scope and AWG settings that stay fixed for a whole run.

    ``scope_rate_hz`` deliberately defaults to something that is **not** a
    divisor of the radio's 192 kHz output rate. The exciter DAC leaves residual
    images around its own sample rate, and at 96 ksps an image at ``192000 - f``
    aliases to exactly ``-f``: straight onto the mirror frequency the sideband
    suppression measurement reads. At 100 ksps the same image lands at
    ``8000 + f``, harmlessly away from anything being measured.

    ``scope_offset_v`` centres the input window on the exciter output's DC bias,
    which sits near +1.6 V on this hardware. Getting it wrong does not clip
    quietly - it clips at one extreme of the range and the tone measurement goes
    with it.
    """

    scope_rate_hz: float = 100_000.0
    capture_s: float = 0.25
    scope_range_v: float = 5.0
    scope_offset_v: float = 1.6
    awg_offset_v: float = 0.0
    frontend_settle_s: float = 2.0
    dwf_path: str = DEFAULT_DWF_PATH


@dataclass
class IqCapture:
    """One synchronous block of both scope channels.

    The channels are named for the scope inputs, not for I and Q: which physical
    output landed on which input is discovered at run time and is not known when
    the capture is taken.
    """

    ch1: np.ndarray
    ch2: np.ndarray
    sample_rate_hz: float
    lost: int
    corrupted: int
    t_utc: str

    @property
    def clean(self) -> bool:
        """True when no samples were dropped or flagged corrupt."""
        return self.lost == 0 and self.corrupted == 0

    def analytic(self, swap: bool = False) -> np.ndarray:
        """The complex signal the transmit chain produced, DC removed.

        ``swap`` exchanges the two channels, which conjugates the result and so
        mirrors the spectrum about DC. That is exactly the ambiguity introduced by
        not knowing which scope input is on I, which is why it is a parameter
        here rather than a wiring requirement.

        The DC of each channel is subtracted because the firmware deliberately
        puts one there: ``PlayIQData`` adds ``ED.DCOffsetI``/``DCOffsetQ`` to null
        the transmitter's carrier. It is a real and interesting quantity, but it
        is not part of any filter's response, so it is measured separately by
        :meth:`dc` and taken out here.
        """
        a = self.ch2 if swap else self.ch1
        b = self.ch1 if swap else self.ch2
        return (a - np.mean(a)) + 1j * (b - np.mean(b))

    def dc(self) -> tuple[float, float]:
        """Mean level of each channel, volts. Carrier nulling lives here."""
        return float(np.mean(self.ch1)), float(np.mean(self.ch2))

    def peak(self) -> float:
        """Largest absolute sample on either channel, volts."""
        if self.ch1.size == 0:
            return 0.0
        return float(max(np.max(np.abs(self.ch1)), np.max(np.abs(self.ch2))))

    def excursion(self, centre_v: float) -> float:
        """Largest departure from centre_v on either channel, volts.

        This, not :meth:`peak`, is what decides whether the input clipped. The
        AD2's range is peak to peak and sits centred on the configured offset, so
        a signal riding on the exciter's +1.6 V bias runs out of window at
        ``offset +/- range/2`` - nowhere near ``range`` from zero.
        """
        if self.ch1.size == 0:
            return 0.0
        return float(max(np.max(np.abs(self.ch1 - centre_v)),
                         np.max(np.abs(self.ch2 - centre_v))))


class TxAd2:
    """Open AD2, drive W1 into the mic input, capture Ch1 and Ch2 together.

    Use as a context manager; the AWG is always silenced on the way out. Unlike
    the receive rig this device is not connected to anything that could be
    damaged by a stuck output, but a tone left running into the microphone would
    be transmitted by the next person to key the radio, so the same cleanup
    handlers are installed.
    """

    def __init__(self, cfg: TxAd2Config, log: Callable[[str], None] = print) -> None:
        self.cfg = cfg
        self.log = log
        self.dwf: Optional[ctypes.CDLL] = None
        self.hdwf = ctypes.c_int(HDWF_NONE)
        self._scope_configured = False
        self._prev_sigint = None
        self.version = ""
        self.captures = 0
        self.dirty_captures = 0

    # -- lifecycle ---------------------------------------------------------

    def __enter__(self) -> "TxAd2":
        self.open()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def open(self) -> None:
        """Open the first available device and put it in manual-configure mode."""
        self.dwf = load_dwf(self.cfg.dwf_path)
        bind_argtypes(self.dwf)
        self.version = dwf_version(self.dwf)
        self.log(f"WaveForms SDK {self.version}")

        if self.dwf.FDwfDeviceOpen(ctypes.c_int(-1), ctypes.byref(self.hdwf)) != 1 \
                or self.hdwf.value == HDWF_NONE:
            raise DwfError(f"could not open an Analog Discovery: {last_error(self.dwf)}")

        self.dwf.FDwfDeviceAutoConfigureSet(self.hdwf, ctypes.c_int(0))

        atexit.register(self._emergency_stop)
        self._prev_sigint = signal.getsignal(signal.SIGINT)
        signal.signal(signal.SIGINT, self._on_sigint)

    def close(self) -> None:
        """Silence the AWG and release the device."""
        if self.dwf is None:
            return
        try:
            self.awg_off()
        finally:
            self.dwf.FDwfDeviceCloseAll()
            self.hdwf = ctypes.c_int(HDWF_NONE)
            self.dwf = None
            atexit.unregister(self._emergency_stop)
            if self._prev_sigint is not None:
                signal.signal(signal.SIGINT, self._prev_sigint)
                self._prev_sigint = None

    def _emergency_stop(self) -> None:
        """Last-ditch AWG silence; safe to call more than once."""
        if self.dwf is None or self.hdwf.value == HDWF_NONE:
            return
        try:
            self.dwf.FDwfAnalogOutReset(self.hdwf, ctypes.c_int(-1))
            self.dwf.FDwfDeviceCloseAll()
        except Exception:
            pass

    def _on_sigint(self, signum, frame):
        self.log("\nInterrupted - silencing the AWG.")
        self._emergency_stop()
        raise KeyboardInterrupt

    # -- scope -------------------------------------------------------------

    def configure_scope(self) -> None:
        """Configure Ch1 and Ch2 for synchronous record-mode capture.

        Both channels take the same range and offset. They are measuring two
        halves of one quadrature pair, and a gain difference between them would
        appear as an amplitude imbalance - which is indistinguishable from poor
        sideband suppression in the radio.
        """
        assert self.dwf is not None
        d, h = self.dwf, self.hdwf
        c_int, c_double = ctypes.c_int, ctypes.c_double

        d.FDwfAnalogInReset(h)
        for ch in (0, 1):
            c = c_int(ch)
            d.FDwfAnalogInChannelEnableSet(h, c, c_int(1))
            d.FDwfAnalogInChannelRangeSet(h, c, c_double(self.cfg.scope_range_v))
            d.FDwfAnalogInChannelOffsetSet(h, c, c_double(self.cfg.scope_offset_v))
        d.FDwfAnalogInAcquisitionModeSet(h, c_int(ACQMODE_RECORD))
        d.FDwfAnalogInFrequencySet(h, c_double(self.cfg.scope_rate_hz))
        d.FDwfAnalogInRecordLengthSet(h, c_double(self.cfg.capture_s))
        d.FDwfAnalogInConfigure(h, c_int(1), c_int(0))  # apply, do not start

        self.log(f"Scope: Ch1+Ch2 @ {self.cfg.scope_rate_hz/1000:.1f} kHz, "
                 f"{self.cfg.capture_s*1000:.0f} ms, range {self.cfg.scope_range_v} V, "
                 f"offset {self.cfg.scope_offset_v:+.2f} V")
        self.log(f"Waiting {self.cfg.frontend_settle_s:.1f} s for the analog frontend "
                 f"to settle...")
        time.sleep(self.cfg.frontend_settle_s)
        self._scope_configured = True

    def capture(self) -> IqCapture:
        """Re-arm and stream one block from both channels.

        Both channels are read out at every status poll, with the same sample
        offset and the same count, so the two arrays stay sample-aligned. Lost
        samples advance the write position on both channels together: a dropout
        has to leave a hole rather than shift one channel in time relative to the
        other, which would look like a phase error and corrupt every sideband
        measurement in the run.
        """
        assert self.dwf is not None
        if not self._scope_configured:
            self.configure_scope()

        d, h = self.dwf, self.hdwf
        c_int, c_double = ctypes.c_int, ctypes.c_double

        n_samples = int(round(self.cfg.scope_rate_hz * self.cfg.capture_s))
        buf1 = (ctypes.c_double * n_samples)()
        buf2 = (ctypes.c_double * n_samples)()
        sts = ctypes.c_byte()
        avail, lost, corrupted = c_int(), c_int(), c_int()
        total_lost = total_corrupted = 0

        d.FDwfAnalogInConfigure(h, c_int(0), c_int(1))  # start

        got = 0
        deadline = time.monotonic() + self.cfg.capture_s * 5 + 5.0
        while got < n_samples:
            if time.monotonic() > deadline:
                raise DwfError("timed out waiting for the scope acquisition")

            if d.FDwfAnalogInStatus(h, c_int(1), ctypes.byref(sts)) != 1:
                raise DwfError(f"FDwfAnalogInStatus failed: {last_error(d)}")

            if got == 0 and sts.value in (DWF_STATE_CONFIG, DWF_STATE_PREFILL,
                                          DWF_STATE_ARMED):
                continue

            d.FDwfAnalogInStatusRecord(h, ctypes.byref(avail), ctypes.byref(lost),
                                       ctypes.byref(corrupted))
            got += lost.value
            total_lost += lost.value
            total_corrupted += corrupted.value

            if avail.value == 0:
                continue

            take = min(avail.value, n_samples - got)
            if take <= 0:
                break
            offset = ctypes.sizeof(ctypes.c_double) * got
            d.FDwfAnalogInStatusData(h, c_int(0), ctypes.byref(buf1, offset), c_int(take))
            d.FDwfAnalogInStatusData(h, c_int(1), ctypes.byref(buf2, offset), c_int(take))
            got += take

        self.captures += 1
        if total_lost or total_corrupted:
            self.dirty_captures += 1

        return IqCapture(
            ch1=np.frombuffer(buf1, dtype=np.float64).copy(),
            ch2=np.frombuffer(buf2, dtype=np.float64).copy(),
            sample_rate_hz=self.cfg.scope_rate_hz,
            lost=total_lost,
            corrupted=total_corrupted,
            t_utc=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        )

    def capture_after(self, settle_s: float) -> IqCapture:
        """Wait for the transmit chain to settle on the new tone, then capture."""
        time.sleep(settle_s)
        return self.capture()

    # -- AWG ---------------------------------------------------------------

    def set_tone(self, f_hz: float, amplitude_v: float) -> None:
        """Drive W1 with a sine into the microphone input.

        Only W1 is used. The transmit chain is fed from the microphone's left
        channel alone - ``TransmitProcessing`` overwrites Q with a copy of I
        immediately after the equaliser - so a second channel would contribute
        nothing.
        """
        assert self.dwf is not None
        d, h = self.dwf, self.hdwf
        c_int, c_double = ctypes.c_int, ctypes.c_double
        ch, node = c_int(0), c_int(ANALOG_OUT_NODE_CARRIER)

        d.FDwfAnalogOutNodeEnableSet(h, ch, node, c_int(1))
        d.FDwfAnalogOutNodeFunctionSet(h, ch, node, ctypes.c_ubyte(FUNC_SINE))
        d.FDwfAnalogOutNodeFrequencySet(h, ch, node, c_double(f_hz))
        d.FDwfAnalogOutNodeAmplitudeSet(h, ch, node, c_double(amplitude_v))
        d.FDwfAnalogOutNodeOffsetSet(h, ch, node, c_double(self.cfg.awg_offset_v))
        d.FDwfAnalogOutNodePhaseSet(h, ch, node, c_double(0.0))
        d.FDwfAnalogOutRunSet(h, ch, c_double(0.0))      # run continuously
        d.FDwfAnalogOutRepeatSet(h, ch, c_int(0))
        # 1 = start. Configure mode 3 applies to an already-running channel and
        # would silently leave a stopped one stopped.
        d.FDwfAnalogOutConfigure(h, ch, c_int(1))

    def awg_off(self) -> None:
        """Stop driving the microphone input."""
        if self.dwf is None or self.hdwf.value == HDWF_NONE:
            return
        self.dwf.FDwfAnalogOutReset(self.hdwf, ctypes.c_int(-1))
