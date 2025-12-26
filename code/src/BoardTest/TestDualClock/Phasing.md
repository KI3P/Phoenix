# Si5351 Quadrature (90-Degree) Phase Generation

This document explains how the `SetSSBRXVFOFrequency` and `SetSSBTXVFOFrequency` functions in `TestDualClock.ino` achieve 90-degree phase difference between clock outputs for I/Q (quadrature) signal generation.

## Overview

Software Defined Radios (SDRs) require quadrature local oscillator signals - two clocks at the same frequency but 90 degrees out of phase - to perform image rejection in the receiver and to generate single-sideband signals in the transmitter. The Si5351 clock generator can produce these quadrature outputs using its phase offset registers.

## Si5351 Architecture

The Si5351 has the following signal path:

```
Crystal (25 MHz) -> PLL (VCO) -> MultiSynth Divider -> Output Clock
                   400-900 MHz      /4 to /1800
```

Key components:

- **PLL (Phase-Locked Loop)**: Multiplies the crystal frequency to produce a VCO frequency between 400-900 MHz
- **MultiSynth Divider**: Divides the VCO frequency down to the desired output frequency
- **Phase Offset Register**: Delays the output by a programmable number of VCO quarter-cycles

### Phase Offset Registers

The Si5351 provides phase offset registers for CLK0-CLK5:

| Register | Address | Description |
|----------|---------|-------------|
| CLK0_PHOFF | 165 | CLK0 phase offset |
| CLK1_PHOFF | 166 | CLK1 phase offset |
| CLK2_PHOFF | 167 | CLK2 phase offset |
| CLK3_PHOFF | 168 | CLK3 phase offset |
| CLK4_PHOFF | 169 | CLK4 phase offset |
| CLK5_PHOFF | 170 | CLK5 phase offset |

Each register is 7 bits, allowing values from 0 to 127. The phase offset delays the clock output by N quarter-cycles of the VCO frequency.

**Important**: CLK6 and CLK7 do not have phase offset registers. For this reason, the Phoenix SDR uses CLK4 and CLK5 for SSB TX quadrature outputs, as these clocks have phase registers available.

## Method 1: Phase Register Method (Frequencies >= 3.2 MHz)

For frequencies at or above 3.2 MHz, the MultiSynth divisor is 126 or less, which fits within the 7-bit phase register limit.

### Mathematical Basis

The relationship between phase offset and output phase shift:

1. **VCO Frequency**: `f_vco = f_out × divisor`
2. **VCO Period**: `T_vco = 1 / f_vco`
3. **Output Period**: `T_out = divisor × T_vco`
4. **Phase Register Unit**: Each count delays by `T_vco / 4` (one quarter VCO cycle)

To achieve 90 degrees (1/4 cycle) at the output frequency:

```
Required delay = T_out / 4
               = (divisor × T_vco) / 4
               = divisor × (T_vco / 4)
```

Since each phase register count = `T_vco / 4`:

```
Phase register value = divisor
```

### Implementation

From `TestDualClock.ino`, lines 179-188:

```c
if (rxmultiple <= 126) {
    si5351.set_freq_manual(freq, pll_freq, SI5351_CLK0);  // Set CLK0 frequency
    si5351.set_freq_manual(freq, pll_freq, SI5351_CLK1);  // Set CLK1 frequency
    si5351.set_phase(SI5351_CLK0, 0);                     // CLK0 phase = 0
    si5351.set_phase(SI5351_CLK1, rxmultiple);            // CLK1 phase = divisor
    si5351.pll_reset(SI5351_PLLA);                        // Reset PLL to align
    si5351.output_enable(SI5351_CLK0, 1);
    si5351.output_enable(SI5351_CLK1, 1);
}
```

### Step-by-Step Process

1. **Configure both clocks** to the same frequency using `set_freq_manual()`
2. **Set CLK0 phase to 0** - this is the reference (I channel)
3. **Set CLK1 phase to divisor value** - this delays CLK1 by 90 degrees (Q channel)
4. **Reset the PLL** - this synchronizes both MultiSynth dividers to start counting from zero simultaneously, establishing the programmed phase relationship
5. **Enable outputs**

