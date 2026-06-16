---
title: Hardware Platform (T41-EP)
type: hardware
status: stable
created: 2026-06-14
updated: 2026-06-16
tags: [hardware, teensy, t41-ep, board-stack, codec, i2c, platform]
source_refs: [sources/t41-ep-schematics]
related: ["[[overview]]", "[[i2c-bus-map]]", "[[rf-board-electronics]]", "[[filter-board-electronics]]", "[[rf-board]]", "[[filter-boards]]", "[[front-panel]]", "[[display-subsystem]]", "[[hardware-state-machine]]", "[[audio-io]]"]
---

# Hardware Platform (T41-EP)

The physical radio Phoenix runs on: a **Teensy 4.1**-based **T41-EP** software-defined HF
transceiver built as a stack of interconnected boards. This page is the entry point for the
`hardware/` area — the *electronics the firmware drives*. For the **code** that drives each
board, follow the firmware module links; this page describes the silicon and how it is wired.

> Source note: confirmed against the **KiCad schematics** ([[t41-ep-schematics]], V012-era)
> as of 2026-06-14, plus the firmware hardware-abstraction code (`Config.h`, board drivers,
> `MainBoard_AudioIO.cpp`). Chip part numbers and connectors are schematic-confirmed; a few
> pin-level wiring claims remain *(infer)* until nets are fully traced.

## Compute core — Teensy 4.1
- **MCU:** NXP i.MX RT1062, **ARM Cortex-M7 @ 600 MHz**, single-precision hardware FPU + DSP
  extensions (used heavily by the CMSIS-DSP FIR/FFT routines — see [[multirate-decimation]],
  [[fast-convolution-filtering]]).
- **Memory used by Phoenix:** tightly-coupled RAM for DSP buffers; `DMAMEM` region for audio
  DMA buffers (the cold-boot uninitialized-`DMAMEM` class of bug is noted in
  [[fast-convolution-filtering]] and [[noise-reduction]]).
- **Storage:** on-chip program flash hosts a **LittleFS** filesystem (~1 MB) for the JSON
  config; an optional **SD card** (Teensy 4.1 built-in socket) mirrors it. See
  [[persistent-config]] — there is **no true EEPROM** despite the historical `EEPROMData` name.
- **USB:** dual serial (Arduino "Dual Serial"): one port for debug, `SerialUSB1` for
  **CAT control** ([[cat-control]]).
- **Three I²C buses** (Wire/Wire1/Wire2) fan out to the peripheral boards — see [[i2c-bus-map]].

## The board stack
(Reference designators from the [[t41-ep-schematics]].)

| Board | Key chips | Role | Firmware |
|---|---|---|---|
| **Main board** (`V012`) | Teensy 4.1 (U8), PCM1808 ADC (U6), PCM5102 DAC (U5), ATtiny85 (U4) + SUP90P06 P-FETs (soft power) | Compute, RX I/Q digitizing, speaker audio, all I/O fan-out, off-board RA8875 drive | [[display-subsystem]], audio (below) |
| **Teensy Audio Board** | **SGTL5000** codec (U7) | Mic input + TX I/Q output path (the *only* SGTL5000) | audio (below) |
| **RF board** (`V012`) | Dual-Si5351 + PE4312 daughterboards, ADT1-1/ADT2-1T mixer xfmrs, PSA-8A+/MAR-3SM+ MMICs, AD8599/OPA2209/MC33078 I/Q amps, MCP23017 (U21, 0x27) | Quadrature LO + mixer, T/R switching, RX/TX attenuators, cal loopback | [[rf-board]] |
| **BPF / exciter-filter board** | ~22× MASWSS0179 RF switches, MCP23017 (U35, 0x24) | Switched per-band band-pass filter banks (RX/TX image rejection) | [[filter-boards]] |
| **LPF / 100 W PA control board** | HF41F relays + ULN2803, CD74HC4514/74HC237 decoders, 2× AD8307 + AD7991 (U20, 0x28/0x29), MCP23017 (U15, 0x25) | Band-LPF decode + PA/XVTR/antenna routing (4 ANT ports), SWR/power sensing (filters on a separate board) | [[filter-boards]] |
| **Front panel** | 2× MCP23017 (0x20/0x21) | Encoders, buttons, LEDs | [[front-panel]] |

The firmware treats each board through a clean driver module (the "hardware abstraction"
principle, [[overview]]), which is what lets the unit tests mock the hardware.

