"""Analog Discovery 2 control for the filter HIL suite.

Wraps the WaveForms SDK (libdwf) through ctypes. There is no pydwf on this
machine and the SDK's own Python support is a constants module plus sample
scripts, so the binding is hand-written; the argtypes table and the record-mode
capture loop are lifted from ``code/tools/receive_chain_test.py``.

Two capabilities matter here:

* **Quadrature output.** W1 and W2 drive the radio's I and Q receive inputs. The
  two channels must be phase-locked and started atomically, or the phase between
  them is undefined and the radio sees a signal that is not analytic. Nothing
  else in this repo has driven the AD2's second AWG channel, let alone in
  quadrature, so this part has no prior art to copy.

* **Streaming capture.** Scope Ch1 reads the demodulated audio. Record mode is
  used rather than single-shot because the AD2's on-board buffer is only a few
  thousand samples; the loop below accounts for lost and corrupted samples so a
  capture that dropped data can be discarded rather than silently analysed.

The device is left driving the radio's ADC inputs if the process dies, so
:class:`Ad2` registers cleanup on ``__exit__``, on SIGINT and at interpreter
exit.
"""

from __future__ import annotations

import atexit
import ctypes
import signal
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Callable, Optional

import numpy as np

# --- SDK constants ---------------------------------------------------------
# Mirrored from /usr/share/digilent/waveforms/samples/py/dwfconstants.py rather
# than imported, so the suite does not depend on the SDK's sample directory
# being present at its default path.

HDWF_NONE = 0

ACQMODE_SINGLE = 0
ACQMODE_RECORD = 3

DWF_STATE_READY = 0
DWF_STATE_CONFIG = 4
DWF_STATE_PREFILL = 5
DWF_STATE_ARMED = 1
DWF_STATE_RUNNING = 3
DWF_STATE_DONE = 2

ANALOG_OUT_NODE_CARRIER = 0
ANALOG_OUT_NODE_FM = 1
ANALOG_OUT_NODE_AM = 2

FUNC_DC = 0
FUNC_SINE = 1

DEFAULT_DWF_PATH = "libdwf.so"


class DwfError(RuntimeError):
    """A WaveForms SDK call failed."""


def load_dwf(path: str = DEFAULT_DWF_PATH) -> ctypes.CDLL:
    """Load libdwf for the current platform."""
    if sys.platform.startswith("win"):
        return ctypes.cdll.dwf
    if sys.platform.startswith("darwin"):
        return ctypes.cdll.LoadLibrary("/Library/Frameworks/dwf.framework/dwf")
    try:
        return ctypes.cdll.LoadLibrary(path)
    except OSError as exc:
        raise DwfError(
            f"could not load {path}. Install the Digilent WaveForms SDK "
            f"(the library normally lands in /usr/lib)."
        ) from exc


def last_error(dwf: ctypes.CDLL) -> str:
    """Return the SDK's description of the most recent failure."""
    buf = ctypes.create_string_buffer(512)
    dwf.FDwfGetLastErrorMsg(buf)
    return buf.value.decode(errors="replace").strip()


def dwf_version(dwf: ctypes.CDLL) -> str:
    """Return the SDK version string."""
    buf = ctypes.create_string_buffer(32)
    dwf.FDwfGetVersion(buf)
    return buf.value.decode(errors="replace")