### Example Calculation

For 14 MHz output:
- Divisor from `EvenDivisor()`: 64 (for 13.6-17.5 MHz range)
- PLL frequency: 14 MHz × 64 = 896 MHz
- Phase register for CLK1: 64
- Delay: 64 × (1/896 MHz / 4) = 64 × 0.279 ns = 17.86 ns
- Output period: 1/14 MHz = 71.43 ns
- Phase shift: 17.86 ns / 71.43 ns = 0.25 = 90 degrees

### Why PLL Reset is Critical

The PLL reset (register 177) simultaneously resets the divider counters for all clocks sharing that PLL. Without the reset, the phase relationship would be undefined because the dividers could be at arbitrary points in their count sequence.

## Method 2: Timed Delay Technique (Frequencies < 3.2 MHz)

For frequencies below 3.2 MHz, the required divisor exceeds 126 (up to 8192 for the lowest frequencies). Since this exceeds the 7-bit phase register maximum, an alternative technique is used.

This method was developed by TJ-Lab and documented at:
https://tj-lab.org/2020/08/27/si5351%E5%8D%98%E4%BD%93%E3%81%A73mhz%E4%BB%A5%E4%B8%8B%E3%81%AE%E7%9B%B4%E4%BA%A4%E4%BF%A1%E5%8F%B7%E3%82%92%E5%87%BA%E5%8A%9B%E3%81%99%E3%82%8B/

### Concept

Instead of using hardware phase registers, this technique exploits the beat frequency between two slightly different frequencies to create a controlled phase offset.

If two signals differ by `delta_f` Hz, their phase difference changes by 360 degrees every `1/delta_f` seconds. To achieve 90 degrees of phase difference:

```
Wait time = (90/360) × (1/delta_f) = 1/(4 × delta_f)
```

For `delta_f = 4 Hz`:
```
Wait time = 1/(4 × 4) = 0.0625 seconds = 62.5 ms
```

### Implementation

From `TestDualClock.ino`, lines 189-207:

```c
else {
    cli();  // Disable interrupts for accurate timing

    // Set both clocks 4 Hz below target, synchronized
    si5351.set_freq_manual((freq - 400ULL), pll_freq, SI5351_CLK0);
    si5351.set_freq_manual((freq - 400ULL), pll_freq, SI5351_CLK1);
    si5351.set_phase(SI5351_CLK0, 0);
    si5351.set_phase(SI5351_CLK1, 0);
    si5351.pll_reset(SI5351_PLLA);  // Align both clocks in phase

    // Set CLK0 to target frequency
    si5351.set_freq_manual(freq, pll_freq, SI5351_CLK0);

    // Wait for 90 degrees of phase accumulation
    delayMicroseconds(58500);  // ~62.5 ms (tuned for accuracy)

    // Set CLK1 to target frequency
    si5351.set_freq_manual(freq, pll_freq, SI5351_CLK1);

    sei();  // Re-enable interrupts
    si5351.output_enable(SI5351_CLK0, 1);
    si5351.output_enable(SI5351_CLK1, 1);
}
```

### Step-by-Step Process

1. **Disable interrupts** (`cli()`) to ensure accurate timing
2. **Set both clocks to (target - 4 Hz)** - both outputs are now synchronized at a slightly lower frequency
3. **Clear phase registers** and **reset PLL** - both clocks are now in phase
4. **Switch CLK0 to target frequency** - CLK0 now runs 4 Hz faster than CLK1
5. **Wait 62.5 ms** - during this time, CLK0 accumulates a 90-degree phase lead
6. **Switch CLK1 to target frequency** - CLK1 now runs at the same frequency as CLK0, but 90 degrees behind
7. **Re-enable interrupts** (`sei()`)

### Timing Calculation

