---
title: Audio I/O (MainBoard_AudioIO — OpenAudio quad-I2S graph)
type: module
status: draft
created: 2026-06-15
updated: 2026-06-15
tags: [audio, i2s, openaudio, codec, queues, mixers, sidetone, mode-routing]
source_refs: [sources/t41-ep-schematics]
related: ["[[overview]]", "[[hardware-platform]]", "[[dsp-chain]]", "[[mode-state-machine]]", "[[main-loop]]", "[[cw-processing]]", "[[real-time-constraints]]", "[[iq-imbalance-correction]]"]
---

# Audio I/O (MainBoard_AudioIO)

**Files:** `MainBoard_AudioIO.cpp` / `.h`. This module is the **bridge between the physical
audio converters and the DSP chain**: it owns the OpenAudio (chipaudette's float-capable fork
of the Teensy Audio library) graph, routes the four audio streams by radio mode, and exposes
the converted samples to [[dsp-chain]] through ring-buffer queues. The *hardware* behind it
(the SGTL5000, PCM1808, PCM5102 and the I²S pin routing) is documented in [[hardware-platform]];
this page is the *software* graph that drives it.

## Four streams over quad I²S

Audio is a **4-channel I²S in / 4-channel I²S out** interface (`AudioInputI2SQuad i2s_quadIn`,
`AudioOutputI2SQuad i2s_quadOut`, `MainBoard_AudioIO.cpp:121,140`). The channel map
(`:78-115`):

| | ch 0 / 1 | ch 2 / 3 |
|---|---|---|
| **`i2s_quadIn`** | mic L/R (SGTL5000 / Audio Board) | **RX I/Q** (PCM1808) |
| **`i2s_quadOut`** | **TX I/Q** → exciter (SGTL5000) | speaker L/R (PCM5102) |

Throughout the module the **`*_Ex` suffix = the exciter / transmit path** (mic-in and TX-I/Q-out
on the SGTL5000); the un-suffixed names are the **receive path** (RX-I/Q-in and speaker-out).

## The graph

```
i2s_quadIn ─0/1─► modeSelectInEx{L,R} ─► Q_in_{L,R}_Ex   (mic / TX-cal in)
           ─2/3─► modeSelectIn{L,R}   ─► Q_in_{L,R}      (RX I/Q in)
transmitIQcal_oscillator ─► modeSelectInEx ch1            (TX I/Q cal tone)

Q_out_{L,R}_Ex ─► modeSelectOutEx{L,R} ─► i2s_quadOut 0/1 (TX I/Q out)
Q_out_{L,R}    ─► modeSelectOut{L,R}   ─► i2s_quadOut 2/3 (speaker out)
sidetone_oscillator ─► modeSelectOut ch2                  (CW sidetone)
```
(`patchCord1…22`, `:141-162`.) The building blocks:
- **4 input mixers** + **4 `AudioRecordQueue`** (`Q_in_*`) capture samples for the DSP.
- **4 `AudioPlayQueue`** (`Q_out_*`) + **4 output mixers** play DSP results back out.
- **`sidetone_oscillator`** (CW monitor) and **`transmitIQcal_oscillator`** (calibration tone,
  [[iq-imbalance-correction]]).

### Mixers are routing switches
The `AudioMixer4`s aren't for mixing levels — they're **on/off selectors**: `SelectMixerChannel`
sets the chosen channel to gain 1 and the rest to 0; `MuteMixerChannels` zeroes all
(`:198-220`). Channel assignments:

| Mixer | ch0 | ch1 | ch2 |
|---|---|---|---|
| `modeSelectIn{L,R}` (RX in) | RX I/Q | — | — |
| `modeSelectInEx{L,R}` (TX in) | microphone | TX-I/Q **cal** oscillator | — |
| `modeSelectOut{L,R}` (speaker) | RX DSP audio | TX-I/Q monitor | **sidetone** |
| `modeSelectOutEx{L,R}` (TX out) | TX I/Q | — | — |

## Mode-based routing — `UpdateAudioIOState()` (`:280`)

Called on mode transitions (from the [[hardware-state-machine]] / `UpdateRFHardwareState`
path); it reconfigures the graph on the **current [[mode-state-machine]] state** and
**short-circuits if the state is unchanged** (`previousAudioIOState`, `:281`) to avoid audio
glitches:

| ModeSm state(s) | Input queues | Routing |
|---|---|---|
| **SSB/CW receive** (+ several CAL states) | RX I/Q on, mic off | in→RX I/Q (ch0); speaker→RX DSP audio (ch0); TX paths muted |
| **SSB_TRANSMIT** | mic + RX I/Q on | in→mic; out→TX I/Q; speaker muted |
| **CALIBRATE_OFFSET/TX_IQ_MARK** | cal-osc + RX I/Q on | in→cal oscillator (ch1); out→TX I/Q; speaker muted |
| **CW_TRANSMIT_\*_MARK** | RX I/Q + mic off | speaker→**sidetone** (ch2) only; all I/Q paths muted |

Note CW transmit carries **no RF I/Q through the audio path** — the CW carrier is keyed in
hardware (`CWon`/`CWoff`, [[rf-board]]); the audio side only generates the **sidetone** for
operator monitoring ([[cw-processing]]).

## Hand-off to the DSP chain

The record/play queues are the **ISR ↔ main-loop boundary**: the I²S interrupt fills/drains
them, and [[dsp-chain]] reads/writes them from the main loop ([[real-time-constraints]]):
- **RX in:** `Q_in_{L,R}.readBuffer()/freeBuffer()` (`DSP.cpp:134-145`); `available()` gates a
  full block, `clear()` drops backlog on overrun (`DSP.cpp:164-165`).
- **Mic in:** `Q_in_{L,R}_Ex` (`DSP.cpp:988-1005`).
- **Speaker out:** `Q_out_{L,R}.getBuffer()` (`DSP.cpp:730`); **TX I/Q out:** `Q_out_{L,R}_Ex`
  (`DSP.cpp:1061`).

## Initialization & rate

`InitializeAudio()` (`:446`): `SetI2SFreq(SR[SampleRate].rate)`, allocate
`AudioMemory(500)` + `AudioMemory_F32(10)`, configure the codecs, and set the sidetone
oscillator amplitude/frequency (`:452-475`). `SetI2SFreq()` (`:488`) reprograms the Teensy I²S
PLL for **48 / 96 / 192 kHz** — Phoenix runs **192 kHz** raw ([[multirate-decimation]]).
`WarmUpAudioIO()` clears the I²S/codec power-on glitch that otherwise corrupts the first PTT.

## Codec control objects
- **`sgtl5000_teensy`** (`AudioControlSGTL5000_Extended`) — the **real SGTL5000** on the Teensy
  Audio Board: `inputSelect(MIC)`, `micGain(ED.currentMicGain)` (`:320`,
  `UpdateTransmitAudioGain`), line levels.
- **`pcm5102_mainBoard`** (`AudioControlSGTL5000`) — *named* for the **PCM5102 speaker DAC** but
  **mistyped**: the PCM5102 is a control-less I²S part with no I²C, so its `.enable()/
  .inputSelect()` calls are **no-ops**. See the discrepancy note in [[hardware-platform]] — a
  code-clarity fix, not a bug.

## Open questions
- Whether the `transmitIQcal_oscillator` amplitude/frequency are set per-calibration or fixed.
- Exact `AudioMemory` headroom vs. the worst-case quad-stream block backlog.
