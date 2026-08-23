"""CAT and diagnostic control of the Phoenix radio for the filter HIL suite.

The radio exposes two USB CDC ports: CAT on the second interface at 38400, and a
diagnostic port on the first at 115200. Commands are terminated with ``;`` in
both directions, with no line ending.

Two things about the CAT protocol shape this module:

* **Many writes return nothing.** ``CAT.cpp`` suppresses empty responses, so
  waiting for a reply after ``AG``/``MD``/``NR``/``FW``/``SR``/``CF``/``EQ``/``FL``
  costs the full read timeout every time. A sweep issues hundreds of these, so
  :data:`SILENT_WRITES` tells :meth:`Radio.cat` to skip the read entirely.

* **Most radio state is not readable over CAT at all.** AGC, the equaliser and
  the sample rate are only visible in the ``ED;`` dump, which the firmware
  prints to the *diagnostic* port as plain ``Serial.print`` text. That text is
  not a protocol, so :class:`EdSnapshot` parses defensively and keeps the raw
  text for the report.

The suite mutates persisted radio settings, so :class:`RadioStateGuard` records
how to put each one back and runs those restores on the way out, on SIGINT and
at interpreter exit.
"""

from __future__ import annotations

import atexit
import re
import time
from dataclasses import dataclass, field
from typing import Callable, Optional, Sequence

import serial

CAT_BAUD = 38400
DIAG_BAUD = 115200
CAT_PORT = "/dev/ttyACM1"
DIAG_PORT = "/dev/ttyACM0"

#: Commands whose write form produces no reply. Waiting on these wastes the
#: whole read timeout per command.
SILENT_WRITES = {"AG", "MD", "NR", "NT", "FW", "FL", "SR", "CF", "EQ", "DB", "KS", "MG", "VX", "AI"}

#: Commands that take no parameter and yet are *writes*, not reads.
#:
#: :meth:`Radio.send` normally infers the form from the length - a bare ``"XX;"``
#: is a read - and that inference is right for everything in
#: :data:`SILENT_WRITES`, whose read forms all answer. ``TX;`` and ``RX;`` break
#: it: they are three characters long, they are writes, and ``CAT.cpp`` suppresses
#: their empty responses, so treating them as reads costs the whole read timeout
#: on every key and unkey.
SILENT_SHORT_WRITES = {"TX", "RX"}

#: CW sidetone offsets, from Globals.cpp. In CW receive the audio is shifted by
#: the selected offset, so the injection frequency must include it.
CW_TONE_OFFSETS_HZ = (400.0, 562.5, 656.5, 750.0, 843.75)

#: SR command parameter -> sample rate in Hz.
SR_CAT_ARG = {176400: 0, 192000: 1}
#: ED.sampleRate index -> sample rate in Hz (SDT.h SAMPLE_RATE_* macros).
SR_ED_INDEX = {12: 176400, 13: 192000}

#: ModulationType enum from SDT.h.
MOD_USB, MOD_LSB, MOD_AM, MOD_SAM = 0, 1, 2, 3
MOD_NAMES = {MOD_USB: "USB", MOD_LSB: "LSB", MOD_AM: "AM", MOD_SAM: "SAM"}
#: MD command parameter for each modulation.
MOD_TO_MD = {MOD_LSB: 1, MOD_USB: 2, MOD_AM: 4, MOD_SAM: 5}

EQUALIZER_CELL_COUNT = 14

ED_BEGIN = "=== ED Struct Contents ==="
ED_END = "=== End ED Struct ==="


class CatError(RuntimeError):
    """A CAT command failed or returned an error response."""


def _as_int(text: str, default: int = -1) -> int:
    try:
        return int(float(text.strip()))
    except (ValueError, AttributeError):
        return default


def _as_float(text: str, default: float = 0.0) -> float:
    try:
        return float(text.strip())
    except (ValueError, AttributeError):
        return default


