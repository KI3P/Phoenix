---
title: CAT Control (Kenwood emulation)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [cat, kenwood, ts-480, ts-2000, serial, rig-control, usb]
source_refs: []
related: ["[[overview]]", "[[main-loop]]", "[[mode-state-machine]]", "[[tune-frequency-control]]", "[[persistent-config]]"]
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

The core is a static table of `valid_command` entries (`NUM_SUPPORTED_COMMANDS = 25`):
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

## Implemented commands (25)

| Group | Commands |
|---|---|
| Frequency / VFO | `FA`/`FB` (VFO A/B freq), `FR`/`FT` (RX/TX VFO select), `FW` (filter width) |
| Mode / status | `MD` (mode), `IF` (transceiver status string), `ID` (radio id) |
| Band | `BU`/`BD` (band up/down) |
| Gain / audio | `AG` (AF gain), `MG` (mic gain), `PC` (power control) |
| DSP | `NR` (noise reduction), `NT` (notch), `VX` (VOX) |
| Keyer | `KS` (keyer speed / WPM) |
| PTT | `TX` (transmit), `RX` (receive) |
| Misc | `AI` (auto-info), `PS` (power on/off), `PD`, `DB` |
| **Custom (non-Kenwood)** | `ED` (dump the `ED` config), `PR` (dump the `hardwareRegister`) |

`ED` and `PR` are Phoenix extensions explicitly marked "NOT a Kenwood keyword"
(`CAT.cpp:119-120`) — handy debug dumps of [[persistent-config]] and the
[[filter-boards]] `hardwareRegister`.

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
  so CAT PTT obeys the same TX interlocks/keying as physical PTT.

Read handlers serialize state back: `IF_read` builds the long TS-480 status string (VFO freq,
mode, RX/TX, etc.); `FA_read`/`MD_read`/`KS_read`/… each return one field.

## Open questions
- Reconcile TS-480 (`CAT.cpp`) vs TS-2000 (`CAT.h`) and verify the `IF` status-string field
  layout against `code/docs/ts_480_pc.pdf`.
- Which `unsupported_cmd` stubs exist and how the radio replies to unknown commands (the `?`
  error response?).
- Whether `AI` (auto-information) actually pushes unsolicited updates or is a no-op.
