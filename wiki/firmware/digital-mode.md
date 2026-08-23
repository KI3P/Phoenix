---
title: Digital Mode (USB Audio)
type: module
status: draft
created: 2026-07-30
updated: 2026-07-31
tags: [digital-mode, usb-audio, ft8, wsjtx, 176k, sample-rate, mode-state-machine, cat, verified-on-hardware]
source_refs: []
related: ["[[mode-state-machine]]", "[[sample-rate-switching]]", "[[audio-io]]", "[[dsp-chain]]", "[[cat-control]]", "[[usb-audio]]", "[[runtime-filter-design]]", "[[hardware-state-machine]]"]
---

# Digital Mode (USB Audio)

A third operating mode alongside SSB and CW, for FT8/JS8/PSK31 and anything else driven
from a PC. In receive the demodulated audio is streamed to the host over USB audio (and
still to the speaker); in transmit the audio source is the host instead of the microphone.

## The rate coincidence that makes it simple

Teensy USB audio is a fixed **44.1 kHz** endpoint. Phoenix's DSP runs at 176.4 or 192 ksps.
At **176.4 ksps** everything lines up exactly:

| quantity | at 176.4 ksps | at 192 ksps |
|---|---|---|
| receive audio tap after `FIR_int1` (Fs/4) | **44,100 Hz** | 48,000 Hz |
| DSP block rate (Fs / 2048) | 86.1328 Hz | 93.75 Hz |
| samples/s at that tap (512 per block) | **44,100** — exact | 48,000 |
| AudioStream graph clock (Fs / 128) | **1378.125 Hz = 4 × 344.53** | 1500 Hz |

So the demodulated audio *is already* at 44.1 kHz, one DSP block *is already* 512 samples,
and the audio library's graph clock is *exactly* 4× the USB audio block rate. Nothing is
resampled and no Teensy core file is modified.

**Two clocks, do not conflate them.** 176.4 ksps is the radio's ADC/DSP rate (what the SR
menu and `SR0;` select). 44.1 kHz is the demodulated *audio* rate and what the USB endpoint
declares to the PC. The second is not an independent choice — it is what Fs/4 equals at the
first.

Because of this, **entering digital mode forces 176.4 ksps** and leaving restores the
previous rate. See [[sample-rate-switching]].

## State machine

`ModeSm.drawio` gained a `DIGITAL_STATES` composite inside `NORMAL_STATES`, containing
`DIGITAL_RECEIVE` (initial) and `DIGITAL_TRANSMIT`, plus a `TO_DIGITAL_MODE` event.

| From | Event | Guard | To |
|---|---|---|---|
| `SSB_RECEIVE` | `TO_DIGITAL_MODE` | — | `DIGITAL_STATES` |
| `CW_RECEIVE` | `TO_DIGITAL_MODE` | — | `DIGITAL_STATES` |
| `DIGITAL_RECEIVE` | `TO_SSB_MODE` | — | `SSB_RECEIVE` |
| `DIGITAL_RECEIVE` | `TO_CW_MODE` | — | `CW_RECEIVE` |
| `DIGITAL_RECEIVE` | `PTT_PRESSED` | `IsTxAllowed()` | `DIGITAL_TRANSMIT` |
| `DIGITAL_TRANSMIT` | `PTT_RELEASED` | — | `DIGITAL_RECEIVE` |

The sample-rate switch lives on the **composite's** enter/exit actions
(`EnterDigitalMode()` / `ExitDigitalMode()` in `Mode.cpp`), not on the leaf states. That is
what makes the rate restore on *every* way out, including the calibration events dispatched
at the `NORMAL_STATES` level — no extra wiring needed. It also means a PTT round trip does
not re-run the entry action and clobber the saved rate.

**Gotcha worth remembering.** Because those actions run as the composite's entry/exit, they
execute while `modeSM.state_id` is still `DIGITAL_STATES` — the leaf has not been entered
yet. `ChangeSampleRate()` ends in `UpdateTuneState()`, so `DIGITAL_STATES` has to appear in
the receive group of both `UpdateTuneState()` and `UpdateRFHardwareState()`
(`HardwareSm.cpp`). Without it they fall through to `default:` and call `HandleTuneState()`
with a stale tune state, reprogramming the VFO wrongly on every entry and exit.
`ModeSm.DigitalModeTransitionsHandleTheCompositeState` pins this.