## Audio front/back end
Audio I/O is a **4-channel (quad) I²S** interface via the OpenAudio (Teensy Audio fork)
graph (`MainBoard_AudioIO.cpp` — the software side is [[audio-io]]). There is **one physical SGTL5000** (on the Teensy Audio Board,
U7); the main board does its own conversion with a **PCM1808** ADC (U6) and **PCM5102** DAC
(U5). Three converters, three jobs (schematic-confirmed, [[t41-ep-schematics]]):
- **SGTL5000** (Audio Board, `sgtl5000_teensy`, I²C addr **LOW**) — **double-duty**: microphone
  **in** (SSB voice) *and* exciter **TX I/Q out** (`MainBoard_AudioIO.cpp:450-457`).
- **PCM1808** (U6, main board) — **RX I/Q in** (ADC).
- **PCM5102** (U5, main board) — **speaker audio out** (DAC).

> ⚠ **Code-naming quirk (not a missing chip).** `MainBoard_AudioIO.cpp` declares a control
> object **`pcm5102_mainBoard` typed `AudioControlSGTL5000`** (addr HIGH) and the header says
> "two SGTL5000 codecs." In fact that object represents the **PCM5102 speaker DAC**, *not* a
> second SGTL5000 — there is only one SGTL5000 on the whole radio. The PCM5102 (and PCM1808)
> are **control-less I²S parts** with no I²C, so the `AudioControlSGTL5000` `.enable()/
> .inputSelect()` calls on this object are effectively **no-ops**; the I²S *data* path is what
> carries speaker audio. So: harmless mistype/comment, worth a code clarity fix, not a bug.
> → [[t41-ep-schematics]]

The quad-channel map:

```
i2s_quadIn   ch0/1 = mic L/R (Audio Board)     ch2/3 = RX I/Q (PCM1808)
i2s_quadOut  ch0/1 = TX I/Q  (Audio Board)     ch2/3 = speaker L/R (PCM5102)
```

I²S sample rate is PLL-configured for **48 / 96 / 192 kHz** (`SetI2SFreq`); Phoenix runs at
**192 kHz** raw ([[multirate-decimation]], [[zoom-fft]]). Mode-based mixer routing in
`UpdateAudioIOState()` connects mic→DSP→TX-I/Q on transmit and RX-I/Q→DSP→speaker on receive.

**I²S pin routing (Teensy quad-I2S, schematic-confirmed 2026-06-15).** The three clocks are
**shared** by all three converters (U5 PCM5102, U6 PCM1808, U7 SGTL5000 adapter):

| Signal | Teensy pin | Net | To |
|---|---|---|---|
| MCLK (master clock) | GPIO23 | `CLK` | U5, U6, U7 |
| LRCLK (word clock) | GPIO20 | `LRCK` | U5, U6, U7 |
| BCLK (bit clock) | GPIO21 | `BCK` | U5, U6, U7 |
| Data out → speaker DAC | GPIO32 | `DAUDIO_OUT` | U5 (PCM5102) |
| Data in ← RX-I/Q ADC | (Teensy I²S in) | `ADC_IN` | from U6 (PCM1808) |

`DAUDIO_OUT` (GPIO32) is the Teensy quad-I2S **OUT1B** feeding the PCM5102; `ADC_IN` carries the
PCM1808's RX I/Q into the Teensy. The **SGTL5000 adapter (U7)** uses the standard Teensy
audio-shield data pins — **OUT1A (GPIO7)** for TX I/Q and the mic data-in line — sharing the
same three clocks *(inferred from the Teensy quad-I2S assignment; not separately net-labelled)*.

## RF signal chain (physical)
Antenna → **LPF (harmonic) + BPF (image) filter banks** → **RF board**: the Si5351 supplies a
**quadrature LO** that, with the codec, forms the I/Q mixer; T/R relays and attenuators set
direction and level. On TX the path adds the **PA / XVTR / antenna** back-end with **AD7991**
SWR/forward-power sensing. The [[hardware-state-machine]] sequences all of this (RX components
disabled before TX enabled, with relay/PIN-diode settling delays). Detailed per-board behaviour
lives in [[rf-board]] and [[filter-boards]] (the firmware modules) with the chip-level
schematic detail in [[rf-board-electronics]] and [[filter-board-electronics]]; the DSP side is
[[iq-quadrature-sampling]] / [[ssb-phasing-method]].

## Power control & graceful shutdown (ATtiny85, U4)
The main board's **ATtiny85 (U4)** is a dedicated **soft-power controller** — it gates the
main power **FET** (the SUP90P06 P-MOSFETs) and runs a 3-state machine so the Teensy can save
state before power is cut. It is *not* an I²C device or sensor; it talks to the Teensy over a
**two-wire handshake**. Firmware: `code/src/ATTiny85_On_Off/ATTiny85_On_Off.ino` (separate
sketch from PhoenixSketch); Teensy side in `Loop.cpp`.

