#pragma once

#include <Arduino.h>

// Simple USB→SDR sample-rate converter for FT8 transmit.
// - Captures mono audio from WSJT via USB (AudioInputUSB)
// - Stores it in a FIFO at ~44.1 kHz
// - Resamples to the Phoenix SDR sample rate (sampleRate, e.g., 192 kHz)
// - Provides blocks of float samples for the FT8 TX path.

/**
 * Initialize the FT8 USB bridge.
 *
 * Must be called after AudioMemory() and after the audio system is
 * initialized (e.g., from InitializeAudio()).
 *
 * @param sdrSampleRateHz Phoenix SDR's internal sample rate (e.g. 192000.0f)
 */
void Ft8UsbBridge_Init(float sdrSampleRateHz);

/**
 * Get a block of SDR-rate audio samples for FT8 transmit.
 *
 * @param out       Pointer to output buffer (length = outCount floats)
 * @param outCount  Number of SDR-rate samples requested
 *
 * @return true if we produced a reasonably complete block,
 *         false if there was not enough USB audio yet.
 *
 * On failure, the caller should typically zero-fill the TX buffer
 * (or just accept that this block will be silent).
 */
bool Ft8UsbBridge_GetSamples(float *out, uint32_t outCount);