## USB transport: pacing, not reimplementation

`USBAudio.cpp` keeps the **stock Teensy `AudioInputUSB` / `AudioOutputUSB`** — the same
classes OpenAudio's `USB_Audio_F32.h` wraps — and with them all the core descriptor, DMA,
ISR and isochronous-feedback code. No USB protocol code is written here.

What does *not* work is putting them in the `AudioConnection` graph directly: the graph is
clocked by the I2S DMA at Fs/128, but these objects must be updated at 44100/128 = 344.53 Hz.
Overfed 4:1, `AudioOutputUSB` only holds two blocks and discards the rest, and
`AudioInputUSB`'s feedback loop is calibrated for the nominal rate.

The fix is a 4:1 pacer, exact at 176.4 ksps:

- `update_all()` skips any `AudioStream` whose protected `active` flag is false
  (`AudioStream.cpp:445`). Trivial subclasses expose `deactivate()` to clear it.
- `USBAudioInit()`, called from `InitializeAudio()`, takes the six objects off the graph
  clock. This must happen at **startup**, not at mode entry — until it does, `update_all()`
  is running them 4× too fast and `AudioInputUSB` asks the host for samples faster than we
  can consume them.
- `USBAudioPacer` is the only object left in `update_all()`. It runs in the audio ISR and
  drives the six manually on every fourth call.
- `AudioConnection::connect()` sets `active = true` on both ends
  (`AudioStream.cpp:318,321`), so `deactivate()` has to run *after* the static
  `AudioConnection`s are constructed — hence a function, not constructors.

No ring buffer is needed: `AudioPlayQueue::MAX_BUFFERS` is 80 and
`AudioRecordQueue::max_buffers` is 209 on Teensy 4, so the stock queues absorb the DSP
delivering four blocks at once every 11.6 ms. The play queues are capped at 24 blocks and
prefilled to half depth (see "Fault 1" below - 8 with no prefill left no underrun margin and
dropped audio constantly), and set `NON_STALLING` so `getBuffer()` never blocks the main
loop. The record queues have no cap of their own, so `USBAudioReadTx()` trims them.

Two library quirks found the hard way: `AudioPlayQueue::stop()` is declared in the header
but **never defined** in Audio 1.3, and Teensy's CMSIS declares `arm_float_to_q15`'s source
pointer non-const.

## DSP taps

**Receive** — `InterpolateReceiveData()` in `DSP.cpp`, between the two interpolation stages,
where the block is 512 samples at exactly Fs/4. Deliberately *before* `AdjustVolume()`, so
the level sent to the PC is independent of the front-panel volume knob - which also means
nothing regulates it, hence `ED.digitalRxLevel` (see "Fault 2" below). The scaling is done
into scratch, not in place: the caller still runs the second interpolation stage over that
same buffer to feed the speaker.

**Transmit** — `ReadUSBTransmitBuffer()` replaces `ReadMicrophoneBuffer()` +
`TXDecimateBy4()`, entering the chain one stage down at `TXDecimateBy2()` (which hardcodes
512 samples in). Everything downstream — `BandEQ`, `TXGain`, Hilbert, IQ correction,
`SidebandSelection`, `PlayIQData` — is untouched, so sideband selection, IQ calibration and
carrier nulling work exactly as they do for SSB.

**The pacing decision that matters.** `ReadUSBTransmitBuffer()` still gates on
`Q_in_L_Ex`/`Q_in_R_Ex` availability and drains those microphone blocks, discarding the
samples. They are used purely as a clock: they fill from the I2S DMA at exactly the rate the
transmit DAC drains, so `TransmitProcessing()` stays locked to the I2S clock. Letting the
*host's* USB clock pace the DSP loop is what starved the transmit output queue in the earlier
abandoned attempt (see [[usb-audio]]). On a host underrun the function emits silence rather
than failing, so the output queue is never left unfed.