@dataclass
class EdSnapshot:
    """Parsed contents of the firmware's ``ED;`` settings dump."""

    raw: str = ""
    fields: dict = field(default_factory=dict)
    agc: int = -1
    sample_rate_hz: int = -1
    audio_volume: int = -1
    cw_filter_index: int = -1
    cw_tone_index: int = -1
    nr_option: int = -1
    notch_on: int = -1
    active_vfo: int = 0
    modulation: list = field(default_factory=lambda: [-1, -1])
    current_band: list = field(default_factory=lambda: [-1, -1])
    equalizer_rec: list = field(default_factory=list)
    fine_tune_hz: list = field(default_factory=lambda: [0.0, 0.0])
    equalizer_xmt: list = field(default_factory=list)
    mic_gain_db: int = -99
    power_out_ssb: list = field(default_factory=list)
    pa_100w: int = -1

    @classmethod
    def parse(cls, lines: Sequence[str]) -> "EdSnapshot":
        """Build a snapshot from the diagnostic port's dump lines.

        Keys on ``name:`` prefixes and ignores anything else, because the
        diagnostic port interleaves ``Debug()`` output with the dump.
        """
        fields: dict[str, str] = {}
        for line in lines:
            if ":" not in line:
                continue
            key, _, value = line.partition(":")
            key = key.strip()
            if key and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_\[\]]*", key):
                fields[key] = value.strip()

        missing = [k for k in ("agc", "sampleRate", "CWFilterIndex", "equalizerRec")
                   if k not in fields]
        if missing:
            raise CatError(
                f"ED dump did not contain {', '.join(missing)}. "
                f"Got {len(fields)} fields; is the diagnostic port right?"
            )

        eq = [_as_int(v) for v in fields.get("equalizerRec", "").split(",") if v.strip()]
        eq_xmt = [_as_int(v) for v in fields.get("equalizerXmt", "").split(",") if v.strip()]
        power_ssb = [_as_float(v) for v in fields.get("powerOutSSB", "").split(",") if v.strip()]
        sr_index = _as_int(fields.get("sampleRate", ""))

        return cls(
            raw="\n".join(lines),
            fields=fields,
            agc=_as_int(fields.get("agc", "")),
            sample_rate_hz=SR_ED_INDEX.get(sr_index, -1),
            audio_volume=_as_int(fields.get("audioVolume", "")),
            cw_filter_index=_as_int(fields.get("CWFilterIndex", "")),
            cw_tone_index=_as_int(fields.get("CWToneIndex", "")),
            nr_option=_as_int(fields.get("nrOptionSelect", "")),
            notch_on=_as_int(fields.get("ANR_notchOn", "")),
            active_vfo=_as_int(fields.get("activeVFO", ""), 0),
            modulation=[_as_int(fields.get("modulation[0]", "")),
                        _as_int(fields.get("modulation[1]", ""))],
            current_band=[_as_int(fields.get("currentBand[0]", "")),
                          _as_int(fields.get("currentBand[1]", ""))],
            equalizer_rec=eq,
            fine_tune_hz=[_as_float(fields.get("fineTuneFreq_Hz[0]", ""), 0.0),
                          _as_float(fields.get("fineTuneFreq_Hz[1]", ""), 0.0)],
            equalizer_xmt=eq_xmt,
            mic_gain_db=_as_int(fields.get("currentMicGain", ""), -99),
            power_out_ssb=power_ssb,
            pa_100w=_as_int(fields.get("PA100Wactive", "")),
        )

    def summary(self) -> dict:
        """The fields the suite and the report care about."""
        return {
            "agc": self.agc,
            "sample_rate_hz": self.sample_rate_hz,
            "audio_volume": self.audio_volume,
            "cw_filter_index": self.cw_filter_index,
            "cw_tone_index": self.cw_tone_index,
            "nr_option": self.nr_option,
            "notch_on": self.notch_on,
            "active_vfo": self.active_vfo,
            "modulation": list(self.modulation),
            "current_band": list(self.current_band),
            "equalizer_rec": list(self.equalizer_rec),
            "fine_tune_hz": list(self.fine_tune_hz),
            "equalizer_xmt": list(self.equalizer_xmt),
            "mic_gain_db": self.mic_gain_db,
            "power_out_ssb": list(self.power_out_ssb),
            "pa_100w": self.pa_100w,
        }

    def diff(self, other: "EdSnapshot") -> dict:
        """Fields that changed between two snapshots."""
        a, b = self.summary(), other.summary()
        return {k: {"before": a[k], "after": b[k]} for k in a if a[k] != b[k]}

    @property
    def active_fine_tune_hz(self) -> float:
        """Fine tune offset applied by the receive chain, in Hz.

        ReceiveProcessing shifts by Fs/4 and then again by this, so it moves the
        frequency an injected tone has to sit at. It is whatever the operator
        last tuned to and is often several kHz.
        """
        return self.fine_tune_hz[self.active_vfo]

    @property
    def modulation_name(self) -> str:
        return MOD_NAMES.get(self.modulation[self.active_vfo], "?")

    @property
    def active_modulation(self) -> int:
        return self.modulation[self.active_vfo]

    @property
    def active_power_ssb(self) -> float:
        """SSB power setting for the band in use, in watts.

        ``TXGain`` derives the transmit DSP gain from this, so it scales the
        amplitude of the exciter I/Q output. It has to stay put for the duration
        of a run or every level measured moves with it.
        """
        band = self.current_band[self.active_vfo]
        if 0 <= band < len(self.power_out_ssb):
            return self.power_out_ssb[band]
        return float("nan")

    @property
    def agc_off(self) -> bool:
        """AGCOff is 0 in the AGCMode enum."""
        return self.agc == 0


