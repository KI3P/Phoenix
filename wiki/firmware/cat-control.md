---
title: CAT Control (Kenwood emulation)
type: module
status: draft
created: 2026-06-08
updated: 2026-11-21
tags: [cat, kenwood, ts-480, ts-2000, serial, rig-control, usb]
source_refs: []
related: ["[[digital-mode]]", "[[overview]]", "[[main-loop]]", "[[mode-state-machine]]", "[[tune-frequency-control]]", "[[persistent-config]]", "[[sample-rate-switching]]", "[[filter-hil-test]]", "[[audio-equalizer]]", "[[cw-processing]]", "[[tx-filter-hil-test]]", "[[filter-board-electronics]]", "[[log-amp-power-detection]]", "[[spi-display-hang]]"]
---

# CAT Control (Kenwood emulation)

**Files:** `CAT.cpp` (~28 KB), `CAT.h`. Computer-Aided Transceiver control so standard
logging/contest/digital software (WSJT-X, fldigi, hamlib, N1MM…) can drive the radio.

## Transport
- **Port:** `SerialUSB1` — the *second* USB serial interface (requires Arduino "USB Type =
  Dual Serial"). The first port is the debug/console serial.
- **Baud:** 38400.
- **Framing:** ASCII commands terminated by `;`. Polled each pass by `CheckForCATSerialEvents()`
  from [[main-loop]], which buffers until the terminator then parses.
- **Newlines are not CAT (2026-08-05, commit ab69673).** In the `serial+midi+audio` build
  there is only one CDC port, so CAT and the console share `Serial`. The CAT reader drained
  that port before `CheckForSerialTimeSync()` ever saw it. As a result, the PJRC time packet
  (`T<10-digit epoch>\n`) was swallowed: the clock was never set, and the leftover bytes made
  the next command answer `?;`. No Kenwood command contains a newline, so the CAT reader now
  treats a newline-terminated run of bytes as something other than CAT:
  - a `T` prefix is passed to `ApplyTimeSyncDigits()` (`Loop.cpp`), shared with the two-port
    build's reader;
  - anything else is discarded. This also fixes clients that end their commands with CRLF.
  
  `code/tools/set_radio_time.py` and `Set-RadioTime.ps1` send the time packet from a PC.

## Protocol ⚠️ TS-480 vs TS-2000
The implemented dialect is the **Kenwood** 2-letter command set. The two source docs
**disagree on which radio**: `CAT.cpp:21` says *"Kenwood TS-480 CAT Interface (partial)"* (and
`code/docs/ts_480_pc.pdf` is the reference), but `CAT.h` says *"Kenwood TS-2000 protocol"*. The
two share most commands so it works either way, but the docs should be reconciled — logged in
[[documentation-todos]].

## Architecture: a command-dispatch table

The core is a static table of `valid_command` entries (`NUM_SUPPORTED_COMMANDS = 35`,
`CAT.cpp:104-136`):
```c
typedef struct {
    char name[3];                       // 2-letter command + NUL
    int  set_len, read_len;             // expected payload lengths
    char* (*write_function)(char*);     // "set" handler
    char* (*read_function)(char*);      // "query" handler
} valid_command;
```
`command_parser()` matches the incoming 2-letter code against the table and dispatches to the
**write** (set) or **read** (query) handler based on the command length — a query is just the
bare command (e.g. `FA;`), a set carries a payload (`FA00014200000;`). Read handlers format
`ED`/hardware state into a Kenwood response string; write handlers apply the change.

> ⚠️ **Adding a command? `set_len` and `read_len` must differ.** `command_parser` tests the
> **write form first**, so a command declaring `set_len == read_len` has an **unreachable read
> handler** — it becomes silently write-only. This is not hypothetical: it is exactly the bug
> that made `FW;` unreadable for as long as `FW` has existed (see below).

## Implemented commands (39)

| Group | Commands |
|---|---|
| Frequency / VFO | `FA`/`FB` (VFO A/B freq), `FR`/`FT` (RX/TX VFO select), `FW` (filter high cut) |
| Mode / status | `MD` (mode), `IF` (transceiver status string), `ID` (radio id) |
| Band | `BU`/`BD` (band up/down) |
| Gain / audio | `AG` (AF gain), `MG` (mic gain), `PC` (power control) |
| Digital mode | `DG` (enter/leave), `DR` (receive level 000-100), `DD` (transmit drive 000-100), `DS` (TX path counters, read-only) |
| SWR bridge / transmit level | `BR` (raw bridge statistics), `XA` (CW attenuation override) |
| Antenna tuner | `AC` (Kenwood: start/stop tuning, network in/thru), `AT` (raw ATU relay state), `AX`/`AY` (tuner diagnostics); need `ATU_PRESENT` except `AC`, which reads `AC000;` without it; see [[atu-control]] |
| DSP | `NR` (noise reduction), `NT` (notch), `VX` (VOX), `PR` (speech processor = CESSB on/off, `PR0;`/`PR1;`/`PR;`), `PL` (processor input level = CESSB level, 10 per dB; output level stored, not used), `RM` (meter: only `RM2` COMP, the CESSB compression meter at 2 dB per dot; `RM1`/`RM3` read `?;`). Branch `cessb-proposal`, see [[cessb]] |
| Keyer | `KS` (keyer speed / WPM) |
| PTT | `TX` (transmit), `RX` (receive) |
| Misc | `AI` (auto-info), `PS` (`PS0;` powers off, `PS1;` is a silent no-op), `PD`, `DB` |
| **Custom (non-Kenwood)** | `ED` (dump the `ED` config), `HR` (dump the `hardwareRegister`), **`XS`**, **`CF`**, **`EQ`**, **`FL`**, **`TT`** (SSB transmit test tone, see below) |

`ED` and `HR` are Phoenix extensions explicitly marked "NOT a Kenwood keyword" — handy debug
dumps of [[persistent-config]] and the [[filter-boards]] `hardwareRegister`.

### Kenwood conformance (checked 2026-11-15 against `code/docs/ts_480_pc.pdf`)

Custom commands must use letter pairs the TS-480 spec does not define, or a Kenwood client
sending the real command gets our meaning instead.

- **Renamed 2026-11-15:** `SR` → `XS` (sample rate). Kenwood `SR1;`/`SR2;` is a VFO/full reset
  (Hamlib `rig_reset`), which used to change the sample rate. `PR` → `HR` (hardware-register
  dump). Kenwood `PR` is the speech processor on/off, so a client polling the compressor got a
  malformed `PR;` reply and caused a register dump on every poll. Since 2026-11-18 (branch
  `cessb-proposal`) `PR` is the real Kenwood command again: the speech processor, which in
  Phoenix is CESSB ([[cessb]]).
- **Fixed 2026-11-15:** `PS_write` ignored its parameter and shut the radio down on any `PSx;`,
  including Kenwood's power-*on* `PS1;` (Hamlib `set_powerstat(ON)`). It now shuts down only on
  `PS0;`, ignores `PS1;`, rejects anything else, and sends no answer (Kenwood set commands get none).
- **Known deviations, left as they are:**
  - `MD`: Kenwood uses 4 = FM and 5 = AM. Ours uses 4 = AM and 5 = SAM, both ways; `IF` reports
    both AM and SAM as 5, so `MD;` and `IF;` disagree in AM.
  - `FW`: Kenwood's is the CW/FSK filter *width*, from a fixed list (SSB uses `SH`/`SL`). Ours is
    the passband high cut in every mode.
  - `NR`: Kenwood allows 0–2. We accept 3 (LMS), and `NR_write` doesn't validate the digit.
  - `DB` is not a TS-480 command, though the table comment doesn't say so.

### SSB transmit test tone: `TT` (added 2026-11-21, branch `cessb-proposal`)

`TT0;` selects the microphone, `TT1;` a single 1 kHz tone, and `TT2;` a two-tone signal
(700 + 1900 Hz, equal amplitude) for IMD tests. `TT;` reads the setting back. This is the same
setting as the Microphone menu's "TX src" item. Neither the menu nor `TT` saves it, so the radio always
starts on the microphone. The setting takes effect at once, also while transmitting, and is
used only in SSB transmit; digital mode and the calibrations keep their own sources.

- **Where the tones enter:** two `AudioSynthWaveformSine` objects on ports 2 and 3 of the
  microphone input mixers (`MainBoard_AudioIO.cpp`, `SetTXAudioSource()`). Port 1 is the
  TX IQ calibration oscillator. The tones therefore pass through the whole chain: TX EQ,
  Hilbert, CESSB and TXGain. Mic gain is set in the codec, so it does not affect them.
- **Levels:** the single tone has amplitude 0.08, the same as the TX IQ / power calibration
  tone (40/500), so it comes out at the power setting. Each of the two tones has 0.04, so
  the two-tone PEP equals the power setting and each tone is 6 dB below PEP.
- On the home screen, the Settings pane shows `TX src:` as `Mic` in green, or `Tone` / `IMD`
  in red.
- **Products to read on USB:** the tones are at dial +700 / +1900 Hz, IMD3 at −500 / +3100 Hz
  and IMD5 at −1700 / +4300 Hz. The products come from the RF chain, after the sideband
  filter, so both sides are equally valid: the −500 Hz product is *not* suppressed (measured
  equal to within 0.2 dB).
- **Bench, radio 2, 2026-11-21** (14.100 MHz USB, dummy load, 60 dB tap to the CMU200 RF1,
  NPOWer and 100 Hz RBW spectra):
  - Single tone at a 10 W setting: PEP 8.35 W, max − avg 0.13 dB. The image is −59 dBc, the
    carrier −58 dBc, and the 2nd/3rd audio harmonics −60 / −68 dBc.
  - Two-tone at 2 W and 5 W: PEP 2.08 W and 4.74 W (within 0.25 dB of the setting), max − avg
    3.0 dB. IMD3 is −32 dBc re one tone (−38 dB re PEP) at both levels.
  - Two-tone at 10 W: PEP 8.1 W, max − avg 2.72 dB, IMD3 −27.5 dBc. The output stage
    compresses above about 5 W.
  - The 1900 Hz tone is 0.6 dB below the 700 Hz tone at every level, i.e. the transmit
    audio passband.
  - CESSB on at 0 dB changed nothing (no clipping). Switching `TT` while keyed took effect
    at once.
  - The tone source isn't the IMD source. A single tone has a constant envelope, so an
    RF-chain (AM/AM) nonlinearity leaves it clean, yet its audio harmonics are ≤ −60 dBc.
    The two-tone products therefore arise after SSB generation.

### DSP-settings commands (added 2026-07, `CAT.cpp:132-135`)

Added so [[filter-hil-test]] could drive settings that were previously **touchscreen-only**.

| Cmd | Set | Read | Meaning |
|---|---|---|---|
| `XS` | `XSn;` | `XS;` | Sample rate: `0` = 176.4 ksps, `1` = 192 ksps ([[sample-rate-switching]]). **Rejected while transmitting** — it reconfigures the I²S clock and rebuilds the whole DSP chain (`CAT.cpp:804-827`). |
| `CF` | `CFn;` | `CF;` | Receive CW audio filter index 0–5 ([[cw-processing]]). |
| `EQ` | `EQbbvvv;` | `EQbb;` | Receive equaliser cell `bb` = 00–13, level `vvv` = 000–100 ([[audio-equalizer]]). Note set/read lengths differ by design. |
| `FL` | `FL####;` | `FL;` | DSP filter **low** cut — the mirror of `FW`'s high cut. |

⚠️ **Still no CAT command for AGC mode.** That is the one setting [[filter-hil-test]] needs and
cannot have, so the suite refuses to run rather than setting it.

There is also **no command for the transmit equaliser** (`ED.equalizerXmt`) to mirror `EQ`'s
receive cells. [[tx-filter-hil-test]] works around it rather than adding one, because
`S_Xmt[i].pCoeffs` and `S_Rec[i].pCoeffs` point at the *same* coefficient array
(`DSP_FFT.cpp:402-403`) — so soloing the transmit cells would re-verify tables `EQ` already
exposes ([[audio-equalizer]]).

### Digital-mode commands (added 2026-07-31)

Three non-Kenwood additions for [[digital-mode]]:

- **`DG`** — `DG1;` enters digital mode, `DG0;` leaves, `DG;` reads. Entering forces
  176.4 ksps and starts the USB audio link; leaving restores the previous rate. Accepted only
  from a receive state, and rejected outright in builds without a USB audio interface.
- **`DR`** — `DR000;`–`DR100;` set the level applied to the receive audio sent to the host,
  `DR;` reads it back zero-padded. Default 25 (−12 dB). Needed because nothing else regulates
  that level: with AGC off the AGC stage applies a fixed gain of 20, so a strong signal
  otherwise clips.
- **`DS`** — read-only, returns `DS<calls>,<underruns>,<trimmed>,<depthMin>,<depthMax>;` for the
  transmit path. Added to diagnose transmit spurs and immediately found the record queue pinned
  at 208 of its 209-block ceiling. Counters reset on entering digital mode.

**`MD` is deliberately inert on digital mode.** WSJT-X sends `MD2;` to force USB on startup and
before every transmission, so treating a modulation change as a request to leave digital mode
would make the mode impossible to hold. Only `DG0;` or the front panel leaves it.

**`XS` is refused while in digital mode**, because that mode pins the sample rate.

### Bridge-measurement commands (added 2026-10-24)

Added for `code/tools/swr/swr_bench_measure.py`, which takes the SWR bridge calibration series
under CAT control (`code/tools/swr/swr_calibration_50_open_short.ipynb`, Section 11; both on the
`swr_cal_redux` branch, not in `nightly`). All three are
non-Kenwood. Uncommitted as of 2026-10-24.

- **`BR`** — raw SWR bridge statistics. `PerformSWRBridgeReading()` now also adds every raw
  AD7991 pair to running sums (`LPFBoard.cpp`, `AccumulateSWRBridgeStats()`): the raw readings,
  before the 0.1/0.9 running average, so a mean over a steady carrier carries no lag.
  `BR0;` clears, `BR1;` reads both detectors once on demand (the bridge is not read while
  receiving, so this is how a key-up floor reading is taken), and `BR;` returns
  `BR<n>,<fwd mean>,<fwd sd>,<ref mean>,<ref sd>;` in mV to 0.1 mV. Failed I²C reads, which
  `readADCsingle()` returns as 65535 or 65534, are left out. A growing `n` also proves the radio
  really keyed, because `TX;` is silently ignored during the CW hang time.
- **`XA`** — CW transmit attenuation override. `XAnnn;` forces nnn tenths of a dB (000–315) in
  place of the power-curve value at the next key-down; `XA999;` returns to the power setting.
  The PA selection still follows `powerOutCW`. Not saved. `XA;` returns `XAnnn,o;`: the
  attenuator's *present* setting (31.5 dB while receiving, so read it keyed) and whether an
  override is set. Needed because `PC` only takes whole watts, so it cannot reach the
  attenuator's limits (the sheet's `min` and `full` rows). Implemented as
  `SetCWAttenuationOverride()` in `HardwareSm.cpp`, applied in the `RFCWMark`/`RFCWSpace` normal
  branch only, not in power calibration.