Two persisted levels, both 0–100 and both in the Microphone menu:
`ED.digitalDriveLevel` ("USB drive", CAT: none) is the software analogue of microphone gain
on transmit; `ED.digitalRxLevel` ("USB RX level", CAT `DR`) attenuates the receive audio sent
to the host, default 25 = −12 dB.

## Control surfaces

- **Front panel**: the MODE button now cycles **SSB → CW → DIGITAL → SSB**. The DIGITAL leg
  is compiled in only when `AUDIO_INTERFACE` is defined; without it the original SSB ↔ CW
  toggle is unchanged.
- **CAT `DG`**: `DG1;` enters, `DG0;` leaves, `DG;` reads. Non-Kenwood. Rejected outright in
  builds without a USB audio interface.
- **CAT `DR`**: `DR000;`–`DR100;` set the receive level, `DR;` reads it back zero-padded.
- **`MD` is deliberately inert on the mode.** WSJT-X sends `MD2;` to force USB on startup and
  before every transmission; if `SetModulation()` treated that as a request to leave digital
  mode the mode would be impossible to hold. Only `DG0;` or the front panel leaves it. `MD3;`
  (a genuine mode change to CW) *is* honoured.
- **`MD_read` / `IF_read`** report the plain sideband (1/2) in digital mode, so hamlib sees a
  sane mode rather than an unknown one.
- **`TX;` / `RX;`** key and unkey digital transmit — this is how WSJT-X keys the rig.
- **`SR`** is refused in digital mode, because the mode pins the rate.

## Build configuration

Digital mode requires **`usb=serialmidiaudio`** (Serial + MIDI + Audio). Teensyduino has no
stock "Dual Serial + Audio" type, so this build has only one CDC port:

- CAT moves from `SerialUSB1` to the primary `Serial`. `CATSerial` in `SDT.h` is the alias
  that hides this; `CAT.cpp` refers only to `CATSerial`.
- `Debug()` compiles to nothing when `AUDIO_INTERFACE` is defined, or its output would
  corrupt the CAT stream.
- **CAT clients must be pointed at the first CDC port** in this configuration.

`code/.vscode/arduino.json` carries the setting, and the `flash-radio` skill reads it.

## Receive, verified on the bench (2026-07-31)

Injected carrier at 7075 kHz, dial 7074 kHz, USB — a 1 kHz audio tone. Captured
from the ALSA device with `arecord -D hw:2,0 -f S16_LE -r 44100 -c 2`.

**Enumeration.** `Teensy MIDI/Audio` (16c0:048a); `/proc/asound/cardN/stream0`
reports 44100 Hz S16_LE stereo on both directions, playback endpoint `ASYNC` with
sync endpoint 0x86 — the isochronous feedback path the design relies on is live.
Capture length is byte-exact (12 s = 2,116,844 B), so nothing is lost at the ALSA
layer.

**CAT.** `DG1;`/`DG0;`/`DG;` work; `SR;` returns `SR0;` (176.4 ksps) in digital
mode; `MD;`, `FA;`, `IF;` all consistent. CAT is on `/dev/ttyACM0` — the only CDC
port in this build.

**Results, in the order the faults were found and fixed:**

| | first capture | + queue fix | + level fix |
|---|---|---|---|
| dropouts (20 s) | 147 runs, 903 ms, **7.55 %** | 1 run, 2.9 ms, 0.015 % | **0 runs, 0.000 %** |
| tone | 993.35 Hz (smeared) | 999.5894 Hz | 999.5676 Hz |
| peak level | −0.2 dBFS | −0.2 dBFS | **−12.3 dBFS** |
| 2nd harmonic | — | −58.8 dB | **−64.5 dB** |

**No pitch error.** The −0.43 Hz offset on a 1 kHz tone is 0.06 ppm of the
7.075 MHz carrier, i.e. signal-generator versus radio calibration. Tone frequency
measured over the first and last 5 s of a 20 s capture differed by 0.009 Hz, below
the measurement floor. **The 4:1 pacer holds exact rate** — this is the headline
result, since a pacer divisor error would show as a 4× or 25 % frequency ratio.

### Fault 1: play-queue underrun

