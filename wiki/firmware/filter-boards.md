---
title: Filter Boards (BPF / LPF), RF Back-End & Power Sensing
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [filters, bpf, lpf, relays, ad7991, swr, power-sensing, mcp23017, antenna, pa]
source_refs: [sources/t41-ep-schematics]
related: ["[[overview]]", "[[filter-board-electronics]]", "[[rf-board]]", "[[hardware-state-machine]]", "[[tune-frequency-control]]", "[[persistent-config]]", "[[hardware-platform]]", "[[i2c-bus-map]]", "[[t41-ep-schematics]]"]
---

# Filter Boards (BPF / LPF), RF Back-End & Power Sensing

**Files:**
- `BPFBoard.cpp` / `BPFBoard.h` — receive **band-pass filter** bank (front-end selectivity)
- `LPFBoard.cpp` / `LPFBoard.h` — the RF **back-end board**: low-pass filters *plus* BPF path
  routing, transverter, 100 W PA, antenna switching, and SWR/power metering
- `LPFBoard_AD7991.cpp` / `.h` — driver for the AD7991 4-channel I²C ADC (power sensing)

Despite the name, the **LPF board is the main RF back-end** — it does far more than low-pass
filtering. Both boards are clean hardware-abstraction modules (mockable, like [[rf-board]]),
driven by `HandleTuneState()`/`HandleRFHardwareStateChange()` in [[hardware-state-machine]].

## The shared `hardwareRegister` model

All three RF boards write into **one global 64-bit `hardwareRegister`** (`Globals.cpp:112`),
each owning a bitfield (`SDT.h:69-83`):

| Bits | Field | Board |
|---|---|---|
| LPFBAND0..3 | LPF band (BCD) | LPF |
| ANT0..1 | antenna select | LPF |
| XVTR | transverter enable | LPF |
| PA100W | 100 W PA select | LPF |
| TXBPF / RXBPF | BPF in TX / RX path | LPF |
| RXTX | T/R state | — |
| CW / MODE / CAL | keying / mode / cal feedback | RF |
| CWVFO / RXVFO | VFO enables | RF |
| BPFBAND0..3 (bit 28+) | BPF band | BPF |

Setting a field calls `buffer_add()` (`Globals.cpp:246`), which queues the register to be
flushed to the **MCP23017 I²C GPIO expanders** that drive the relays. `UpdateMCPRegisters()`
writes the cached GPIOA/GPIOB bytes out. Each board exposes `Get*MCPRegisters()` test hooks.

## BPF — band-pass (RX selectivity)

`SelectBPFBand(band)` narrows the receive front end to the active band. Bands **0–10 plus
15 = BYPASS**. The band→relay control word uses a **byte-swap encoding** (`BPF_WORD` macro,
`BPFBoard.h:48`): compute `1 << band`, swap the two bytes, with a special case folding the
bypass pattern `0x0080 → 0x0008`. The header carries the full band↔word table
(`BPFBoard.h:29-41`) — worth keeping as the canonical reference.

## LPF — harmonic suppression + back-end routing

- `SelectLPFBand(band)` (bands 0–10) switches the TX harmonic-suppression low-pass via
  `BandToBCD(band)` — the relay driver wants a **4-bit BCD** band code.
- **BPF path routing:** `TXSelectBPF`/`TXBypassBPF`, `RXSelectBPF`/`RXBypassBPF` route the
  TX/RX signal through or around the BPF bank.
- **Transverter:** `SelectXVTR`/`BypassXVTR` route to an external VHF/UHF transverter (also
  used during calibration to send the signal away from the T/R switch, `HardwareSm.cpp:355`).
- **100 W PA:** `Select100WPA`/`Bypass100WPA` (driven by `ED.PA100Wactive`).
- **Antenna:** `SelectAntenna(0–3)` relay matrix; per-band choice in `ED.antennaSelection`
  ([[persistent-config]]).

### Band-code → relay decode (what the firmware writes)
The firmware never addresses relays directly — it writes the encoded `hardwareRegister` bits to
the MCP23017 (U15, 0x25) and dedicated decoder logic on the control board does the rest
(`LPFBoard.cpp:78-84`, [[i2c-bus-map]]). The bit assignments the code drives:

- **LPF band** — **GPIOB[0:3]** = 4-bit BCD (`SDT.h:95-106`): 60 m=0000, 160 m=0001, 80 m=0010,
  40 m=0011, 30 m=0100, 20 m=0101, 17 m=0110, 15 m=0111, 12 m=1000, 10 m=1001, 6 m=1010, NF=1111.
- **Antenna** — **GPIOB[4:5]** (2-bit).
- **100 W PA route** — **GPIOB[7]**.
- **BPF path routing** — **GPIOA[0:1]** (TXBPF/RXBPF).

How those bits are physically decoded (CD74HC4514 / 74HC237 decoders, ULN2803 inverting
drivers, the on-board HF41F relays, and the external per-band 7-element LPF board with its
inductor values) is documented in **[[filter-board-electronics]]**.

