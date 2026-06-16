---
title: Phoenix Wiki Index
type: index
status: stub
created: 2026-06-08
updated: 2026-06-16
---

<!-- updated 2026-06-16: 43 content pages (firmware 21, theory 12, hardware 4, roadmap 6) + 1 source (7 schematics); split board *electronics* out of the firmware module pages into hardware/rf-board-electronics + hardware/filter-board-electronics (firmware/ rf-board & filter-boards now describe the code/API, hardware/ owns chips/pinouts/decode chains). Earlier (06-15): lint clean; fixed stale 3×AD8307→2×, phantom 2nd SGTL5000, deci→centi-Hz; added firmware/audio-io. -->


# Phoenix Wiki — Index

Catalog of every page in the wiki, grouped by category. The LLM updates this on every
ingest. To answer a query, read this first to locate relevant pages, then drill in.

See [[CLAUDE]] for the maintenance schema and [[log]] for the chronological record.

## Firmware
_Modules, architecture, state machines, DSP chains, design decisions, code heritage._

**Architecture**
- [[overview]] — top-level firmware architecture: layers, execution flow, module map.
- [[state-machine-architecture]] — how the StateSmith-generated SM layer works.
- [[real-time-constraints]] — the ~10 ms loop budget and timing rules.
- [[spectrum-refresh-floor]] — why refresh ≈ NCHUNKS×T_loop; the 11.7→18.8 fps optimization. *(draft)*
- [[code-heritage]] — lineage from DD4WH Teensy Convolution SDR; contributor list; GPLv3.

**State machines**
- [[mode-state-machine]] — ModeSm: SSB/CW × RX/TX, iambic keyer, calibration entry (generated). *(draft)*
- [[ui-state-machine]] — UISm: splash→home→menu stack + tool screens (DFE/EQ/BIT/cal) (generated). *(draft)*
- [[tune-frequency-control]] — Tune.cpp: frequency math (Fs/4 shift, fine tune, CW offset) + PA power-cal math. Not a state machine. *(draft)*
- [[hardware-state-machine]] — HardwareSm (hand-written) maps mode→RF/tune hardware; drives 4 cal sub-machines. *(draft)*
- [[calibration-state-machines]] — the 4 generated cal SMs: 3 identical minimize-sweeps + PowerCalSm curve-fit. *(draft)*
- [[built-in-test]] — startup I²C presence self-test (BIT struct → status screen). *(draft)*

**Hardware-abstraction modules** _(the code; physical electronics live under Hardware below)_
- [[rf-board]] — `RFBoard.cpp` API: Si5351 3-VFO quadrature LO (phase-reg vs <3.2 MHz timed-delay), attenuators, T/R, cal loopback → electronics in [[rf-board-electronics]]. *(draft)*
- [[filter-boards]] — `BPFBoard`/`LPFBoard` API: BPF + LPF/RF-back-end relays via shared hardwareRegister; AD7991 SWR/power math → electronics in [[filter-board-electronics]]. *(draft)*
- [[front-panel]] — 2×MCP23017 encoders/buttons/LEDs; two-tier ISR+1ms-timer I²C servicing (encoder-i2c-speed branch). *(draft)*

**Signal processing & UI**
- [[dsp-chain]] — DSP.cpp/FFT/FIR/Noise/CW on OpenAudio; the Convolution-SDR core.
- [[audio-io]] — MainBoard_AudioIO: OpenAudio quad-I²S graph, mode-based mixer routing, RX/TX/mic/speaker queues, sidetone. *(draft)*
- [[cw-processing]] — CW audio filter + adaptive Morse decoder (Goertzel + timing histograms + Morse tree). *(draft)*
- [[display-subsystem]] — RA8875 dirty-flag pane model (13 home panes), DrawDisplay router on UISm state. *(draft)*