- **`AT`** (added 2026-11-14) — the ATU relay state, for testing the tuner. `ATnbsLLLCCC;` sets
  network/balun/caps-at-input and the L and C masks in one expander update. `AT;` returns the
  state plus the register field and the I²C readback in hex. It is refused (`?;`) while
  transmitting and in builds without `ATU_PRESENT`. Full format in [[atu-control]]. Kenwood's
  `AC` (internal tuner) is left for the automatic tuner, [[atu-auto-tuning]].
- **`DD`** — the digital-mode transmit drive (`ED.digitalDriveLevel`, the "USB drive" menu item),
  `DD000;`–`DD100;`, `DD;` reads. Previously menu-only.

### ⚠️ `TX` and `RX` are silent *writes* in both their forms

A trap for any CAT client, not just the test suites. Almost every command distinguishes its read
form from its write form by **length** — a bare `XX;` is a read. `TX` and `RX` break that: a bare
`TX;` is a **write**, and their handlers return `empty_string_p`, whose empty response
`command_parser` suppresses. So a client that infers "3 characters ⇒ read ⇒ wait for a
reply" **blocks for its full read timeout on every key and unkey**, waiting for something that is
never coming. `filter_hil/radio.py` carries a `SILENT_SHORT_WRITES` set for exactly this pair.