Every zero run in the first capture was an exact multiple of `AUDIO_BLOCK_SAMPLES`
(128/256/384/512), spaced about 133 ms apart. That is
`AudioOutputUSB::update()` substituting a silence block when its input queue is
empty — so an empty play queue is heard as clean digital silence, not as a glitch,
which makes it easy to misread as a dead signal.

Cause: the queue was capped at 8 blocks and **never prefilled**, so it lived at a
depth of 0–4 blocks. The DSP inherently swings it by 4 blocks every 11.6 ms (four
blocks delivered at once, one consumed every 2.9 ms), leaving no underrun margin
at all; anything that delayed the main loop drained it. The ~133 ms spacing points
at display refreshes as the trigger.

Fix: depth 8 → 24 blocks, prefilled to half. ~35 ms of margin each way for ~35 ms
of added latency, which is nothing to an FT8 decoder. See `USB_PLAY_QUEUE_BLOCKS`.

### Fault 2: no headroom

The stream sat at 98 % of full scale with ~0.15 dB of headroom. This is **not** AGC
action — `ED.agc` was already `AGCOff`, and in that mode the AGC stage
(`DSP.cpp:322`) just applies `fixed_gain = 20.0`, a compile-time constant
(`SDT.h:639`). So the USB level is `RF input × DSP chain gain × 20`, with nothing
regulating it: a stronger signal clips, a weaker one is quiet. Turning AGC *on*
would lower the level but also compress, which digital-mode decoders do not want.

Fix: `ED.digitalRxLevel` (0–100, default 25 = −12 dB), applied in
`USBAudioWriteRx()` into scratch — not in place, because the caller still has to
run the second interpolation stage over that buffer to feed the speaker.
Adjustable from the Microphone menu ("USB RX level") and over CAT as `DR`.
Measured effect: peak −0.2 → −12.3 dBFS and 2nd harmonic −58.8 → −64.5 dB, so the
chain *was* mildly clipping.

### Also fixed: CAT sideband selection

Found while setting up this test and fixed as its own change — `MD2;` selected the
wrong sideband. Pre-existing, in shared SSB/CW code. See [[cat-control]].

## Transmit, verified on the bench (2026-07-31)

AD2 scope probes on the exciter I/Q outputs, 5 W, tone fed into the USB sink.

**Sideband generation is correct.** USB gives phase(Q)-phase(I) = -89.99 deg with
+41.0 dB image rejection; LSB gives +90.12 deg and -41.2 dB. The sign flips, which
is the direct refutation of the abandoned branch's failure mode - that produced
*double* sideband. Amplitude imbalance -0.16 dB, and both channels are continuous
across every DSP block (the branch's signature was one channel valid for only
~9.5 ms of each 11.6 ms block). Two-tone IMD3 -51 dB, IMD5 -69 dB.

**Amplitude response** (constant-amplitude sweep, one key-down): peak at 800 Hz,
-3 dB at ~2243 Hz, 1.9 dB spread over 200-1200 Hz and 2.8 dB over 200-2200 Hz,
then -5.9 dB at 2600, -10.8 at 3000, -17.0 at 3200.

This is the intended SSB passband, not a fault. It is the cascade of
`coeffs12K_8K_LPF_FIR` (decimate-by-2 into the Hilbert, -6 dB at 3500 Hz) and
`FIR_int3_12ksps_48tap_2k7` (the TX audio bandwidth filter, -6 dB at 3039.6 /
-3 dB at 2759 Hz); cascading them explains why the measured -3 dB sits below
`FIR_int3`'s own figure. It is **not** the Hilbert transform - computed from its
actual coefficient tables it is within 0.01 dB and 0.1 deg from 200 Hz up, and
running it at 11.025 kHz rather than its design 12 kHz changes nothing - and not
the equaliser, which is flat.

*Practical consequence:* place FT8 audio in the **500-1500 Hz** region. At 2400 Hz
you are down 4 dB, at 3000 Hz more than 10 dB.

### The record queue was exhausting the audio pool

Transmit showed a 0.53 dB envelope modulation at **344.53 Hz**, which is exactly
44100/128 - the AUDIO_BLOCK_SAMPLES rate. Varying `aplay`'s ALSA period size over
128/256/512/1024 left it at 344.53 Hz throughout (a host-side cause would have
tracked 344/172/86/43 Hz), so the radio was making it.