## SWR & power sensing — `PerformSWRBridgeReading()` (`LPFBoard.cpp:589`)

Reads a directional coupler to compute **forward power, reflected power, and SWR**. There are
**two `#ifdef`-selected front ends with *different* detector hardware and therefore different
math** — a distinction worth getting right:

**Digital (default, `#else` branch, `LPFBoard.cpp:625-648`).** The coupler feeds **two AD8307
*log* power detectors** (U14 forward, U19 reflected) whose outputs go to the **AD7991** 12-bit
I²C ADC (**CH0 = forward, CH1 = reflected**), referenced by the on-board **REF3440 4.096 V**
on the ADC's Vin3/Vref pin (schematic-confirmed, [[t41-ep-schematics]]). Because an AD8307 is
**logarithmic**, the code converts mV → dBm → W with the **AD8307 transfer law** (≈25 mV/dB
slope, −84 dBm intercept), plus per-band cal and fixed coupler/pad offsets:
```
mV    = counts · VREF_MV / 4096
P_dBm = mV / (25 + SWR_*_SlopeAdj[band]) − 84 + SWR_*_Offset[band] + PAD_ATTEN + COUPLER_ATTEN
P_W   = 10^(P_dBm/10) / 1000
|Γ|   = sqrt(Pr/Pf);   SWR = (1+|Γ|)/(1−|Γ|)
```

**Analog (`#ifdef USE_ANALOG_SWR`, `LPFBoard.cpp:591-621`).** A *different* board option: **linear
diode detectors** on Teensy ADC pins 26 (FWD) / 27 (REV). Being linear, this path uses the
square-law `P = (V·10)² / 50` (20 dB coupler ⇒ ×10 V; P = V²/50 Ω). This is **not** used with
the AD8307 hardware and is disabled by default (`USE_ANALOG_SWR` commented out in `Config.h`).

Guard rails (both): no forward power ⇒ SWR = 1; `|Γ|` clamped < 0.999. Forward/reflected use
**different smoothing constants**. Results cached and read via
`ReadSWR()`/`ReadForwardPower()`/`ReadReflectedPower()`; `ReadSWRLastUpdateMs()` lets the
display detect TX activity.

## How it ties together
- **Band changes:** `HandleTuneState()` calls `SelectLPFBand` + `SelectBPFBand` +
  `SelectAntenna` + the PA select **before** reprogramming the VFO ([[rf-board]]) — so filters
  settle before RF appears. See [[hardware-state-machine]].
- **Power metering** feeds the front-panel power/SWR display and the **power calibration**
  routine (`PowerCalSm`), which fits the tanh PA model in [[tune-frequency-control]].

## Physical hardware
The three boards this module drives — the BPF/exciter-filter board (MASWSS0179 switches,
MCP23017 U35 0x24), the LPF/100 W PA control board (decoders, ULN2803s, HF41F relays, PA/XVTR
control), the external per-band LPF filter board, and the AD8307/AD7991/REF3440 SWR-sensing
front end — are documented in **[[filter-board-electronics]]** (schematic-traced,
[[t41-ep-schematics]]). See also the platform-level [[hardware-platform]].

## Open questions
- ~~Full LPF band↔BCD table and the band-code → CD74HC4514/74HC237 → relay mapping.~~
  **Resolved (2026-06-15, owner net trace):** BCD table (above) + full decode chain in
  [[filter-board-electronics]] (GPIOB[0:3]→U12 4514→U9/U10 ULN2803→external relays via J24/J25;
  antenna GPIOB[4:5]→U2 74HC237→U4→K4-K7; PA GPIOB[7]→U4→K2/K3; BPF routing GPIOA[0:1]→U5/U6).
  The external filter board is ingested too: **one dedicated 7-element LPF per band** (2 relays
  each, band-named control nets, Yn = band BCD n). *Remaining (minor):* tabulate the exact
  per-band L/C → cutoff frequency.
- ~~⚠ AD8307 (log) vs linear power math.~~ — **RESOLVED, not a bug (2026-06-14, net trace +
  schematic owner confirmation).** The **default digital path correctly applies the AD8307 log
  law** (mV/25 − 84 → dBm → W, `LPFBoard.cpp:640-643`). The linear `(V·10)²/50` is the *other*
  branch (`USE_ANALOG_SWR`), which targets linear **diode** detectors on Teensy pins 26/27 and
  is off by default. The earlier flag came from a **wiki error** that attributed the analog-path
  formula to the digital path; now corrected. AD8307 OUT (U14 fwd / U19 rev) → AD7991 CH0/CH1;
  REF3440 → Vin3/Vref reference (see [[filter-board-electronics]]). → [[t41-ep-schematics]]
- The AD7991 `REGISTER_SETUP` channel-select bits vs. the "CH0+CH1" comment (the setup byte
  has the channel nibble = 0; confirm how per-channel reads are configured).