**Control flow & config**
- [[main-loop]] — Loop.cpp: `loop()` sequence + interrupt FIFO + CW key/PTT handling.
- [[cat-control]] — CAT.cpp: Kenwood (TS-480/2000) emulation, 25-command dispatch table on SerialUSB1; +custom ED/PR dumps. *(draft)*
- [[persistent-config]] — the `ED` struct (single source of truth) → JSON on LittleFS+SD; Config.h/Globals/ParamSave. *(draft)*

## Theory
_DSP/SDR concepts, calibration, domain "why it works". Constructed from code + DSP
fundamentals (no copy of the Purdum & Peter book). Code claims cited `file:line`._

- [[theory-overview]] — the DSP/SDR theory spine; signal path end to end, key numbers.
- [[iq-quadrature-sampling]] — complex baseband; sign-of-frequency; image problem. *(draft)*
- [[ssb-phasing-method]] — SSB by phasing: ±45° Hilbert TX pair + freq-domain RX. *(draft)*
- [[fast-convolution-filtering]] — FFT overlap-save channel filter; the Convolution-SDR core. *(draft)*
- [[multirate-decimation]] — ÷8 RX / ÷16 TX polyphase rate changes; tap-count tradeoff. *(draft)*
- [[iq-imbalance-correction]] — per-band amp/phase correction & 3-pass calibration sweep. *(draft)*
- [[agc-design]] — wdsp look-ahead AGC: ring-buffer attack, hang state machine. *(draft)*
- [[noise-reduction]] — Kim / MMSE-spectral / LMS-notch NR. *(draft)*
- [[synchronous-am-detection]] — SAM: PLL carrier recovery for fade-robust AM. *(draft)*
- [[audio-equalizer]] — 14-band parallel filterbank (RX + TX audio). *(draft)*
- [[tx-carrier-null]] — LO-leakage suppression via per-band DC offset (distinct from image). *(draft)*
- [[zoom-fft]] — spectrum-display zoom via IIR decimation (raw 192 kHz, 375 Hz base bin). *(draft)*

## Hardware
_The physical T41-EP platform the firmware drives: boards, chips, buses, power. Distinct from
the firmware/ module pages (which describe the code). Derived from `Config.h` + board drivers._

- [[hardware-platform]] — T41-EP platform overview: Teensy 4.1, board stack, single-SGTL5000
  + PCM1808/PCM5102 audio, RF chain, power/PA/XVTR options. *(stable)*
- [[i2c-bus-map]] — the three I²C buses (Wire/Wire1/Wire2), every device + address, and why
  the front panel gets its own 400 kHz bus. *(stable)*
- [[rf-board-electronics]] — RF board silicon: dual/single Si5351 LO + revisions, PE4312
  attenuators (GPIO-bit→pin table, designators), ADT/PSA/MAR mixer front end, I/Q amps. *(draft)*
- [[filter-board-electronics]] — BPF/LPF/PA back-end chips: MASWSS switches, CD74HC4514/74HC237
  decoders + ULN2803/HF41F relays, external per-band LPF board, AD8307/AD7991 SWR sensing. *(draft)*

## Roadmap
_Plans for future development, proposals, open questions._

- [[documentation-todos]] — running list of wiki pages to write + code/comment discrepancies
  and possible bugs found while documenting (incl. a ⚠ possible NR channel-routing bug).
- [[development-backlog]] — software-dev tasks mirroring the open GitHub issues (index).
  - [[transmit-spectrum]] (#10) — see the TX spectrum; largely built, needs verify.
  - [[manual-notch]] (#11) — 3-way notch (off/auto/manual); manual mode is new work.
  - [[usb-audio]] (#13) — bi-directional USB audio; in active debugging.
  - [[openaudio-library]] (#14) — change OpenAudio for audio quality; foundational.

## Sources
_One summary page per ingested raw source._

- [[t41-ep-schematics]] — the seven KiCad board schematics (main/RF/attenuator/dual-clock/BPF/
  LPF-control/LPF-filter); the first physical-hardware source. *(draft)*