def bind_argtypes(dwf: ctypes.CDLL) -> None:
    """Declare argument types so 64-bit pointers are not truncated."""
    c_int, c_double, c_byte, c_uint = (
        ctypes.c_int,
        ctypes.c_double,
        ctypes.c_byte,
        ctypes.c_uint,
    )
    p_int = ctypes.POINTER(c_int)
    p_byte = ctypes.POINTER(c_byte)
    p_double = ctypes.POINTER(c_double)

    # Device
    dwf.FDwfDeviceOpen.argtypes = [c_int, p_int]
    dwf.FDwfDeviceAutoConfigureSet.argtypes = [c_int, c_int]
    dwf.FDwfDeviceCloseAll.argtypes = []
    dwf.FDwfGetLastErrorMsg.argtypes = [ctypes.c_char_p]
    dwf.FDwfGetVersion.argtypes = [ctypes.c_char_p]

    # Scope
    dwf.FDwfAnalogInReset.argtypes = [c_int]
    dwf.FDwfAnalogInChannelEnableSet.argtypes = [c_int, c_int, c_int]
    dwf.FDwfAnalogInChannelRangeSet.argtypes = [c_int, c_int, c_double]
    dwf.FDwfAnalogInChannelOffsetSet.argtypes = [c_int, c_int, c_double]
    dwf.FDwfAnalogInAcquisitionModeSet.argtypes = [c_int, c_int]
    dwf.FDwfAnalogInFrequencySet.argtypes = [c_int, c_double]
    dwf.FDwfAnalogInRecordLengthSet.argtypes = [c_int, c_double]
    dwf.FDwfAnalogInConfigure.argtypes = [c_int, c_int, c_int]
    dwf.FDwfAnalogInStatus.argtypes = [c_int, c_int, p_byte]
    dwf.FDwfAnalogInStatusRecord.argtypes = [c_int, p_int, p_int, p_int]
    # void_p rather than POINTER(c_double): the buffer is filled at an offset via
    # byref(buf, n), which carries the array's type and a stricter declaration
    # would reject it.
    dwf.FDwfAnalogInStatusData.argtypes = [c_int, c_int, ctypes.c_void_p, c_int]

    # AWG. None of these appear elsewhere in the repo.
    dwf.FDwfAnalogOutReset.argtypes = [c_int, c_int]
    dwf.FDwfAnalogOutNodeEnableSet.argtypes = [c_int, c_int, c_int, c_int]
    dwf.FDwfAnalogOutNodeFunctionSet.argtypes = [c_int, c_int, c_int, ctypes.c_ubyte]
    dwf.FDwfAnalogOutNodeFrequencySet.argtypes = [c_int, c_int, c_int, c_double]
    dwf.FDwfAnalogOutNodeAmplitudeSet.argtypes = [c_int, c_int, c_int, c_double]
    dwf.FDwfAnalogOutNodeOffsetSet.argtypes = [c_int, c_int, c_int, c_double]
    dwf.FDwfAnalogOutNodePhaseSet.argtypes = [c_int, c_int, c_int, c_double]
    dwf.FDwfAnalogOutMasterSet.argtypes = [c_int, c_int, c_int]
    dwf.FDwfAnalogOutRunSet.argtypes = [c_int, c_int, c_double]
    dwf.FDwfAnalogOutRepeatSet.argtypes = [c_int, c_int, c_int]
    dwf.FDwfAnalogOutConfigure.argtypes = [c_int, c_int, c_int]


@dataclass(frozen=True)
class Ad2Config:
    """Scope and AWG settings that stay fixed for a whole run."""

    scope_rate_hz: float = 96_000.0
    capture_s: float = 0.25
    scope_range_v: float = 2.0
    scope_offset_v: float = 0.0
    awg_offset_v: float = 0.0
    frontend_settle_s: float = 2.0
    dwf_path: str = DEFAULT_DWF_PATH


@dataclass
class Capture:
    """One block of scope samples."""

    samples: np.ndarray
    sample_rate_hz: float
    lost: int
    corrupted: int
    t_utc: str

    @property
    def clean(self) -> bool:
        """True when no samples were dropped or flagged corrupt."""
        return self.lost == 0 and self.corrupted == 0