### The `TX` parameter is optional (fixed 2026-07-30)

`ts_480_pc.pdf` p.21 defines the set form as `TX P1;` — P1 is `0` (normal/MIC), `1` (DTS via the
ANI input) or `2` (TX Tune) — and states that **"if no P1 parameter is specified, P1=0 is used"**.
All four of `TX;`, `TX0;`, `TX1;` and `TX2;` are therefore legal ways to key. Phoenix originally
accepted only `TX;`, because the table entry was `{ "TX", 3, 0, ... }` and `command_parser`
dispatches purely on where the semicolon falls.

That was a real interoperability bug, not a pedantic one. **Hamlib** — the layer WSJT-X, fldigi and
Pat all drive the radio through — picks the form from the PTT type: `TX;` for `RIG_PTT_ON`, `TX0;`
for `RIG_PTT_ON_MIC`, `TX1;` for `RIG_PTT_ON_DATA`, and `RX;` to unkey. Which one a given user
sends depends on their backend and data-mode settings, so the radio keyed for some clients and
silently refused for others.

Both entries now read `{ "TX", 3+1, 3, TX_write, TX_write }` (and likewise for `RX`, which also
accepts the TS-2000-style `RX0;`/`RX1;`). The idiom for an **optional parameter** is to put the
*write* function in both slots: `set_len` covers the parameterised form, `read_len` the bare one.
There is deliberately no `TX_read` — adding one would make `TX;` read instead of key, breaking
Hamlib's `RIG_PTT_ON` and both HIL suites, which send `TX;`/`RX;` (`filter_hil/radio.py:480`).
`TX_write` ignores P1, so every form keys identically; Phoenix has neither a separate data input
nor a tune mode to distinguish them.

