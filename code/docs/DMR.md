# DMR on Phoenix

This document describes how Digital Mobile Radio (DMR) voice is implemented as an overlay on top of Phoenix's existing NFM / FM_WIDE modulation. It is the entry point for anyone maintaining or extending the DMR-related modules.

## Layering

DMR is **not** a peer of NFM or FM_WIDE in `ModulationType`. It is a *digital coding* (`DigitalCoding::DigitalCodingDMR` in `SDT.h`) applied on top of one of those FM modulations. The over-the-air waveform is 4FSK at 4800 sym/s (≈ 9.6 kbps) carried inside an FM channel: 12.5 kHz for NFM, 25 kHz for FM_WIDE. The full stack from antenna to operator is:

```
                 TX                                 RX
                 ──                                 ──
  AmbeDvsi       AmbeEncodePcm20ms                  AmbePopPcm20ms
  (USB ThumbDV)    │                                  ▲
                   ▼                                  │
  DmrFraming     DmrTxBuildVoiceFrame /             DmrRxParseLcHeaderBurst /
                 DmrTxBuildLcHeaderBurst              DmrRxParseFrame /
                                                     DmrRxParseEmbeddedLc
                   │                                  ▲
                   ▼                                  │
  DmrModem       DmrModemTxQueueBurst →              DmrModemRxFeed /
                 DmrModemRender                       DmrModemRxPopBurst
                   │                                  ▲
                   ▼                                  │
  DSP integration FmModulate +                       nfmdemod +
                  TS gating                           DmrAudioPumpAndFill
                   │                                  ▲
                   ▼                                  │
  Phoenix codec   PlayIQData → SGTL5000 → mixer ───  ADC ← antenna
```

Per-VFO selection lives in `ED.dmrEnabled` plus the existing `ED.modulation[]`. DMR is active when `dmrConfig.enabled == true` AND `modulation == NFM || modulation == FM_WIDE`.

## Module map

| File | Role |
|---|---|
| `AmbeDvsi.{h,cpp}` | USB host driver for the DVSI ThumbDV (AMBE3000R) on the Teensy 4.1 USBHOST port. PCM ↔ 49-bit AMBE+2 channel data over CDC-ACM @ 460 800. |
| `DmrFraming.{h,cpp}` | All FEC primitives + LC header / voice burst / embedded-LC pack and parse. |
| `DmrModem.{h,cpp}` | 4FSK TX (RRC pulse-shaped, polyphase, TS-gated) and RX (matched RRC, cross-correlation SYNC, slicer, continuous superframe tracking). Audio rate flexible (24/48/96/192 kHz); recommended 24 kHz post-DecimateBy8. |
| `DmrAudioOut.{h,cpp}` | 8 kHz AMBE PCM → DSP-rate float audio (RX path), polyphase windowed-sinc upsampler with a 4 800-sample ring. |
| `DmrTxAudio.{h,cpp}` | Mic audio (DSP rate) → 8 kHz int16 → AmbeEncodePcm20ms → 3-frame voice burst → modem queue (TX path). Auto-detects new calls via a 50 ms feed gap and queues an LC header at the start. |
| `DSP.cpp` | Wires modem and codecs into Phoenix's existing RX (`Demodulate`) and TX (`TransmitProcessing`) chains under `DigitalCodingDMR`. `FmModulate` is the new phase-integrating modulator used by both analog FM voice and DMR. |
| `MainBoard_DisplayMenus.cpp` | Operator menu page: Enable / TG / My DMR ID / Time Slot / Color Code / Save Now / Reset Defaults. Persists to LittleFS via `Storage`. |
| `MainBoard_DisplayHome.cpp` | Home-screen status pane: live call indicator (`Group TG:9 From:1234567 3s`), lock/search state, signal strength, BER (FEC corrections per burst). |

## FEC stack

DMR uses an unusually rich set of error-correcting codes. Phoenix implements:

