---
title: I²C Bus Map & Addresses
type: hardware
status: stable
created: 2026-06-14
updated: 2026-06-15
tags: [hardware, i2c, wire, mcp23017, si5351, ad7991, addresses, bus-topology]
source_refs: [sources/t41-ep-schematics]
related: ["[[hardware-platform]]", "[[rf-board]]", "[[filter-boards]]", "[[front-panel]]", "[[built-in-test]]", "[[hardware-state-machine]]"]
---

# I²C Bus Map & Addresses

The Teensy 4.1 exposes **three independent I²C buses** (`Wire`, `Wire1`, `Wire2`). Phoenix
deliberately spreads its peripherals across all three to isolate timing-critical traffic and
to keep the front-panel bus fast. Addresses are defined in `Config.h:62-72`; bus assignments
are made where each driver calls `begin_I2C(...)`. The three buses are confirmed on the main
board schematic as the net pairs **`SDA/SCL` (Wire), `SDA1/SCL1` (Wire1), `SDA2/SCL2`
(Wire2)** leaving the Teensy ([[t41-ep-schematics]]).

## Bus topology

| Bus | Devices | Addr | Purpose | Driver (`begin_I2C`) |
|---|---|---|---|---|
| **Wire** (Wire0, "main") | Si5351 (VFO) | `0x60` | Quadrature LO clock generator | `RFBoard_si5351.cpp:80` (`Wire.begin()`) |
| | Si5351 #2 (dual-VFO HW) | `0x61` | 2nd clock gen on split-VFO boards | `RFBoard_si5351.cpp` |
| | RF-board MCP23017 | `0x27` | RX/TX attenuators + switching | `RFBoard.cpp:88` (default `Wire`) |
| | SGTL5000 (Audio Board) | LOW | The radio's *only* SGTL5000: mic-in + TX-I/Q codec control | `MainBoard_AudioIO.cpp:450` |
| **Wire1** | Front-panel MCP23017 #1 | `0x20` | Encoders / buttons / LEDs | `FrontPanel.cpp:285` |
| | Front-panel MCP23017 #2 | `0x21` | Encoders / buttons / LEDs | `FrontPanel.cpp:290` |
| **Wire2** | BPF-board MCP23017 | `0x24` | Band-pass filter relay bank | `BPFBoard.cpp:29` |
| | LPF-board MCP23017 | `0x25` | Low-pass filter + back-end relays | `LPFBoard.cpp:238` |
| | AD7991 4-ch ADC | `0x28` or `0x29` | SWR / forward & reverse power | `LPFBoard.cpp:747,754` |

## Why three buses

- **Wire1 is front-panel-only, run at 400 kHz.** Because the two panel MCP23017s are the
  *only* devices on `Wire1`, the driver raises the clock to **400 kHz** (`FrontPanel.cpp:300`)
  with no contention. This is central to the `encoder-i2c-speed` work — fast, dedicated bus +
  a two-tier ISR/1 ms-timer servicing model keeps encoder reads snappy. See [[front-panel]].
- **Wire2 carries the RF back-end** (BPF/LPF/AD7991) — band-switching relay writes and
  SWR/power reads that happen on tune/T-R changes, kept off the audio-codec/VFO bus.
- **Wire (Wire0) is the "main" bus** shared by the VFO clock gen, the RF-board attenuators, and
  both audio codecs.

## Address quirks
- **AD7991 dual address.** The part ships with one of two fixed addresses depending on the
  ordering code, so the driver **probes `0x28` then `0x29`** (`LPFBoard.cpp:747-754`,
  `Config.h:70-72`). Not user-configurable.
- **Only one SGTL5000.** There is a single SGTL5000 (Audio Board, addr LOW). The firmware also
  declares a `pcm5102_mainBoard` codec object at addr **HIGH** (`MainBoard_AudioIO.cpp:163`),
  but **no SGTL5000 is populated there** — the main-board RX/speaker path is PCM1808/PCM5102,
  which are control-less I²S parts (not on any I²C bus). Those `setAddress(HIGH)/.enable()` calls
  are no-ops. See [[hardware-platform]] (audio) / [[t41-ep-schematics]].
- **Dual-VFO Si5351.** `SI5351_DUAL_VFO_ADDR 0x61` is only populated on split-VFO hardware
  (`HasDualVFOs()`), enabling independent RX/TX clock generation — see [[rf-board]].

## Startup presence check
The **built-in test** ([[built-in-test]]) probes the expected I²C devices at boot and flags
any that fail to ACK, surfacing a missing/misaddressed board on the status screen. This bus
map is the reference for what *should* be present on each bus.