| ATtiny pin | Dir | Net | Function |
|---|---|---|---|
| PB3 (pin 2) | in (ext. pulldown) | PANEL_SWITCH | front-panel ON/OFF button (HIGH = pressed) |
| PB4 (pin 3) | out | FET_SWITCH | power FET gate: **HIGH = radio on**, LOW = off |
| PB1 (pin 6) | out → Teensy | START_SHUTDOWN | tells Teensy to run shutdown code (`BEGIN_TEENSY_SHUTDOWN` on the Teensy) |
| PB2 (pin 7) | in ← Teensy | SHUTDOWN_COMPLETE | Teensy raises this when shutdown is finished |

**Sequence:** *OFF* → button press turns the FET on (boots Teensy), 3 s debounce → *ON* →
button press asserts START_SHUTDOWN → *SHUTDOWN* (power still on) while the Teensy saves
parameters, then the Teensy asserts SHUTDOWN_COMPLETE → ATtiny drops the FET → *OFF*. The
Teensy polls the request each loop (`Loop.cpp:1499 if (digitalRead(BEGIN_TEENSY_SHUTDOWN))
ShutdownTeensy()`); `ShutdownTeensy()` (`Loop.cpp:1414`) saves state then
`digitalWrite(SHUTDOWN_COMPLETE,1)`. The CAT **PS** command (`CAT.cpp:670`) invokes the same
graceful shutdown in software. So the firmware **depends** on the ATtiny for orderly power-down
(it is what makes "save settings on power-off" work — see [[persistent-config]], [[main-loop]]).

## Power & options (from `Config.h`)
- Optional **100 W PA** (`PA100Wactive`) vs barefoot; optional **transverter (XVTR)** path.
- Optional **analog SWR** on Teensy ADC pins 26 (FWD) / 27 (REV) instead of the default
  AD7991 digital SWR (`USE_ANALOG_SWR`, `Config.h`).
- Single- vs **dual-VFO** Si5351 hardware (`SI5351_DUAL_VFO_ADDR 0x61`) — see [[rf-board]].

## Open questions
- Power rails/current budget and PA bias details (IRF630/L7808/TC4428 on the LPF board).

_(The PE4312↔MCP23017 mapping, the LPF band-code→relay decode, the U21 upper bits, and the
I²S/MCLK routing have all been traced — see Resolved below and the module pages.)_

## Resolved by the schematic ([[t41-ep-schematics]], 2026-06-14/15)
- **I²S routing** — shared clocks MCLK=GPIO23 (`CLK`), LRCLK=GPIO20 (`LRCK`), BCLK=GPIO21
  (`BCK`) to all three converters; `DAUDIO_OUT`=GPIO32 → PCM5102; `ADC_IN` ← PCM1808; SGTL5000
  on the standard audio-shield data pins. See the audio section.
- **RF attenuators** — MCP23017 U21 GPIOA→RX / GPIOB→TX PE4312s, low 6 bits = 0.5 dB-LSB word;
  **upper bits GPA6/7, GPB6/7 unused/NC**. Designators are revision-dependent. → [[rf-board-electronics]]
- **LPF band select** — GPIOB[0:3] BCD → U12 CD74HC4514 (Yn = BCD) → ULN2803 → external filter
  board: **one dedicated 7-element LPF per band** + NF bypass. → [[filter-board-electronics]]
- **ATtiny85 (U4) = soft-power controller** with a Teensy graceful-shutdown handshake (not an
  I²C/sensor device) — see the dedicated section above.
- **One SGTL5000, not two** — RX path is PCM1808 + PCM5102; the code's second "SGTL5000" is a
  naming artifact (see discrepancy box above).
- **RA8875 is off-board** — main-board J7 "Display" + J8 "Display Voltage" feed an external
  display module, which is why no RA8875 symbol appears.
- **Attenuators = 2× PE4312** digital step attenuators (31.5 dB / 0.5 dB) → the 31.5 in
  `Set{RX,TX}Attenuation`. **Dual Si5351** confirmed. → [[rf-board-electronics]]
- **Power/SWR = 2 AD8307 log detectors (U14 fwd, U19 rev) → AD7991 CH0/CH1**, ref = REF3440 on
  Vin3/Vref. Net-traced 2026-06-14; the default firmware log conversion is **correct** (no bug
  — the linear formula belongs to the unused analog-diode path). → [[filter-board-electronics]]