The `DS` CAT counters found the cause, and it was not the block joins I first
suspected - underruns and trims were both zero in steady state. It was
`depthMax = 208`: the transmit record queue is `begin()`-ed for the whole of
digital mode but only drained in `DIGITAL_TRANSMIT`, so while receiving it filled
to `AudioRecordQueue`'s 209-block ceiling and held **~418 of the 500 audio
blocks** between its two channels. Everything else in the graph was starved of
blocks, and the resulting glitches appeared at the rate the pacer was applying the
allocation pressure.

Fixed by bounding the queue in the pacer rather than only in `USBAudioReadTx()`.
Measured effect:

| | before | after |
|---|---|---|
| envelope ripple (folded) | 0.53 dB | **0.06 dB** |
| sidebands at +/-344 Hz | -27 / -29 dBc | **-74.5 / -75.1 dBc** |
| "sidebands" at +/-86 Hz | -16 / -15 dBc | -26.5 / -33.8 dBc |

The +/-86 Hz row is carrier skirt, not a sideband - both figures were measured too close in.
See the correction below; at proper resolution the after figure is -93 dBc.

| broadband floor | -65 dBc | **-84.6 dBc** |
| queue depth min/max | 11 / 208 | **12 / 12** |
| underruns, trims | 0, 197 | **0, 0** |

47 dB better at the block rate and 20 dB better broadband. Worth remembering as a
general lesson: an unbounded `AudioRecordQueue` does not fail loudly, it quietly
starves every other node in the audio graph.

### The queue fix helped receive as much as transmit

Re-measured with the same 1 kHz carrier at the same level, before and after:

| | before | after (60 s) |
|---|---|---|
| dropouts | 0 | 0 |
| 2nd harmonic | -64.5 dB | **-82.0 dB** |
| noise floor | -119.3 dB | **-135.4 dB** |
| L vs R | differed during settling | identical throughout |

17.5 dB less distortion and 16 dB lower noise floor on receive, from a fix made for
transmit. Holding ~418 of the 500 audio blocks was causing allocation failures in
the receive path too - never enough to show as a dropout, but enough to raise the
floor and generate harmonics. No drift over 60 s, consistent with the 10-minute
receive measurement.

### Correction: there are no +/-86 Hz sidebands

An earlier version of this page reported residual sidebands at the DSP block rate
at -27 dBc. That was a resolution artifact. Those captures were 40.96 ms, giving
24.4 Hz bins, which puts +/-86 Hz only **3.5 bins** from the carrier - inside its
skirt. Repeating with 170 ms captures (5.88 Hz bins, 14.6 bins out) gives
**-93 dBc**.

What the transmit envelope actually contained before the fix was not a modulation
at all but **discrete ringing transients** at irregular intervals, flat between
them. Impulsive events give broadband spectra, which is why the carrier was smeared
into a pedestal spanning +/-600 Hz at -40 to -60 dBc; probing that pedestal at
+/-86 Hz sampled it at an arbitrary point. Nothing was ever special about 86 Hz.
The 344.53 Hz figure *was* real - that was measured on a 683 ms capture with 1.5 Hz
bins and confirmed by the ALSA-period test.

After the fix the transmit carrier skirt is -93 dBc at +/-86 Hz and the floor is
-95 dBc; the only spurs above -85 dBc are carrier/DC leakage and the 2nd harmonic.

**Lesson worth keeping:** before believing a close-in spur, check how many FFT bins
away it is. Anything within ~5 bins is carrier skirt until proven otherwise.

## Still unverified / open

- **End-to-end FT8** through WSJT-X over hamlib.
- **Long-run clock drift on transmit.** Receive was measured over 10 minutes with
  zero drift events (|drift| < ~5 ppm); transmit has not been run that long.
- **Single- vs dual-VFO.** All bench work so far is on one radio; the abandoned
  branch's transmit failure was hardware-dependent (`DIRECT_COUPLED_TX`).
- `digitalDriveLevel` has a menu entry but no CAT command, so it could not be
  swept during the IMD test.