class Radio:
    """Serial control of the radio under test."""

    def __init__(self, cat_port: str = CAT_PORT, diag_port: Optional[str] = DIAG_PORT,
                 cat_baud: int = CAT_BAUD, diag_baud: int = DIAG_BAUD,
                 timeout_s: float = 0.5, log: Callable[[str], None] = print) -> None:
        self.cat_port_name = cat_port
        self.diag_port_name = diag_port
        self.cat_baud = cat_baud
        self.diag_baud = diag_baud
        self.timeout_s = timeout_s
        self.log = log
        self.cat: Optional[serial.Serial] = None
        self.diag: Optional[serial.Serial] = None
        self.round_trips = 0
        self.wait_s = 0.0

    def __enter__(self) -> "Radio":
        self.open()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def open(self) -> None:
        try:
            self.cat = serial.Serial(self.cat_port_name, self.cat_baud, timeout=self.timeout_s)
        except serial.SerialException as exc:
            raise CatError(f"could not open the CAT port {self.cat_port_name}: {exc}") from exc
        if self.diag_port_name:
            try:
                self.diag = serial.Serial(self.diag_port_name, self.diag_baud,
                                          timeout=self.timeout_s)
            except serial.SerialException as exc:
                self.log(f"WARNING: could not open the diagnostic port "
                         f"{self.diag_port_name}: {exc}")
                self.diag = None
        time.sleep(0.2)
        self.drain_diag()

    def close(self) -> None:
        for port in (self.cat, self.diag):
            if port is not None and port.is_open:
                port.close()
        self.cat = self.diag = None

    # -- CAT ---------------------------------------------------------------

    def send(self, cmd: str, expect_reply: Optional[bool] = None) -> Optional[str]:
        """Send one CAT command, optionally reading its reply.

        ``expect_reply`` defaults to False for the write forms of commands the
        firmware answers with an empty string; see :data:`SILENT_WRITES`.
        """
        if self.cat is None:
            raise CatError("CAT port is not open")
        if not cmd.endswith(";"):
            cmd += ";"

        if expect_reply is None:
            name = cmd[:2].upper()
            if name in SILENT_SHORT_WRITES:
                expect_reply = False
            else:
                is_read = len(cmd) == 3  # "XX;"
                expect_reply = is_read or name not in SILENT_WRITES

        self.cat.reset_input_buffer()
        self.cat.write(cmd.encode("ascii"))
        self.cat.flush()
        self.round_trips += 1

        if not expect_reply:
            return None

        started = time.monotonic()
        out = []
        deadline = started + self.timeout_s
        while time.monotonic() < deadline:
            if self.cat.in_waiting:
                ch = self.cat.read(1).decode("ascii", errors="replace")
                out.append(ch)
                if ch == ";":
                    break
            else:
                time.sleep(0.002)
        self.wait_s += time.monotonic() - started

        reply = "".join(out)
        return reply or None

    def expect(self, cmd: str) -> str:
        """Send a command that must answer, and reject an error response."""
        reply = self.send(cmd, expect_reply=True)
        if reply is None:
            raise CatError(f"{cmd!r} produced no reply")
        if reply == "?;":
            raise CatError(f"{cmd!r} was rejected by the radio")
        return reply

    # -- diagnostic port ---------------------------------------------------

    def drain_diag(self) -> None:
        if self.diag is not None:
            self.diag.reset_input_buffer()

    def read_diag_until(self, sentinel: str, timeout_s: float = 3.0) -> list[str]:
        """Collect diagnostic lines until a sentinel appears."""
        if self.diag is None:
            raise CatError("the diagnostic port is not open; it is needed to read the ED dump")
        lines: list[str] = []
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            raw = self.diag.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").rstrip("\r\n")
            lines.append(line)
            if sentinel in line:
                return lines
        raise CatError(f"timed out waiting for {sentinel!r} on the diagnostic port")

    def dump_ed(self, timeout_s: float = 3.0) -> EdSnapshot:
        """Ask for the settings dump and parse it off the diagnostic port."""
        self.drain_diag()
        self.send("ED;", expect_reply=True)
        lines = self.read_diag_until(ED_END, timeout_s)
        # Discard anything printed before the dump started.
        for i, line in enumerate(lines):
            if ED_BEGIN in line:
                lines = lines[i + 1:]
                break
        return EdSnapshot.parse(lines)

    def dump_psd(self, timeout_s: float = 5.0) -> list[float]:
        """Read the 512-bin spectrum the firmware prints to the diagnostic port."""
        if self.diag is None:
            raise CatError("the diagnostic port is not open; it is needed to read the PSD")
        self.drain_diag()
        self.send("PD;", expect_reply=True)
        values: list[float] = []
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline and len(values) < 512:
            raw = self.diag.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if "," in line:
                _, _, val = line.partition(",")
                try:
                    values.append(float(val))
                except ValueError:
                    pass
        return values

    # -- radio settings ----------------------------------------------------

    def get_sample_rate(self) -> int:
        reply = self.expect("SR;")
        idx = int(reply[2])
        for rate, arg in SR_CAT_ARG.items():
            if arg == idx:
                return rate
        raise CatError(f"unrecognised SR reply {reply!r}")

    def set_sample_rate(self, rate_hz: int, settle_s: float = 2.0) -> None:
        """Switch sample rate and confirm the radio took it.

        ChangeSampleRate() reconfigures the I2S clock and rebuilds the whole DSP
        chain, so the settle is not optional.
        """
        if rate_hz not in SR_CAT_ARG:
            raise CatError(f"{rate_hz} is not a CAT-selectable sample rate")
        self.send(f"SR{SR_CAT_ARG[rate_hz]};", expect_reply=False)
        time.sleep(settle_s)
        actual = self.get_sample_rate()
        if actual != rate_hz:
            raise CatError(f"asked for {rate_hz} sps but the radio reports {actual}")

    def get_cw_filter(self) -> int:
        return int(self.expect("CF;")[2])

    def set_cw_filter(self, index: int) -> None:
        self.send(f"CF{index};", expect_reply=False)

    def get_eq_cell(self, index: int) -> int:
        return int(self.expect(f"EQ{index:02d};")[4:7])

    def set_eq_cell(self, index: int, value: int) -> None:
        self.send(f"EQ{index:02d}{value:03d};", expect_reply=False)

    def set_eq_all(self, values: Sequence[int]) -> None:
        for i, v in enumerate(values):
            self.set_eq_cell(i, int(v))

    def set_eq_solo(self, index: int, level: int = 100) -> None:
        """Leave one equaliser cell up and the rest at zero.

        The cells are summed in parallel with alternating signs, so zeroing the
        others isolates this one exactly rather than approximately.
        """
        for i in range(EQUALIZER_CELL_COUNT):
            self.set_eq_cell(i, level if i == index else 0)

    def set_volume_pct(self, pct: int) -> None:
        """Set AF volume. The AG parameter is 0-255 scaled from 0-100 percent."""
        self.send(f"AG0{int(round(pct * 255 / 100)):03d};", expect_reply=False)

    def get_filter_hi_hz(self) -> int:
        return int(self.expect("FW;")[2:6])

    def set_filter_hi_hz(self, hz: int) -> None:
        self.send(f"FW{int(hz):04d};", expect_reply=False)

    def get_filter_lo_hz(self) -> int:
        return int(self.expect("FL;")[2:6])

    def set_filter_lo_hz(self, hz: int) -> None:
        self.send(f"FL{int(hz):04d};", expect_reply=False)

    def set_nr(self, value: int) -> None:
        self.send(f"NR{value};", expect_reply=False)

    def set_notch(self, value: int) -> None:
        self.send(f"NT{value};", expect_reply=False)

    def get_mode(self) -> str:
        return self.expect("MD;")

    def enter_cw(self) -> None:
        self.send("MD3;", expect_reply=False)

    def set_modulation(self, mod: int) -> None:
        """Select a demodulator, and leave CW receive if that is where we are."""
        if mod not in MOD_TO_MD:
            raise CatError(f"no MD command for modulation {mod}")
        self.send(f"MD{MOD_TO_MD[mod]};", expect_reply=False)

    # -- transmit -----------------------------------------------------------

    def ptt_press(self, settle_s: float = 0.5) -> None:
        """Key the transmitter.

        ``TX;`` is only honoured from ``SSB_RECEIVE`` or ``CW_RECEIVE``; from any
        other state ``TX_write`` falls through its switch and does nothing at
        all, silently. Callers that need to know whether the radio actually keyed
        have to look at the exciter output, not at the CAT reply.
        """
        self.send("TX;", expect_reply=False)
        time.sleep(settle_s)

    def ptt_release(self, settle_s: float = 0.3) -> None:
        """Unkey. Safe to call when already receiving."""
        self.send("RX;", expect_reply=False)
        time.sleep(settle_s)

    def get_mic_gain_pct(self) -> int:
        """Mic gain as the 0-100 scale the MG command uses."""
        return int(self.expect("MG;")[2:5])

    def set_mic_gain_pct(self, pct: int) -> None:
        """Set mic gain on the MG 0-100 scale, which maps to -40..+30 dB."""
        self.send(f"MG{int(round(max(0, min(100, pct)))):03d};", expect_reply=False)

    @staticmethod
    def mic_gain_pct_from_db(db: float) -> int:
        """Invert the MG command's -40..+30 dB mapping."""
        return int(round((float(db) + 40.0) * 100.0 / 70.0))

    @staticmethod
    def mic_gain_db_from_pct(pct: float) -> int:
        """The dB value the firmware will store for an MG parameter.

        ``MG_write`` truncates to an integer number of dB, so a percentage does
        not round-trip exactly; this reproduces what the radio will end up with.
        """
        return int((float(pct) * 70.0 / 100.0) - 40.0)

    def get_power_w(self) -> float:
        """Requested output power, watts. Mode dependent in the firmware."""
        return float(self.expect("PC;")[2:5])

    def set_power_w(self, watts: float) -> None:
        self.send(f"PC{int(round(watts)):03d};", expect_reply=True)

    def sidetone_shift_hz(self, ed: EdSnapshot, in_cw: bool) -> float:
        """Audio offset applied in CW receive, in Hz.

        In CW the chain shifts by the selected sidetone offset so the beat note
        lands where the operator expects, which moves the injection frequency by
        the same amount.
        """
        if not in_cw:
            return 0.0
        idx = ed.cw_tone_index
        if 0 <= idx < len(CW_TONE_OFFSETS_HZ):
            return CW_TONE_OFFSETS_HZ[idx]
        return CW_TONE_OFFSETS_HZ[3]