| Code | Used for | Distance | Generator polynomial |
|---|---|---|---|
| **Hamming(15,11)** | Row code in BPTC(196,96); per-AMBE-frame protection of bits 24..34 | 3 | `g(x) = x⁴ + x + 1 = 0x13` |
| **Hamming(13,9)** | Column code in BPTC(196,96) | 3 | same `0x13` |
| **Hamming(11,7)** | Column code in BPTC(128,77) | 3 | same `0x13` |
| **Golay(23,12)** | Per-AMBE-frame protection of bits 0..23 (in two halves) | 7 | `g(x) = 0xAE3` |
| **Golay(20,8)** | Slot Type (Color Code + Data Type) on Voice LC Header bursts | 6 | shortened from `(24,12)` Golay with `g(x) = 0xC75` |
| **BPTC(196,96)** | Voice LC Header info area | n/a (block product) | row Hamming(15,11) + column Hamming(13,9) |
| **BPTC(128,77)** | Embedded LC across voice frames A..D | n/a | column Hamming(11,7) only |
| **QR(16,7)** | EMB header (Color Code + PI + LCSS) on voice frames A..E | 6 | `g(x) = x⁹ + x⁷ + x⁵ + x² + x + 1 = 0x2A7` |
| **RS(12,9) over GF(2⁸)** | LC trailer on Voice LC Header (replaces a CRC) | 4 | `g(x) = (x+α)(x+α²)(x+α³) = x³ + 14·x² + 56·x + 64`, primitive poly `0x11D`, α=2 |
| **CRC-5** | LC integrity in embedded LC | n/a | `g(x) = x⁵ + x² + 1 = 0x25` |

Two CRC masks are pre-applied to the RS check bytes:

| Burst type | Mask |
|---|---|
| Voice LC Header (data type `0x1`) | `0x96 0x96 0x96` |
| Terminator with LC (data type `0x2`) | `0x99 0x99 0x99` |

## Burst layouts

All bursts are 264 bits (33 bytes), 132 4FSK symbols, ~27.5 ms airtime per slot.

### Voice LC Header (data burst format)

```
bits   0..97   info first half  (BPTC(196,96) bits   0..97)
bits  98..107  slot type half 1 (Golay(20,8) codeword bits 19..10)
bits 108..155  SYNC             (BS-sourced voice 0x755FD7DF75F7)
bits 156..165  slot type half 2 (Golay(20,8) codeword bits  9..0)
bits 166..263  info second half (BPTC(196,96) bits 98..195)
```

LC payload (96 bits = 9 LC + 3 RS check):

```
bits 0..1     PF (1) + Reserved (1)         [zero in this firmware]
bits 2..7     FLCO (6)                      [0=Group, 3=Private]
bits 8..15    FID (8)                       [0 = standard]
bits 16..23   Service Options (8)           [0]
bits 24..47   Destination Address (24)      [TG number or unit ID]
bits 48..71   Source Address (24)           [our DMR Radio ID]
bytes 9..11   RS(12,9) parity ⊕ 0x96 mask   [trailer]
```

### Voice frames A..F (voice burst format)

```
bits   0..107  info first half  (108 bits, voice payload)
bits 108..155  middle           (48 bits)
bits 156..263  info second half (108 bits, voice payload)
```

The 216-bit info area carries 3 AMBE+2 frames × 72 bits. The middle field is:

| Frame | Middle 48 bits |
|---|---|
| F | full SYNC `0x755FD7DF75F7` |
| A..E | 8 EMB1 + 32 embedded data + 8 EMB2 |

EMB header (16 bits = EMB1 ‖ EMB2): QR(16,7)-encoded `(CC<<3) | (PI<<2) | LCSS`. LCSS sequence across A→D: `01 → 11 → 11 → 10`. Frame E LCSS is `00` (single fragment / reverse channel — currently zero-data).

The 4×32 = 128 bits of embedded data across A..D are the BPTC(128,77) codeword for the 72-bit LC + 5-bit CRC. So a receiver that tunes in mid-call can recover src/dst within ≤ 360 ms.

