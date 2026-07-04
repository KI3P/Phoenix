#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
#include <cmath>

/*
 * Regression tests for the noise-reduction NaN robustness fixes.
 *
 * Background: the radio worked when freshly flashed but went silent after a cold
 * power cycle whenever noise reduction was enabled. The convolution filter's
 * overlap-add history buffers (last_sample_buffer_L/R in DSP_FFT.cpp) live in
 * DMAMEM, which the Teensy 4.x does NOT zero-initialize at cold boot. The random
 * bit patterns there (some NaN/Inf) were fed into the convolution FFT on the
 * first frame and then latched *permanently* into the noise-reduction feedback
 * state (Kim's NR_Gts, Spectral's xt, Xanr's weights), collapsing the audio.
 *
 * Two fixes:
 *   1. InitializeFilters() now clears the convolution overlap buffers.
 *   2. The NR algorithms sanitize/clamp so a transient NaN/Inf cannot latch.
 *
 * These tests reproduce the failure mode on the host and prove recovery.
 */

extern ReceiveFilterConfig RXfilters;

namespace {

constexpr uint32_t kFrame = NR_FFT_L;        // 256 samples per NR frame
constexpr uint32_t kSampleRate = 24000;      // post-decimation audio rate

// True if every element of buf[0..N) is a finite number (not NaN or Inf).
bool AllFinite(const float32_t *buf, uint32_t N) {
    for (uint32_t i = 0; i < N; i++) {
        if (!std::isfinite(buf[i])) {
            return false;
        }
    }
    return true;
}

// Fill a frame with a finite test tone in both I and Q. 'phase' lets successive
// frames be continuous so the overlapped FFT processing behaves realistically.
void FillTone(float32_t *I, float32_t *Q, uint32_t N, float32_t phase) {
    for (uint32_t i = 0; i < N; i++) {
        float32_t s = 0.2f * sinf(2.0f * (float32_t)M_PI * 30.0f *
                                  ((float32_t)i + phase) / (float32_t)N);
        I[i] = s;
        Q[i] = s;
    }
}

// Configure the active band so the NR voice-activity detection range is valid.
void SetupBand() {
    bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = 200;
    bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = 3000;
}

} // namespace

// ---------------------------------------------------------------------------
// Fix #1: the convolution overlap buffers are cleared by InitializeFilters().
//
// We poison the (file-static) overlap buffers by pushing a frame containing a
// NaN through ConvolutionFilter - exactly what cold-boot DMAMEM garbage would
// do. InitializeFilters() then simulates the boot-time init. A subsequent clean
// frame must produce finite output: without the CLEAR_VAR added to
// InitializeFilters the leftover NaN feeds straight back into the FFT.
// ---------------------------------------------------------------------------
TEST(NoiseReduction, ConvolutionFilterClearsOverlapBuffersOnInit) {
    SetupBand();
    InitializeFilters(SPECTRUM_ZOOM_1, &RXfilters);

    float32_t I[kFrame];
    float32_t Q[kFrame];
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = READ_BUFFER_SIZE / RXfilters.DF;   // ConvolutionFilter requires this
    data.sampleRate_Hz = kSampleRate;

    ASSERT_EQ(data.N, kFrame);

    // Poison the overlap history with a NaN (stored into last_sample_buffer_L/R).
    FillTone(I, Q, data.N, 0);
    I[42] = std::nanf("");
    Q[42] = std::nanf("");
    ConvolutionFilter(&data, &RXfilters, nullptr);

    // Simulate the boot-time initialization that the fix now performs.
    InitializeFilters(SPECTRUM_ZOOM_1, &RXfilters);

    // A clean frame must yield finite output now that the history is cleared.
    FillTone(I, Q, data.N, kFrame);
    ConvolutionFilter(&data, &RXfilters, nullptr);
    EXPECT_TRUE(AllFinite(data.I, data.N));
    EXPECT_TRUE(AllFinite(data.Q, data.N));
}