The delay value of 58500 microseconds (58.5 ms) is slightly less than the theoretical 62500 microseconds (62.5 ms). This accounts for:
- I2C communication overhead in the `set_freq_manual()` calls
- Code execution time
- Fine-tuning for optimal quadrature accuracy

This value can be adjusted empirically by measuring the actual phase difference with an oscilloscope.

### Why 4 Hz Offset?

The 4 Hz value (400 in deci-Hertz units) is chosen because:
- It's small enough to be negligible compared to the target frequency
- It results in a convenient 62.5 ms delay for 90 degrees
- It's large enough that timing jitter has minimal impact on phase accuracy

## Frequency-Dependent Divisor Selection

The `EvenDivisor()` function selects appropriate divisors to keep the PLL within its valid range (400-900 MHz):

| Frequency Range | Divisor | PLL Range |
|-----------------|---------|-----------|
| < 100 kHz | 8192 | 409-819 MHz |
| 100-200 kHz | 4096 | 409-819 MHz |
| 200-400 kHz | 2048 | 409-819 MHz |
| 400-800 kHz | 1024 | 409-819 MHz |
| 800 kHz - 1.6 MHz | 512 | 409-819 MHz |
| 1.6-3.2 MHz | 256 | 409-819 MHz |
| 3.2-6.85 MHz | 126 | 403-863 MHz |
| 6.85-9.5 MHz | 88 | 603-836 MHz |
| 9.5-13.6 MHz | 64 | 608-870 MHz |
| 13.6-17.5 MHz | 44 | 598-770 MHz |
| 17.5-25 MHz | 34 | 595-850 MHz |
| 25-36 MHz | 24 | 600-864 MHz |
| 36-45 MHz | 18 | 648-810 MHz |
| 45-60 MHz | 14 | 630-840 MHz |
| 60-80 MHz | 10 | 600-800 MHz |
| 80-100 MHz | 8 | 640-800 MHz |
| 100-150 MHz | 6 | 600-900 MHz |
| 150-220 MHz | 4 | 600-880 MHz |
| >= 220 MHz | 2 | 440+ MHz |

The divisor of 126 is the boundary between the two quadrature methods because it's the maximum value that fits in the 7-bit phase register.

## PLL Assignment

The Phoenix SDR assigns PLLs as follows to allow independent RX and TX frequencies:

| Clock Outputs | PLL | Purpose |
|--------------|-----|---------|
| CLK0, CLK1 | PLLA | SSB RX quadrature |
| CLK4, CLK5 | PLLB | SSB TX quadrature |
| CLK6 | PLLB | CW TX (single output) |

This configuration allows:
- RX and TX to operate at different frequencies simultaneously
- Full-duplex operation capability
- Independent frequency adjustment without affecting the other VFO
- Phase offset registers available for both RX (CLK0/CLK1) and TX (CLK4/CLK5) quadrature pairs

## Optimization: Minimal I2C Traffic

When tuning within the same divisor range, the code optimizes I2C communication:

```c
if (rxmultiple == oldrxMultiple) {
    si5351.set_pll(pll_freq, SI5351_PLLA);  // Only update PLL frequency
} else {
    // Full reconfiguration required
    ...
}
```

This reduces I2C traffic during normal tuning operations, as only the PLL frequency needs to change when the divisor remains constant. The phase relationship is maintained because the divisor (and thus the phase register value) hasn't changed.

## Summary

| Parameter | Phase Register Method | Timed Delay Method |
|-----------|----------------------|-------------------|
| Frequency Range | >= 3.2 MHz | < 3.2 MHz |
| Divisor Range | <= 126 | > 126 |
| Phase Control | Hardware register | Software timing |
| Accuracy | Very high | Depends on timing precision |
| Complexity | Simple | Requires interrupt disable |
| Adjustment | Fixed by divisor | Tunable delay value |

Both methods achieve the same goal: producing two clock outputs at identical frequencies with a 90-degree phase relationship, essential for quadrature signal processing in SDR applications.
