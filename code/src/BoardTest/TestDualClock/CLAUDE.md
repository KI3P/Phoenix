# CLAUDE.md - TestDualClock Board Test

## Overview

This is a **board test sketch** for verifying the dual-clock Si5351 VFO configuration used in the Phoenix SDR radio. It provides an interactive serial interface to manually set and test the three VFO outputs required for SSB receive, SSB transmit, and CW transmit operations.

## Purpose

Tests the Si5351 clock generator's ability to produce:
- **SSB RX VFO**: Quadrature outputs on CLK0/CLK1 (PLLA)
- **SSB TX VFO**: Quadrature outputs on CLK4/CLK5 (PLLB)
- **CW TX VFO**: Single output on CLK6 (PLLB)

This validates the hardware configuration needed for full-duplex-capable VFO operation where RX and TX frequencies can be set independently.

## Hardware Requirements

- **Teensy 4.1** microcontroller
- **Si5351** clock generator at I2C address 0x60
- 25 MHz crystal reference

## Files

| File | Description |
|------|-------------|
| `TestDualClock.ino` | Main Arduino sketch with serial interface and VFO control functions |
| `RFBoard_si5351.cpp` | Si5351 library implementation (GPL licensed, based on Jason Milldrum's library) |
| `RFBoard_si5351.h` | Si5351 library header with register definitions and class declaration |
| `SDT.h` | System definitions, error codes, and hardware register bit mappings |

## Usage

1. Upload to Teensy 4.1 using Arduino IDE
2. Open Serial Monitor at 115200 baud
3. Use menu commands:
   - `TX` - Set SSB TX frequency (CLK4/CLK5)
   - `RX` - Set SSB RX frequency (CLK0/CLK1)
   - `CW` - Set CW TX frequency (CLK6)
4. Enter frequency in kHz when prompted

## Key Functions

### `SetSSBRXVFOFrequency(int64_t frequency_dHz)`
Sets CLK0/CLK1 as quadrature outputs using PLLA. Uses phase offset method for frequencies above 3.2 MHz, and timed delay technique for lower frequencies.

### `SetSSBTXVFOFrequency(int64_t frequency_dHz)`
Sets CLK4/CLK5 as quadrature outputs using PLLB. Same quadrature generation techniques as RX.

### `SetCWTXVFOFrequency(int64_t frequency_dHz)`
Sets CLK6 for CW transmission using standard Si5351 frequency setting.

### `EvenDivisor(int64_t freq2_Hz)`
Calculates the optimal even divisor for PLL configuration across the full frequency range (100 kHz to 220+ MHz).

## Frequency Units

Frequencies are handled in **deci-Hertz** (Hz × 100) internally for precision:
- `SI5351_FREQ_MULT = 100`
- Input from serial is in kHz, converted to deci-Hz: `(f_kHz * 1000L) * 100L`

## PLL Assignment

| Clock Output | PLL | Purpose |
|-------------|-----|---------|
| CLK0, CLK1 | PLLA | SSB RX quadrature |
| CLK4, CLK5 | PLLB | SSB TX quadrature |
| CLK6 | PLLB | CW TX |

## Quadrature Generation

For frequencies >= 3.2 MHz:
- Uses Si5351 phase registers to set 90° offset
- Phase = divisor value for CLK1/CLK5

For frequencies < 3.2 MHz:
- Uses timed delay technique from TJ-Labs
- Sets both clocks 4 Hz low, resets PLL, then staggers frequency changes by ~62.5 ms

## Build

Use Arduino IDE with Teensyduino installed. Select Teensy 4.1 as the board.

## Relationship to Main Project

This test code validates the VFO architecture before integration into the main Phoenix SDR firmware. The production `RFBoard.cpp` uses similar patterns but with additional integration for state machine control.