class Ad2:
    """Open AD2, drive W1/W2 in quadrature, capture Ch1.

    Use as a context manager; the AWG is always silenced on the way out.
    """

    def __init__(self, cfg: Ad2Config, log: Callable[[str], None] = print) -> None:
        self.cfg = cfg
        self.log = log
        self.dwf: Optional[ctypes.CDLL] = None
        self.hdwf = ctypes.c_int(HDWF_NONE)
        self._scope_configured = False
        self._awg_running = False
        self._prev_sigint = None
        self.version = ""

    # -- lifecycle ---------------------------------------------------------

    def __enter__(self) -> "Ad2":
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

        # Manual configure: settings are applied when we say so, not on every set.
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
        """Configure Ch1 for record mode.

        The analog frontend needs a couple of seconds to settle after the range
        and offset are set. That cost is paid here, once, rather than on every
        capture - a sweep makes hundreds of captures and would otherwise spend
        most of its time waiting.
        """
        assert self.dwf is not None
        d, h = self.dwf, self.hdwf
        c_int, c_double = ctypes.c_int, ctypes.c_double

        d.FDwfAnalogInReset(h)
        d.FDwfAnalogInChannelEnableSet(h, c_int(0), c_int(1))
        d.FDwfAnalogInChannelRangeSet(h, c_int(0), c_double(self.cfg.scope_range_v))
        d.FDwfAnalogInChannelOffsetSet(h, c_int(0), c_double(self.cfg.scope_offset_v))
        d.FDwfAnalogInAcquisitionModeSet(h, c_int(ACQMODE_RECORD))
        d.FDwfAnalogInFrequencySet(h, c_double(self.cfg.scope_rate_hz))
        d.FDwfAnalogInRecordLengthSet(h, c_double(self.cfg.capture_s))
        d.FDwfAnalogInConfigure(h, c_int(1), c_int(0))  # apply, do not start

        self.log(f"Scope: Ch1 @ {self.cfg.scope_rate_hz/1000:.1f} kHz, "
                 f"{self.cfg.capture_s*1000:.0f} ms, range {self.cfg.scope_range_v} V")
        self.log(f"Waiting {self.cfg.frontend_settle_s:.1f} s for the analog frontend to settle...")
        time.sleep(self.cfg.frontend_settle_s)
        self._scope_configured = True

    def capture(self) -> Capture:
        """Re-arm and stream one block from Ch1."""
        assert self.dwf is not None
        if not self._scope_configured:
            self.configure_scope()

        d, h = self.dwf, self.hdwf
        c_int, c_double = ctypes.c_int, ctypes.c_double

        n_samples = int(round(self.cfg.scope_rate_hz * self.cfg.capture_s))
        buf = (ctypes.c_double * n_samples)()
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

            if got == 0 and sts.value in (DWF_STATE_CONFIG, DWF_STATE_PREFILL, DWF_STATE_ARMED):
                continue

            d.FDwfAnalogInStatusRecord(h, ctypes.byref(avail), ctypes.byref(lost),
                                       ctypes.byref(corrupted))
            # Advance past lost samples so the dropout leaves a hole rather than
            # shifting everything after it in time.
            got += lost.value
            total_lost += lost.value
            total_corrupted += corrupted.value

            if avail.value == 0:
                continue

            take = min(avail.value, n_samples - got)
            d.FDwfAnalogInStatusData(
                h, c_int(0),
                ctypes.byref(buf, ctypes.sizeof(ctypes.c_double) * got),
                c_int(take),
            )
            got += take

        return Capture(
            samples=np.frombuffer(buf, dtype=np.float64).copy(),
            sample_rate_hz=self.cfg.scope_rate_hz,
            lost=total_lost,
            corrupted=total_corrupted,
            t_utc=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        )

    def capture_after(self, settle_s: float) -> Capture:
        """Wait for the radio's DSP to settle on the new stimulus, then capture."""
        time.sleep(settle_s)
        return self.capture()

    # -- AWG ---------------------------------------------------------------

    def set_quadrature(self, f_hz: float, amplitude_v: float, phase_sign: int) -> None:
        """Drive W1/W2 with a phase-locked quadrature pair.

        W1 sits at 0 degrees and W2 leads or lags by 90, which is what makes the
        pair analytic: the radio sees a single-sided tone at +f_hz or -f_hz
        rather than a real cosine with both images. Which sign corresponds to
        which physical wiring is discovered at run time, so the caller passes
        ``phase_sign`` rather than assuming.

        ``FDwfAnalogOutMasterSet`` slaves W2 to W1's timebase, and configuring
        channel -1 starts both atomically; doing them separately leaves the
        phase between the channels undefined.
        """
        assert self.dwf is not None
        d, h = self.dwf, self.hdwf
        c_int, c_double = ctypes.c_int, ctypes.c_double
        node = c_int(ANALOG_OUT_NODE_CARRIER)
        phase_deg = 90.0 * (1 if phase_sign >= 0 else -1)

        for ch, phase in ((0, 0.0), (1, phase_deg)):
            c = c_int(ch)
            d.FDwfAnalogOutNodeEnableSet(h, c, node, c_int(1))
            d.FDwfAnalogOutNodeFunctionSet(h, c, node, ctypes.c_ubyte(FUNC_SINE))
            d.FDwfAnalogOutNodeFrequencySet(h, c, node, c_double(f_hz))
            d.FDwfAnalogOutNodeAmplitudeSet(h, c, node, c_double(amplitude_v))
            d.FDwfAnalogOutNodeOffsetSet(h, c, node, c_double(self.cfg.awg_offset_v))
            d.FDwfAnalogOutNodePhaseSet(h, c, node, c_double(phase))
            d.FDwfAnalogOutRunSet(h, c, c_double(0.0))      # run continuously
            d.FDwfAnalogOutRepeatSet(h, c, c_int(0))

        # Slave channel 2 to channel 1 so they share a timebase.
        d.FDwfAnalogOutMasterSet(h, c_int(1), c_int(0))
        # -1 = all channels, 1 = start. Note 3 is "apply to a channel that is
        # already running" and silently leaves a stopped channel stopped.
        d.FDwfAnalogOutConfigure(h, c_int(-1), c_int(1))
        self._awg_running = True

    def set_am_quadrature(self, carrier_hz: float, amplitude_v: float, phase_sign: int,
                          mod_hz: float, depth_pct: float) -> None:
        """Quadrature carrier with amplitude modulation on both channels.

        Used to exercise the AM demodulator's DC blocker, whose corner is only
        reachable by varying the envelope rather than the carrier.
        """
        assert self.dwf is not None
        d, h = self.dwf, self.hdwf
        c_int, c_double = ctypes.c_int, ctypes.c_double
        carrier = c_int(ANALOG_OUT_NODE_CARRIER)
        am = c_int(ANALOG_OUT_NODE_AM)
        phase_deg = 90.0 * (1 if phase_sign >= 0 else -1)

        for ch, phase in ((0, 0.0), (1, phase_deg)):
            c = c_int(ch)
            d.FDwfAnalogOutNodeEnableSet(h, c, carrier, c_int(1))
            d.FDwfAnalogOutNodeFunctionSet(h, c, carrier, ctypes.c_ubyte(FUNC_SINE))
            d.FDwfAnalogOutNodeFrequencySet(h, c, carrier, c_double(carrier_hz))
            d.FDwfAnalogOutNodeAmplitudeSet(h, c, carrier, c_double(amplitude_v))
            d.FDwfAnalogOutNodeOffsetSet(h, c, carrier, c_double(self.cfg.awg_offset_v))
            d.FDwfAnalogOutNodePhaseSet(h, c, carrier, c_double(phase))

            # The modulation must be in phase on both channels - only the
            # carrier is in quadrature - or the envelope itself rotates.
            d.FDwfAnalogOutNodeEnableSet(h, c, am, c_int(1))
            d.FDwfAnalogOutNodeFunctionSet(h, c, am, ctypes.c_ubyte(FUNC_SINE))
            d.FDwfAnalogOutNodeFrequencySet(h, c, am, c_double(mod_hz))
            d.FDwfAnalogOutNodeAmplitudeSet(h, c, am, c_double(depth_pct))
            d.FDwfAnalogOutNodeOffsetSet(h, c, am, c_double(0.0))
            d.FDwfAnalogOutNodePhaseSet(h, c, am, c_double(0.0))

            d.FDwfAnalogOutRunSet(h, c, c_double(0.0))
            d.FDwfAnalogOutRepeatSet(h, c, c_int(0))

        d.FDwfAnalogOutMasterSet(h, c_int(1), c_int(0))
        d.FDwfAnalogOutConfigure(h, c_int(-1), c_int(1))
        self._awg_running = True

    def awg_off(self) -> None:
        """Stop driving the radio's inputs."""
        if self.dwf is None or self.hdwf.value == HDWF_NONE:
            return
        self.dwf.FDwfAnalogOutReset(self.hdwf, ctypes.c_int(-1))
        self._awg_running = False