class RadioStateGuard:
    """Undo everything the suite changed, however it exits.

    Restores run last-registered-first, so a setting changed twice ends up back
    at the value it had before the first change. A restore that raises is logged
    and the rest still run; whatever could not be put back shows up in the final
    ED diff.
    """

    def __init__(self, radio: Radio, baseline: EdSnapshot,
                 log: Callable[[str], None] = print) -> None:
        self.radio = radio
        self.baseline = baseline
        self.log = log
        self._restores: list[tuple[str, Callable[[], None]]] = []
        self.restored = False
        self.residual_diff: dict = {}
        self.failures: list[str] = []

    def __enter__(self) -> "RadioStateGuard":
        atexit.register(self.restore)
        return self

    def __exit__(self, *exc) -> None:
        self.restore()
        atexit.unregister(self.restore)

    def on_restore(self, label: str, fn: Callable[[], None]) -> None:
        """Register how to undo a change, before making it."""
        self._restores.append((label, fn))

    def restore(self) -> None:
        """Run every registered restore, newest first. Safe to call twice."""
        if self.restored:
            return
        self.restored = True
        for label, fn in reversed(self._restores):
            try:
                fn()
            except Exception as exc:  # keep going; a stuck radio is worse
                self.failures.append(f"{label}: {exc}")
                self.log(f"WARNING: could not restore {label}: {exc}")
        self._restores.clear()

    def verify(self) -> dict:
        """Re-read the radio and report anything that did not go back."""
        try:
            final = self.radio.dump_ed()
        except Exception as exc:
            self.residual_diff = {"error": str(exc)}
            return self.residual_diff
        self.residual_diff = self.baseline.diff(final)
        return self.residual_diff