> When [[usb-audio]] (#13) resumes, note that `TX1;` is Hamlib's standard "transmit from the data
> port" signal — which may remove the need for the non-Kenwood `UM;` command on the `usb_audio`
> branch.

The same change closed a latent defect in `command_parser`: it evaluated
`command[ set_len - 1 ]` and `command[ read_len - 1 ]` unconditionally, and nine entries carry a
`0` in one of those fields, so the expression became `command[-1]` — an out-of-bounds read off the
front of `catCommand[128]`. It never crashed, but `ID;`, `IF;`, `PD;`, `ED;` and `PR;` (now `HR;`) reached
their read handlers only because the preceding byte happened not to be `';'`. Both tests are now
guarded with `> 0`.

⚠️ **Test the dispatcher, not just the handler.** `CAT_test.cpp` already asserted that TX accepted
`TX;`, `TX0;` and `TX1;` — but it called `TX_write()` directly, and `TX_write` ignores its
argument. The tests passed for the entire life of the bug. The regression tests added alongside
the fix go through `command_parser()`.

`PC` (power) is the opposite case worth noting alongside it: `PC_write` *does* answer, echoing
`PC%03d;`, so it is one of the few writes that is not silent.

## Fixes to pre-existing commands

All of these affected CAT users on earlier releases:

- **`PC` in digital mode set the CW power (fixed 2026-10-24).** `PC_write`/`PC_read` tested only
  the SSB states, so in `DIGITAL_RECEIVE`/`DIGITAL_TRANSMIT` they fell through to
  `ED.powerOutCW`. Digital mode transmits through the SSB chain, and `TXGain()` takes its level
  from `ED.powerOutSSB`. A client setting power in digital mode therefore changed nothing on the
  air. Both now use `UsesSSBPower()`, which includes the digital states.
- **`PC` dropped the hundreds digit (fixed 2026-10-24).** `PC_write` parsed from `cmd[3]`, so
  `PC100;` set 0 W. It now parses all three digits.
- **`FA;`, `FB;` and `IF;` replies were garbled on the radio (fixed 2026-11-11, `38b26c3`).**
  They formatted `GetTXRXFreq()`'s `int64_t` with `%lld`. The Teensy's printf has no
  `long long` support: it printed a literal `ld` (`FA;` → `FA0000000000ld;`), and in `IF;`
  every later field shifted. They now cast to `long` (32 bits, ample for Hz) and use `%ld`.
  The host unit tests could not catch this, because the PC's printf handles `%lld`. Two
  display labels had the same bug: the home screen FT Inc value and the power-cal Freq line
  (`ebe42f7`). No `%lld` remains in the sketch.
- **CAT going silent with USB still connected** was the display SPI hang, not a CAT fault
  ([[spi-display-hang]]).

- ⚠️ **`MD` inverted the receive filter.** `MD_write` set `bands[].mode` but never
  `ED.modulation[]`. `Demodulate()` switches on the *latter*, while `InitFilterMask()` *compares
  the two* and treats a difference as a deliberate departure from the band default — by
  **mirroring the passband**. So `MD` did not merely fail to change the demodulator; it flipped
  the sideband of the receive filter. (Same normalization the display had to learn — see
  `EffectivePassbandEdges_Hz` in [[display-subsystem]].)
- **`MD1`/`MD2` could not escape CW.** They now dispatch `TO_SSB_MODE` when in CW receive. That
  transition previously existed **only on the front-panel button**, so CAT could enter CW mode
  and never leave it ([[mode-state-machine]]).
- **`FW` was write-only.** It declared `set_len == read_len`, making `FW;` unreachable per the
  parser rule above. Also `MD4` now selects **AM**, which was previously unreachable (`MD5`
  gives SAM).

## How commands apply
CAT does **not** have a private path to the hardware — it feeds the **same channels as the
front panel**:
- **Event dispatch:** writes enqueue `InterruptType` events into the [[main-loop]] FIFO — e.g.
  a frequency set raises `iUPDATE_TUNE` (`CAT.cpp:221`), band up/down raise `iBUTTON_PRESSED`
  (`CAT.cpp:174,183`). So CAT tuning flows through [[tune-frequency-control]] exactly like the
  encoder.
- **Direct `ED` writes:** simple settings write straight to [[persistent-config]] (e.g.
  `KS_write` → `ED.currentWPM`, `CAT.cpp:475`).
- **PTT:** `TX_write` dispatches into [[mode-state-machine]] (switches on `modeSM.state_id`),
  so CAT PTT obeys the same TX interlocks/keying as physical PTT. ⚠️ It only dispatches from
  `SSB_RECEIVE` (→ `PTT_PRESSED`) or `CW_RECEIVE` (→ `KEY_PRESSED`); from any **other** state it
  falls through its `switch` and does nothing at all, silently. A client cannot learn from the CAT
  reply whether the radio actually keyed — it has to observe the hardware.

Read handlers serialize state back: `IF_read` builds the long TS-480 status string (VFO freq,
mode, RX/TX, etc.); `FA_read`/`MD_read`/`KS_read`/… each return one field.

## Open questions
- Reconcile TS-480 (`CAT.cpp`) vs TS-2000 (`CAT.h`) and verify the `IF` status-string field
  layout against `code/docs/ts_480_pc.pdf`. **`TX`/`RX` were checked against p.21 on 2026-07-30**
  (see above); the rest of the table has not been audited against the spec, and `TX`/`RX` were
  wrong, so assume others may be too.
- A CAT command for **AGC mode** — the remaining gap for [[filter-hil-test]], which currently
  has to demand the operator set it by hand.
- `XS` is interlocked against transmit, but the **menu path is not** ([[sample-rate-switching]]).
  Whether that asymmetry is deliberate.
- The set/read-length constraint is a parser property enforced only by convention. Nothing
  fails at build time if a new command violates it — a static assertion over the table would
  catch the next `FW`.
- Which `unsupported_cmd` stubs exist and how the radio replies to unknown commands (the `?`
  error response?).
- Whether `AI` (auto-information) actually pushes unsolicited updates or is a no-op.


## Sideband selection: `bands[].mode` is a band default, not the current mode

Fixed 2026-07-31, after `MD2;` was observed tuning a 40 m receiver to LSB.

The receive sideband is not stored anywhere directly. It is the **sign** of
`bands[].FLoCut_Hz`/`FHiCut_Hz` - stored in the convention of the band's default
modulation, negative below the carrier for an LSB-default band - **mirrored by
`InitFilterMask()` (`DSP_FIR.cpp:1167`) iff `ED.modulation[activeVFO]` differs from
`bands[].mode`**. That mirroring is the only mechanism that produces the opposite
sideband.

So the two fields are not two copies of the same thing:

| field | meaning | written by |
|---|---|---|
| `ED.modulation[activeVFO]` | the **current** modulation - what `Demodulate()` switches on and the display shows | front-panel DEMODULATION button, `SetModulation()`. Persisted. |
| `bands[].mode` | the band **default** - the fixed reference the mirroring is measured against | nothing at run time. Rebuilt from the table in `Globals.cpp` each boot; not persisted. |

`SetModulation()` used to write both. Making the default agree with the newly
requested modulation switches the mirroring *off*, so the band-default passband
comes back: on 40 m, `MD2;` (select USB) tuned the radio to LSB, while the display
- which reads `ED.modulation[]` - went on saying USB. `MD_read` and `IF_read` read
the same field and so announced the band default rather than the mode in use,
which is how the fault was initially misdiagnosed at the bench.

`FL_read`/`FW_read`/`FW_write`/`FilterSetSSB` (`DSP_FFT.cpp:515`) and `VolumeScale`
(`DSP.cpp:225`) legitimately keep using `bands[].mode`: they manipulate the
*stored* cutoffs, which are in the band-default convention. Leave them.

**Method note.** `CAT_test.cpp` had a test named `MD_write_SetsModulationNotJustBandMode`
asserting that `bands[].mode` was written - it encoded the defect as the contract and
passed for its whole life. It is now `MD_write_SetsModulationAndLeavesTheBandDefault`,
and the new tests assert the **effective passband** rather than field values, which is
the only formulation that could have caught this.
