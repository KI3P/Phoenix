---
title: CAT Control (Kenwood emulation)
type: module
status: draft
created: 2026-06-08
updated: 2026-07-31
tags: [cat, kenwood, ts-480, ts-2000, serial, rig-control, usb]
source_refs: []
related: ["[[digital-mode]]", "[[overview]]", "[[main-loop]]", "[[mode-state-machine]]", "[[tune-frequency-control]]", "[[persistent-config]]", "[[sample-rate-switching]]", "[[filter-hil-test]]", "[[audio-equalizer]]", "[[cw-processing]]", "[[tx-filter-hil-test]]"]
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

## Protocol ⚠️ TS-480 vs TS-2000
The implemented dialect is the **Kenwood** 2-letter command set. The two source docs
**disagree on which radio**: `CAT.cpp:21` says *"Kenwood TS-480 CAT Interface (partial)"* (and
`code/docs/ts_480_pc.pdf` is the reference), but `CAT.h` says *"Kenwood TS-2000 protocol"*. The
two share most commands so it works either way, but the docs should be reconciled — logged in
[[documentation-todos]].

## Architecture: a command-dispatch table

The core is a static table of `valid_command` entries (`NUM_SUPPORTED_COMMANDS = 32`,
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

## Implemented commands (32)

| Group | Commands |
|---|---|
| Frequency / VFO | `FA`/`FB` (VFO A/B freq), `FR`/`FT` (RX/TX VFO select), `FW` (filter high cut) |
| Mode / status | `MD` (mode), `IF` (transceiver status string), `ID` (radio id) |
| Band | `BU`/`BD` (band up/down) |
| Gain / audio | `AG` (AF gain), `MG` (mic gain), `PC` (power control) |
| Digital mode | `DG` (enter/leave), `DR` (receive level 000-100), `DS` (TX path counters, read-only) |
| DSP | `NR` (noise reduction), `NT` (notch), `VX` (VOX) |
| Keyer | `KS` (keyer speed / WPM) |
| PTT | `TX` (transmit), `RX` (receive) |
| Misc | `AI` (auto-info), `PS` (power on/off), `PD`, `DB` |
| **Custom (non-Kenwood)** | `ED` (dump the `ED` config), `PR` (dump the `hardwareRegister`), **`SR`**, **`CF`**, **`EQ`**, **`FL`** |

`ED` and `PR` are Phoenix extensions explicitly marked "NOT a Kenwood keyword" — handy debug
dumps of [[persistent-config]] and the [[filter-boards]] `hardwareRegister`.

### DSP-settings commands (added 2026-07, `CAT.cpp:132-135`)

Added so [[filter-hil-test]] could drive settings that were previously **touchscreen-only**.

| Cmd | Set | Read | Meaning |
|---|---|---|---|
| `SR` | `SRn;` | `SR;` | Sample rate: `0` = 176.4 ksps, `1` = 192 ksps ([[sample-rate-switching]]). **Rejected while transmitting** — it reconfigures the I²S clock and rebuilds the whole DSP chain (`CAT.cpp:804-827`). |
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

**`SR` is refused while in digital mode**, because that mode pins the sample rate.

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
front of `catCommand[128]`. It never crashed, but `ID;`, `IF;`, `PD;`, `ED;` and `PR;` reached
their read handlers only because the preceding byte happened not to be `';'`. Both tests are now
guarded with `> 0`.

⚠️ **Test the dispatcher, not just the handler.** `CAT_test.cpp` already asserted that TX accepted
`TX;`, `TX0;` and `TX1;` — but it called `TX_write()` directly, and `TX_write` ignores its
argument. The tests passed for the entire life of the bug. The regression tests added alongside
the fix go through `command_parser()`.

`PC` (power) is the opposite case worth noting alongside it: `PC_write` *does* answer, echoing
`PC%03d;`, so it is one of the few writes that is not silent.

## Three fixes to pre-existing commands

All three affected CAT users on earlier releases:

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
- `SR` is interlocked against transmit, but the **menu path is not** ([[sample-rate-switching]]).
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