### Per-AMBE-frame FEC

Each 49-bit AMBE+2 frame is expanded to 72 bits as:

```
u_0 = AMBE bits  0..11  (12)  → Golay(23,12)   → 23 bits
u_1 = AMBE bits 12..23  (12)  → Golay(23,12)   → 23 bits
u_2 = AMBE bits 24..34  (11)  → Hamming(15,11) → 15 bits
u_3 = AMBE bits 35..45  (11)  → unprotected    → 11 bits
                                                  -----
                                                  72 bits
```

AMBE bits 46..48 are dropped (per ETSI's choice of perceptually-least-significant bits). The three frames are packed into the 216-bit info area with frame 1 straddling the SYNC.

## Runtime config

`dmrConfig` (declared in `SDT.h`, defined in `Globals.cpp`) is the live source of truth. The framing layer holds a private copy via `DmrConfigure(&dmrConfig)`. Persistent state lives on five `ED` fields:

| Field | Type | Default | Range |
|---|---|---|---|
| `ED.dmrEnabled` | `bool` | `false` | on/off |
| `ED.dmrTalkGroup` | `int32_t` | `9` | 0..16 777 215 |
| `ED.dmrSrcId` | `int32_t` | `0` | 0..16 777 215 |
| `ED.dmrTimeSlot` | `int32_t` | `1` | 1..2 |
| `ED.dmrColorCode` | `int8_t` | `1` | 0..15 |

`SyncDmrConfigFromED()` mirrors them into `dmrConfig` and re-`DmrConfigure`s the framing layer. Called from `setup()` after `Storage` loads `ED`, and from each menu post-update callback.

## TX flow

1. Operator dials Menu → DMR → Enable: on.
2. Switches active VFO modulation to NFM or FM_WIDE.
3. Presses PTT. `ModeSm` enters TRANSMIT.
4. Each ~10.7 ms (256 samples at 24 kHz):
   - `TransmitProcessing` reads mic audio, decimates to 24 kHz / 256 samples.
   - `DmrTxAudioFeed(data.I, 256)` decimates to 8 kHz, feeds the AMBE codec, accumulates 3 channel frames per voice burst, hands assembled bursts (with auto-LC-header at the start of each call) to the modem queue.
   - `DmrModemRender(data.I, 256)` writes 4FSK baseband samples (zeros between bursts).
   - `FmModulate(&data, 648.0f)` integrates phase to produce 24 kHz I/Q at ±1.944 kHz peak deviation per ETSI.
   - **Per-sample µs TS gating** zeros the I/Q outside our slot — sample-precise (~42 µs at 24 kHz).
   - Standard `TXInterpolateBy2`/`TXInterpolateBy4` chain produces 192 kHz I/Q.

## RX flow

1. Standard front end: ADC → I/Q decimation → `Demodulate` dispatch.
2. For NFM and FM_WIDE: `nfmdemod(data)` produces audio in `data.I[]` at 24 kHz.
3. `DmrRxBridgeFmAudio(data)` (only when `dmrConfig.enabled`):
   - `DmrModemRxFeed(data->I, 256)` runs matched RRC, peak-settled SYNC acquisition, continuous superframe tracking.
   - For each popped burst, try `DmrRxParseLcHeaderBurst` (new call) → `DmrRxParseEmbeddedLc` (mid-call addressing recovery) → `DmrRxParseFrame` (voice payload).
   - Decoded AMBE frames go to `AmbeDecodeFrame()`; PCM eventually returns via `AmbePopPcm20ms`.
4. `DmrAudioPumpAndFill(data->I, 256)` polyphase-upsamples 8 kHz PCM → 24 kHz audio, fills `data->I[]`.
5. Existing audio chain (BandEQ → NR → InterpolateReceiveData → AdjustVolume → PlayBuffer) treats it as ordinary RX audio.

## Tier-II frame sync

`DmrModem` captures `micros()` at SYNC acquisition and at every confirmed re-validation (frame F bursts). `DmrSlotIsOursUs(now)` consults `DmrModemGetSyncRef()`; when valid, it computes slot phase as `(now - syncRefUs + 60000 - SYNC_OFFSET_INTO_SLOT_US) % 60000`. This anchors our TX slot to the network's frame phase rather than free-running off the local `micros()` clock. Sync ref invalidates when the modem drops back to SEARCHING.

`SYNC_OFFSET_INTO_SLOT_US` is set to 16 000 µs — the SYNC pattern's center within its 30 ms slot. Real-world tuning may want a slight calibration offset for round-trip group delay through the FM mod/demod chain.

## ETSI / Phoenix divergences

| Item | Status |
|---|---|
| BS-sourced voice SYNC `0x755FD7DF75F7` | matches ETSI |
| BPTC(196,96) info layout | matches ETSI |
| BPTC(128,77) info layout (7-row × 11-col, 7 R bits prepended) | matches ETSI |
| RS(12,9) over GF(2⁸), α=2, primitive `0x11D` | matches ETSI |
| RS Voice-LC-Header CRC mask `0x96 0x96 0x96` | matches ETSI |
| Golay(20,8) generator (shortened from `(24,12)` with `g(x) = 0xC75`) | matches ETSI's algebraic structure; exact bit ordering of the Slot Type split around SYNC may need OTA verification |
| QR(16,7) generator `0x2A7` (distance 6) | matches ETSI's algebraic structure (one of multiple distance-6 generators); swap to `0x317`, `0x279`, etc. if real-world testing shows mismatch |
| 5-bit CRC `g(x) = x⁵ + x² + 1 = 0x25` | matches ETSI's reference for the embedded LC |
| Voice frame E reverse-channel content | currently zero-filled; ETSI carries embedded-CRC + reverse-channel here |
| Per-VFO DMR settings | not yet — single global `dmrConfig` |
| RX TG whitelist editor | not yet — only the master TG via the menu; the underlying `rxTgFilter[16]` array exists in `DmrConfig` but has no UI |
| Private call dialing UI | not yet — `callType` is hardcoded to `DmrCallGroup` from the menu |

## Deferred work

- **Voice frame E embedded CRC + reverse channel** (5-bit embedded CRC per ETSI TS 102 361-2 §6.4.2)
- **Multi-superframe TX SYNC tracking** to keep TX phase locked to received SYNC during long transmissions (currently only RX side anchors)
- **Per-VFO DMR config** in `ED`
- **RX TG whitelist editor** in the menu UI
- **Private call dialing** (separate `dst` field for unit IDs vs Group TGs)
- **Hardware PA on/off** at slot boundaries — current implementation zeros I/Q in software, which gives "no RF output" but the PA still sits at idle bias

## Testing

Off-Teensy tests in `/tmp/dmrtest/` (recreated each session) verify:

- TX RRC peak amplitude and DC gain (matched filter pair)
- 4FSK symbol round-trip through the modem (multiple sample rates and noise levels)
- BPTC(196,96) round-trip + 1..8-bit error correction profile
- BPTC(128,77) round-trip + 1-bit corrects every cell
- LC pack/unpack round-trip across all FLCO/dst/src combinations including 24-bit edges
- RS(12,9) single-byte error correction (180/180 trials)
- Voice burst FEC round-trip (50/50 trials, 6900/6900 bits)
- Closed-loop DmrModem TX → FmModulate → I/Q → nfmdemod → DmrModem RX with 0 byte errors
- Multi-burst superframe extraction (continuous tracking after lock)
- Embedded LC TX→RX round-trip with mid-call tune-in recovery
- QR(16,7) corrects all 2 048 single-bit and 15 360 double-bit errors
- Per-sample µs TS gating splits a block straddling a slot boundary at sample 120/256 ± 1

The on-Teensy build target is the standard Phoenix Arduino IDE flow with `DMR_THUMBDV_ENABLED` set in `Config.h` to pull in the AMBE codec driver.