// ---------------------------------------------------------------------------
// Fix #2a: Kim NR recovers from a transient NaN.
//
// Without the NaN-aware gain clamp, a single bad frame poisons the NR_Gts time
// smoothing feedback, which then stays NaN forever and silences the audio.
// ---------------------------------------------------------------------------
TEST(NoiseReduction, Kim1RecoversFromTransientNan) {
    SetupBand();
    InitializeKim1NoiseReduction();

    float32_t I[kFrame];
    float32_t Q[kFrame];
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = kFrame;
    data.sampleRate_Hz = kSampleRate;

    // Warm up with clean audio.
    for (int f = 0; f < 8; f++) {
        FillTone(I, Q, kFrame, (float32_t)f * kFrame);
        Kim1_NR(&data);
    }

    // Inject one corrupted frame.
    FillTone(I, Q, kFrame, 100);
    I[37] = std::nanf("");
    Kim1_NR(&data);

    // Recover with clean audio.
    for (int f = 0; f < 20; f++) {
        FillTone(I, Q, kFrame, (float32_t)f * kFrame);
        Kim1_NR(&data);
    }

    EXPECT_TRUE(AllFinite(data.I, kFrame));
    EXPECT_TRUE(AllFinite(data.Q, kFrame));
}

// ---------------------------------------------------------------------------
// Fix #2b: Spectral NR recovers from a transient NaN.
//
// Without sanitizing NR_X (and guarding xt), the self-referential noise
// estimate xt latches NaN and corrupts the spectral weighting indefinitely.
// ---------------------------------------------------------------------------
TEST(NoiseReduction, SpectralRecoversFromTransientNan) {
    SetupBand();
    InitializeKim1NoiseReduction();        // clears shared buffers (incl. overlap)
    InitializeSpectralNoiseReduction();

    float32_t I[kFrame];
    float32_t Q[kFrame];
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = kFrame;
    data.sampleRate_Hz = kSampleRate;

    // Warm up past the ~20-frame noise-learning phase.
    for (int f = 0; f < 30; f++) {
        FillTone(I, Q, kFrame, (float32_t)f * kFrame);
        SpectralNoiseReduction(&data);
    }

    // Inject one corrupted frame.
    FillTone(I, Q, kFrame, 100);
    I[91] = std::nanf("");
    SpectralNoiseReduction(&data);

    // Recover with clean audio.
    for (int f = 0; f < 40; f++) {
        FillTone(I, Q, kFrame, (float32_t)f * kFrame);
        SpectralNoiseReduction(&data);
    }

    EXPECT_TRUE(AllFinite(data.I, kFrame));
    EXPECT_TRUE(AllFinite(data.Q, kFrame));
}

// ---------------------------------------------------------------------------
// Fix #2c: Xanr (LMS noise reduction / notch) recovers from a transient NaN.
//
// Without input sanitization a NaN propagates into the adaptive weights ANR_w,
// which then never recover. Xanr writes its output to data->Q.
// ---------------------------------------------------------------------------
TEST(NoiseReduction, XanrRecoversFromTransientNan) {
    InitializeXanrNoiseReduction();

    float32_t I[kFrame];
    float32_t Q[kFrame];
    DataBlock data;
    data.I = I;
    data.Q = Q;
    data.N = kFrame;
    data.sampleRate_Hz = kSampleRate;

    // Warm up with clean audio.
    for (int f = 0; f < 8; f++) {
        FillTone(I, Q, kFrame, (float32_t)f * kFrame);
        Xanr(&data, 0);
    }

    // Inject one corrupted frame.
    FillTone(I, Q, kFrame, 100);
    I[10] = std::nanf("");
    Xanr(&data, 0);

    // Recover with clean audio.
    for (int f = 0; f < 40; f++) {
        FillTone(I, Q, kFrame, (float32_t)f * kFrame);
        Xanr(&data, 0);
    }

    EXPECT_TRUE(AllFinite(data.Q, kFrame));
}
