/**
 * Tests for the run-time generated filter tables.
 *
 * Several stages of the DSP chain used to ship coefficient tables designed
 * offline for a single audio sample rate of 24 ksps. They are now generated
 * from an analog design spec on every sample rate change, so that a filter
 * labelled 2.0 kHz cuts at 2.0 kHz whether the radio is running at 192 ksps or
 * 176.4 ksps.
 *
 * These tests answer two questions:
 *
 *   1. Do the generated filters still do what the frozen tables did? Every
 *      response here is measured against the original coefficients, kept in
 *      reference_filters.cpp, at the rate they were designed for.
 *
 *   2. Do they now hold their frequencies when the sample rate changes? The
 *      same corners and peaks are measured at both rates and the ratio checked
 *      against 1.0. Before this work those ratios read 176400/192000 = 0.919.
 *
 * Sweeps are written to filtercmp_*.txt for compare_176k_vs_192k.ipynb.
 */

#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
#include "reference_filters.h"

// ---------------------------------------------------------------------------
//  Measurement
// ---------------------------------------------------------------------------

/** Samples used to measure a settled response. Long enough that the DFT bin is
 *  narrow (5.9 Hz at 24 ksps) and short enough to stay fast. */
#define MEASURE_SAMPLES 4096

/** Blocks pushed through a filter before measuring, to let the state settle. */
#define WARMUP_BLOCKS 4

/**
 * Amplitude of a single frequency component of a real signal.
 *
 * Correlates the signal against a complex exponential at tone_Hz through a Hann
 * window. Peak picking, which the older sweep tests use, misses the true peak
 * of a sampled sine by up to a decibel when there are only a handful of samples
 * per cycle; this is exact to well under 0.01 dB, which is what comparing two
 * filter designs against each other needs.
 *
 * @param signal Samples to analyse
 * @param nSamples Length of signal
 * @param tone_Hz Frequency to measure
 * @param fs_Hz Sample rate
 * @return Amplitude of that component
 */
static float32_t ToneAmplitude(const float32_t *signal, uint32_t nSamples,
                               float32_t tone_Hz, float32_t fs_Hz) {
    double sumI = 0.0, sumQ = 0.0, windowSum = 0.0;
    const double w = 2.0 * M_PI * (double)tone_Hz / (double)fs_Hz;
    for (uint32_t n = 0; n < nSamples; n++) {
        const double hann = 0.5 * (1.0 - cos(2.0 * M_PI * (double)n / (double)(nSamples - 1)));
        windowSum += hann;
        sumI += hann * (double)signal[n] * cos(w * (double)n);
        sumQ += hann * (double)signal[n] * sin(w * (double)n);
    }
    if (windowSum == 0.0) return 0.0;
    // The factor of two recovers the amplitude split between the positive and
    // negative frequency halves of a real signal.
    return (float32_t)(2.0 * sqrt(sumI * sumI + sumQ * sumQ) / windowSum);
}

/** Fill a buffer with a unit-amplitude cosine, continuing from sample offset. */
static void FillTone(float32_t *buffer, uint32_t nSamples, float32_t tone_Hz,
                     float32_t fs_Hz, uint32_t offset) {
    const double w = 2.0 * M_PI * (double)tone_Hz / (double)fs_Hz;
    for (uint32_t n = 0; n < nSamples; n++) {
        buffer[n] = (float32_t)cos(w * (double)(n + offset));
    }
}

/**
 * Gain of a biquad cascade at one frequency, measured through the ARM filter.
 *
 * Runs the same arm_biquad_cascade_df2T_f32 the radio uses, so the result
 * includes whatever the float32 arithmetic does, not just the ideal response.
 *
 * @param coeffs Cascade coefficients, nSections sets of 5
 * @param nSections Number of biquad sections
 * @param tone_Hz Frequency to measure at
 * @param fs_Hz Sample rate
 * @return Linear gain
 */
static float32_t BiquadCascadeGain(const float32_t *coeffs, uint32_t nSections,
                                   float32_t tone_Hz, float32_t fs_Hz) {
    std::vector<float32_t> state(nSections * 2, 0.0f);
    std::vector<float32_t> in(MEASURE_SAMPLES), out(MEASURE_SAMPLES);

    arm_biquad_cascade_df2T_instance_f32 filter;
    filter.numStages = nSections;
    filter.pState = state.data();
    filter.pCoeffs = (float32_t *)coeffs;

    uint32_t offset = 0;
    for (int block = 0; block < WARMUP_BLOCKS; block++) {
        FillTone(in.data(), MEASURE_SAMPLES, tone_Hz, fs_Hz, offset);
        arm_biquad_cascade_df2T_f32(&filter, in.data(), out.data(), MEASURE_SAMPLES);
        offset += MEASURE_SAMPLES;
    }
    return ToneAmplitude(out.data(), MEASURE_SAMPLES, tone_Hz, fs_Hz);
}

/**
 * Gain of an FIR filter at one frequency, measured through the ARM filter.
 *
 * @param taps Filter coefficients
 * @param nTaps Number of taps
 * @param tone_Hz Frequency to measure at
 * @param fs_Hz Sample rate
 * @return Linear gain
 */
static float32_t FIRGain(const float32_t *taps, uint32_t nTaps,
                         float32_t tone_Hz, float32_t fs_Hz) {
    std::vector<float32_t> state(nTaps + MEASURE_SAMPLES - 1, 0.0f);
    std::vector<float32_t> in(MEASURE_SAMPLES), out(MEASURE_SAMPLES);

    arm_fir_instance_f32 filter;
    arm_fir_init_f32(&filter, nTaps, (float32_t *)taps, state.data(), MEASURE_SAMPLES);

    uint32_t offset = 0;
    for (int block = 0; block < 2; block++) {
        FillTone(in.data(), MEASURE_SAMPLES, tone_Hz, fs_Hz, offset);
        arm_fir_f32(&filter, in.data(), out.data(), MEASURE_SAMPLES);
        offset += MEASURE_SAMPLES;
    }
    return ToneAmplitude(out.data(), MEASURE_SAMPLES, tone_Hz, fs_Hz);
}

static float32_t ToDB(float32_t gain) {
    return 20.0f * log10f(gain + 1e-30f);
}

/** Write a frequency sweep as index,frequency,gain - the format the notebooks read. */
static void WriteSweep(const char *filename, const float32_t *freq,
                       const float32_t *gain, uint32_t nPoints) {
    FILE *file = fopen(filename, "w");
    if (file == NULL) return;
    for (uint32_t i = 0; i < nPoints; i++) {
        fprintf(file, "%u,%7.6f,%7.6f\n", i, freq[i], gain[i]);
    }
    fclose(file);
}

// ---------------------------------------------------------------------------
//  Response feature finders
// ---------------------------------------------------------------------------

/** Frequency at which a response first falls level_dB below its value at fLow. */
static float32_t LowpassCorner(const float32_t *coeffs, uint32_t nSections,
                               float32_t fLow, float32_t fHigh, float32_t fStep,
                               float32_t level_dB, float32_t fs_Hz, bool isFIR,
                               uint32_t nTaps) {
    const float32_t reference = isFIR ? FIRGain(coeffs, nTaps, fLow, fs_Hz)
                                      : BiquadCascadeGain(coeffs, nSections, fLow, fs_Hz);
    const float32_t target = reference * powf(10.0f, level_dB / 20.0f);
    float32_t prevF = fLow, prevG = reference;
    for (float32_t f = fLow + fStep; f <= fHigh; f += fStep) {
        const float32_t g = isFIR ? FIRGain(coeffs, nTaps, f, fs_Hz)
                                  : BiquadCascadeGain(coeffs, nSections, f, fs_Hz);
        if (g <= target) {
            // Linear interpolation between the bracketing points.
            return prevF + (target - prevG) * (f - prevF) / (g - prevG);
        }
        prevF = f;
        prevG = g;
    }
    return -1.0f;
}

/** Frequency of maximum response over a range. */
static float32_t BandpassPeak(const float32_t *coeffs, uint32_t nSections,
                              float32_t fLow, float32_t fHigh, float32_t fStep,
                              float32_t fs_Hz) {
    float32_t bestF = fLow, bestG = -1.0f;
    for (float32_t f = fLow; f <= fHigh; f += fStep) {
        const float32_t g = BiquadCascadeGain(coeffs, nSections, f, fs_Hz);
        if (g > bestG) {
            bestG = g;
            bestF = f;
        }
    }
    return bestF;
}

// ---------------------------------------------------------------------------
//  Fixtures for switching sample rate
// ---------------------------------------------------------------------------

/** Audio sample rate the frozen reference tables were designed for. */
#define REFERENCE_AUDIO_FS_HZ 24000.0f

static float32_t AudioRateHz() {
    return (float32_t)SR[SampleRate].rate / (float32_t)RXfilters.DF;
}

/**
 * Rebuild every filter table for a given sample rate.
 *
 * SampleRate is a reference into the persisted config, and gtest shares process
 * state between tests, so anything that changes it has to put it back.
 */
static void RebuildFiltersAtRate(uint8_t rate) {
    SampleRate = rate;
    InitializeFilters(SPECTRUM_ZOOM_1, &RXfilters);
    InitializeTransmitFilters(&TXfilters);
}

/** Nominal cutoffs of the five CW audio filters, in Hz. */
static const float32_t CW_FILTER_FC_HZ[5] = {840.0f, 1080.0f, 1320.0f, 1800.0f, 2000.0f};

/** Centre frequencies of the 14 equaliser cells, in Hz. */
static const float32_t EQ_FC_HZ[EQUALIZER_CELL_COUNT] = {
    198.425f, 250.0f, 314.98f, 400.0f, 500.0f, 630.0f, 793.0f,
    1000.0f, 1259.0f, 1587.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f
};

/** The generated CW audio coefficient tables, in the same order as above. */
static const float32_t *GeneratedCWFilter(int index) {
    switch (index) {
        case 0: return CW_AudioFilterCoeffs1;
        case 1: return CW_AudioFilterCoeffs2;
        case 2: return CW_AudioFilterCoeffs3;
        case 3: return CW_AudioFilterCoeffs4;
        default: return CW_AudioFilterCoeffs5;
    }
}

/** The reference CW audio coefficient tables. */
static const float32_t *ReferenceCWFilter(int index) {
    switch (index) {
        case 0: return CW_AudioFilterCoeffs1_ref;
        case 1: return CW_AudioFilterCoeffs2_ref;
        case 2: return CW_AudioFilterCoeffs3_ref;
        case 3: return CW_AudioFilterCoeffs4_ref;
        default: return CW_AudioFilterCoeffs5_ref;
    }
}

/** The reference equaliser coefficient tables. */
static const float32_t *ReferenceEQBand(int index) {
    static const float32_t *bands[EQUALIZER_CELL_COUNT] = {
        EQ_Band1Coeffs_ref, EQ_Band2Coeffs_ref, EQ_Band3Coeffs_ref, EQ_Band4Coeffs_ref,
        EQ_Band5Coeffs_ref, EQ_Band6Coeffs_ref, EQ_Band7Coeffs_ref, EQ_Band8Coeffs_ref,
        EQ_Band9Coeffs_ref, EQ_Band10Coeffs_ref, EQ_Band11Coeffs_ref, EQ_Band12Coeffs_ref,
        EQ_Band13Coeffs_ref, EQ_Band14Coeffs_ref
    };
    return bands[index];
}

// ===========================================================================
//  1. Do the generated filters match the tables they replace?
// ===========================================================================

TEST(FilterDesign, CWAudioFiltersMatchReference) {
    RebuildFiltersAtRate(SAMPLE_RATE_192K);
    ASSERT_FLOAT_EQ(AudioRateHz(), REFERENCE_AUDIO_FS_HZ)
        << "the reference tables were designed at this rate";

    const uint32_t nPoints = 121;
    float32_t freq[nPoints], genDB[nPoints], refDB[nPoints];
    char filename[64];

    for (int band = 0; band < 5; band++) {
        const float32_t fMin = 100.0f;
        const float32_t fMax = CW_FILTER_FC_HZ[band] * 2.5f;
        const float32_t fStep = (fMax - fMin) / (float32_t)(nPoints - 1);
        float32_t worstError = 0.0f;

        for (uint32_t i = 0; i < nPoints; i++) {
            freq[i] = fMin + (float32_t)i * fStep;
            genDB[i] = ToDB(BiquadCascadeGain(GeneratedCWFilter(band), 6, freq[i],
                                              REFERENCE_AUDIO_FS_HZ));
            refDB[i] = ToDB(BiquadCascadeGain(ReferenceCWFilter(band), 6, freq[i],
                                              REFERENCE_AUDIO_FS_HZ));
            // Below the noise floor of the cascade the two designs are both
            // just rounding error, so only compare where there is signal.
            if (refDB[i] > -90.0f) {
                worstError = fmaxf(worstError, fabsf(genDB[i] - refDB[i]));
            }
        }

        sprintf(filename, "filtercmp_cw_band_%d_gen.txt", band);
        WriteSweep(filename, freq, genDB, nPoints);
        sprintf(filename, "filtercmp_cw_band_%d_ref.txt", band);
        WriteSweep(filename, freq, refDB, nPoints);

        EXPECT_LT(worstError, 0.05f)
            << "CW audio filter " << band << " (" << CW_FILTER_FC_HZ[band]
            << " Hz) departs from the reference Chebyshev by " << worstError << " dB";
    }
}

TEST(FilterDesign, EqualiserCellsMatchReference) {
    RebuildFiltersAtRate(SAMPLE_RATE_192K);
    ASSERT_FLOAT_EQ(AudioRateHz(), REFERENCE_AUDIO_FS_HZ);

    const uint32_t nPoints = 121;
    float32_t freq[nPoints], genDB[nPoints], refDB[nPoints];
    char filename[64];

    for (int band = 0; band < EQUALIZER_CELL_COUNT; band++) {
        const float32_t fMin = EQ_FC_HZ[band] * 0.3f;
        // Stop short of Nyquist. These cells have a zero there, and comparing
        // two designs inside a null measures rounding error, not the response.
        const float32_t fMax = fminf(EQ_FC_HZ[band] * 3.0f, REFERENCE_AUDIO_FS_HZ * 0.45f);
        const float32_t fStep = (fMax - fMin) / (float32_t)(nPoints - 1);
        float32_t worstError = 0.0f;

        for (uint32_t i = 0; i < nPoints; i++) {
            freq[i] = fMin + (float32_t)i * fStep;
            genDB[i] = ToDB(BiquadCascadeGain(*EQ_Coeffs[band], 4, freq[i],
                                              REFERENCE_AUDIO_FS_HZ));
            refDB[i] = ToDB(BiquadCascadeGain(ReferenceEQBand(band), 4, freq[i],
                                              REFERENCE_AUDIO_FS_HZ));
            // Deep in the stopband both designs are down in the float32 noise.
            if (refDB[i] > -90.0f) {
                worstError = fmaxf(worstError, fabsf(genDB[i] - refDB[i]));
            }
        }

        sprintf(filename, "filtercmp_eq_band_%d_gen.txt", band);
        WriteSweep(filename, freq, genDB, nPoints);
        sprintf(filename, "filtercmp_eq_band_%d_ref.txt", band);
        WriteSweep(filename, freq, refDB, nPoints);

        // The prototype is recovered from these very tables by an exact inverse
        // bilinear transform, so anything above rounding error is a real defect.
        EXPECT_LT(worstError, 0.01f)
            << "equaliser cell " << band << " (" << EQ_FC_HZ[band]
            << " Hz) departs from the reference by " << worstError << " dB";
    }
}

TEST(FilterDesign, CWDecodeFIRMatchesReferenceCorners) {
    RebuildFiltersAtRate(SAMPLE_RATE_192K);

    // The replacement is a Kaiser windowed sinc where the original was a
    // Parks-McClellan design, so the skirts differ in detail. What has to carry
    // over is where the filter turns over and how well it stops.
    const float32_t genMinus6 = LowpassCorner(CW_Filter_Coeffs2, 0, 100.0f, 6000.0f, 5.0f,
                                              -6.0f, REFERENCE_AUDIO_FS_HZ, true, 64);
    const float32_t refMinus6 = LowpassCorner(CW_Filter_Coeffs2_ref, 0, 100.0f, 6000.0f, 5.0f,
                                              -6.0f, REFERENCE_AUDIO_FS_HZ, true, 64);
    EXPECT_NEAR(genMinus6 / refMinus6, 1.0f, 0.02f)
        << "generated -6 dB corner " << genMinus6 << " Hz vs reference " << refMinus6 << " Hz";

    // The CW decoder thresholds on absolute magnitude, so passband gain matters.
    EXPECT_NEAR(FIRGain(CW_Filter_Coeffs2, 64, 200.0f, REFERENCE_AUDIO_FS_HZ), 1.0f, 0.01f);

    // Well past the corner it must still be a stopband.
    EXPECT_LT(ToDB(FIRGain(CW_Filter_Coeffs2, 64, 4000.0f, REFERENCE_AUDIO_FS_HZ)), -60.0f);
}

TEST(FilterDesign, TransmitAudioFilterMatchesReferenceCorners) {
    RebuildFiltersAtRate(SAMPLE_RATE_192K);

    const float32_t genMinus3 = LowpassCorner(FIR_int3_12ksps_48tap_2k7, 0, 100.0f, 8000.0f, 5.0f,
                                              -3.0f, REFERENCE_AUDIO_FS_HZ, true, 48);
    const float32_t refMinus3 = LowpassCorner(FIR_int3_12ksps_48tap_2k7_ref, 0, 100.0f, 8000.0f, 5.0f,
                                              -3.0f, REFERENCE_AUDIO_FS_HZ, true, 48);
    EXPECT_NEAR(genMinus3 / refMinus3, 1.0f, 0.02f)
        << "transmit audio bandwidth moved: generated " << genMinus3
        << " Hz vs reference " << refMinus3 << " Hz";

    EXPECT_NEAR(FIRGain(FIR_int3_12ksps_48tap_2k7, 48, 300.0f, REFERENCE_AUDIO_FS_HZ), 1.0f, 0.01f);
}

// ===========================================================================
//  2. Do the generated filters hold their frequencies across sample rates?
// ===========================================================================

TEST(FilterDesign, GeneratedCornersHoldAcrossRates) {
    const uint8_t savedRate = SampleRate;

    float32_t cw192[5], cw176[5], eq192[EQUALIZER_CELL_COUNT], eq176[EQUALIZER_CELL_COUNT];
    float32_t txAudio192, txAudio176, cwDecode192, cwDecode176;

    RebuildFiltersAtRate(SAMPLE_RATE_192K);
    float32_t fs = AudioRateHz();
    for (int b = 0; b < 5; b++) {
        cw192[b] = LowpassCorner(GeneratedCWFilter(b), 6, 200.0f, 3600.0f, 5.0f, -6.0f, fs, false, 0);
    }
    for (int b = 0; b < EQUALIZER_CELL_COUNT; b++) {
        eq192[b] = BandpassPeak(*EQ_Coeffs[b], 4, EQ_FC_HZ[b] * 0.85f, EQ_FC_HZ[b] * 1.15f,
                                EQ_FC_HZ[b] * 0.002f, fs);
    }
    txAudio192 = LowpassCorner(FIR_int3_12ksps_48tap_2k7, 0, 100.0f, 8000.0f, 5.0f, -3.0f, fs, true, 48);
    cwDecode192 = LowpassCorner(CW_Filter_Coeffs2, 0, 100.0f, 6000.0f, 5.0f, -6.0f, fs, true, 64);

    RebuildFiltersAtRate(SAMPLE_RATE_176K);
    fs = AudioRateHz();
    for (int b = 0; b < 5; b++) {
        cw176[b] = LowpassCorner(GeneratedCWFilter(b), 6, 200.0f, 3600.0f, 5.0f, -6.0f, fs, false, 0);
    }
    for (int b = 0; b < EQUALIZER_CELL_COUNT; b++) {
        eq176[b] = BandpassPeak(*EQ_Coeffs[b], 4, EQ_FC_HZ[b] * 0.85f, EQ_FC_HZ[b] * 1.15f,
                                EQ_FC_HZ[b] * 0.002f, fs);
    }
    txAudio176 = LowpassCorner(FIR_int3_12ksps_48tap_2k7, 0, 100.0f, 8000.0f, 5.0f, -3.0f, fs, true, 48);
    cwDecode176 = LowpassCorner(CW_Filter_Coeffs2, 0, 100.0f, 6000.0f, 5.0f, -6.0f, fs, true, 64);

    SampleRate = savedRate;
    RebuildFiltersAtRate(savedRate);

    FILE *summary = fopen("filtercmp_rate_summary.txt", "w");
    if (summary != NULL) {
        fprintf(summary, "stage,type,f_192k_Hz,f_176k_Hz,measured_ratio,expected_ratio\n");
        for (int b = 0; b < 5; b++) {
            fprintf(summary, "CW_audio_%.0f_minus6dB,fs_derived,%.1f,%.1f,%.4f,1.0000\n",
                    CW_FILTER_FC_HZ[b], cw192[b], cw176[b], cw176[b] / cw192[b]);
        }
        for (int b = 0; b < EQUALIZER_CELL_COUNT; b++) {
            fprintf(summary, "EQ_band%d_%.0f_peak,fs_derived,%.1f,%.1f,%.4f,1.0000\n",
                    b, EQ_FC_HZ[b], eq192[b], eq176[b], eq176[b] / eq192[b]);
        }
        fprintf(summary, "TX_audio_bandwidth_minus3dB,fs_derived,%.1f,%.1f,%.4f,1.0000\n",
                txAudio192, txAudio176, txAudio176 / txAudio192);
        fprintf(summary, "CW_decode_FIR_minus6dB,fs_derived,%.1f,%.1f,%.4f,1.0000\n",
                cwDecode192, cwDecode176, cwDecode176 / cwDecode192);
        fclose(summary);
    }

    for (int b = 0; b < 5; b++) {
        EXPECT_NEAR(cw176[b] / cw192[b], 1.0f, 0.01f)
            << "CW audio filter " << CW_FILTER_FC_HZ[b] << " Hz moved from "
            << cw192[b] << " Hz to " << cw176[b] << " Hz across the rate change";
    }
    for (int b = 0; b < EQUALIZER_CELL_COUNT; b++) {
        EXPECT_NEAR(eq176[b] / eq192[b], 1.0f, 0.01f)
            << "equaliser cell " << EQ_FC_HZ[b] << " Hz moved from "
            << eq192[b] << " Hz to " << eq176[b] << " Hz across the rate change";
    }
    EXPECT_NEAR(txAudio176 / txAudio192, 1.0f, 0.01f)
        << "transmit audio bandwidth moved from " << txAudio192 << " Hz to " << txAudio176 << " Hz";
    EXPECT_NEAR(cwDecode176 / cwDecode192, 1.0f, 0.01f)
        << "CW decode filter moved from " << cwDecode192 << " Hz to " << cwDecode176 << " Hz";
}

TEST(FilterDesign, EqualiserCellsPeakOnTheirLabelledCentres) {
    const uint8_t savedRate = SampleRate;
    const uint8_t rates[2] = {SAMPLE_RATE_192K, SAMPLE_RATE_176K};

    for (int r = 0; r < 2; r++) {
        RebuildFiltersAtRate(rates[r]);
        const float32_t fs = AudioRateHz();
        for (int b = 0; b < EQUALIZER_CELL_COUNT; b++) {
            const float32_t peak = BandpassPeak(*EQ_Coeffs[b], 4, EQ_FC_HZ[b] * 0.85f,
                                                EQ_FC_HZ[b] * 1.15f, EQ_FC_HZ[b] * 0.002f, fs);
            EXPECT_NEAR(peak / EQ_FC_HZ[b], 1.0f, 0.01f)
                << "cell " << b << " labelled " << EQ_FC_HZ[b] << " Hz peaks at "
                << peak << " Hz at " << SR[rates[r]].rate << " sps";
            // Peak gain is what the equaliser's flat setting depends on.
            EXPECT_NEAR(BiquadCascadeGain(*EQ_Coeffs[b], 4, peak, fs), 1.0f, 0.02f);
        }
    }

    SampleRate = savedRate;
    RebuildFiltersAtRate(savedRate);
}

// ===========================================================================
//  3. Behaviour the redesign must not break
// ===========================================================================

/** Number of points in the flat-equaliser sweep. */
#define FLATSUM_POINTS 61

/**
 * Response of BandEQ with every cell at unity, measured through the real path.
 *
 * @param responseDB Output, FLATSUM_POINTS values
 * @param freq Output, the frequencies measured at
 * @param useReference Bind the cells to the reference tables instead of the generated ones
 */
static void MeasureFlatEqualiserSum(float32_t *responseDB, float32_t *freq, bool useReference) {
    const float32_t fs = AudioRateHz();
    const uint32_t nSamples = READ_BUFFER_SIZE / RXfilters.DF;

    if (useReference) {
        for (int i = 0; i < EQUALIZER_CELL_COUNT; i++) {
            RXfilters.S_Rec[i].pCoeffs = (float32_t *)ReferenceEQBand(i);
            memset(RXfilters.S_Rec[i].pState, 0,
                   sizeof(float32_t) * 2 * RXfilters.eqNumStages);
        }
    }

    for (uint32_t i = 0; i < FLATSUM_POINTS; i++) {
        // Log spacing over the range the equaliser covers.
        freq[i] = 250.0f * powf(3200.0f / 250.0f,
                                (float32_t)i / (float32_t)(FLATSUM_POINTS - 1));

        std::vector<float32_t> I(nSamples), Q(nSamples);
        DataBlock data;
        data.I = I.data();
        data.Q = Q.data();
        data.N = nSamples;
        data.sampleRate_Hz = fs;

        // The cells at the bottom of the range have long impulse responses, so
        // they need several blocks before the output settles.
        uint32_t offset = 0;
        for (int block = 0; block < 12; block++) {
            FillTone(I.data(), nSamples, freq[i], fs, offset);
            BandEQ(&data, &RXfilters, RX);
            offset += nSamples;
        }
        responseDB[i] = ToDB(ToneAmplitude(data.I, nSamples, freq[i], fs));
    }
}

/** Peak-to-peak variation of a response, in dB. */
static float32_t Ripple(const float32_t *responseDB, uint32_t nPoints) {
    float32_t lo = responseDB[0], hi = responseDB[0];
    for (uint32_t i = 1; i < nPoints; i++) {
        lo = fminf(lo, responseDB[i]);
        hi = fmaxf(hi, responseDB[i]);
    }
    return hi - lo;
}

TEST(FilterDesign, FlatEqualiserSumStaysFlat) {
    const uint8_t savedRate = SampleRate;

    // BandEQ sums the 14 cells with alternating sign, a reconstruction that only
    // comes out flat if every cell keeps its designed shape and peak gain. It is
    // the property most at risk from regenerating the cells, so check it through
    // the real BandEQ path rather than from the coefficients.
    //
    // Note the flat sum does not sit at 0 dB - the overlapping cells add to
    // roughly +4 dB. That is how the equaliser has always behaved; what matters
    // is that the sum is level, and that regenerating the cells did not move it.
    int32_t savedEQ[EQUALIZER_CELL_COUNT];
    for (int i = 0; i < EQUALIZER_CELL_COUNT; i++) {
        savedEQ[i] = ED.equalizerRec[i];
        ED.equalizerRec[i] = 100;  // flat
    }

    float32_t freq[FLATSUM_POINTS];
    float32_t generated192[FLATSUM_POINTS], reference192[FLATSUM_POINTS];
    float32_t generated176[FLATSUM_POINTS];

    RebuildFiltersAtRate(SAMPLE_RATE_192K);
    MeasureFlatEqualiserSum(generated192, freq, false);
    // Rebuilding puts the coefficient pointers back before the reference pass.
    RebuildFiltersAtRate(SAMPLE_RATE_192K);
    MeasureFlatEqualiserSum(reference192, freq, true);
    WriteSweep("filtercmp_eq_flatsum_192000_gen.txt", freq, generated192, FLATSUM_POINTS);
    WriteSweep("filtercmp_eq_flatsum_192000_ref.txt", freq, reference192, FLATSUM_POINTS);

    RebuildFiltersAtRate(SAMPLE_RATE_176K);
    MeasureFlatEqualiserSum(generated176, freq, false);
    WriteSweep("filtercmp_eq_flatsum_176400_gen.txt", freq, generated176, FLATSUM_POINTS);

    // The generated cells must reconstruct exactly what the frozen ones did.
    for (uint32_t i = 0; i < FLATSUM_POINTS; i++) {
        EXPECT_NEAR(generated192[i], reference192[i], 0.05f)
            << "flat equaliser sum differs from the reference by "
            << (generated192[i] - reference192[i]) << " dB at " << freq[i] << " Hz";
    }

    // And the reconstruction must survive the rate change.
    const float32_t rippleRef = Ripple(reference192, FLATSUM_POINTS);
    const float32_t ripple176 = Ripple(generated176, FLATSUM_POINTS);
    EXPECT_LT(ripple176, rippleRef + 0.25f)
        << "flat equaliser is less level at 176.4 ksps (" << ripple176
        << " dB peak to peak) than the reference is at 192 ksps (" << rippleRef << " dB)";

    for (uint32_t i = 0; i < FLATSUM_POINTS; i++) {
        EXPECT_NEAR(generated176[i], reference192[i], 0.5f)
            << "flat equaliser at 176.4 ksps is " << (generated176[i] - reference192[i])
            << " dB away from the 192 ksps reference at " << freq[i] << " Hz";
    }

    for (int i = 0; i < EQUALIZER_CELL_COUNT; i++) ED.equalizerRec[i] = savedEQ[i];
    SampleRate = savedRate;
    RebuildFiltersAtRate(savedRate);
}

TEST(FilterDesign, TransmitDecimatorRejectsAliases) {
    const uint8_t savedRate = SampleRate;
    const uint8_t rates[2] = {SAMPLE_RATE_192K, SAMPLE_RATE_176K};

    // coeffs12K_8K_LPF_FIR feeds a decimate-by-2 stage, so everything above a
    // quarter of its input rate folds back into the transmit audio. The table it
    // replaces was flat to 0.425 * Fs and gave essentially no protection.
    const float32_t referenceAtFold = ToDB(FIRGain(coeffs12K_8K_LPF_FIR_ref, 48,
                                                   REFERENCE_AUDIO_FS_HZ * 0.25f,
                                                   REFERENCE_AUDIO_FS_HZ));
    EXPECT_GT(referenceAtFold, -6.0f)
        << "the reference table is expected to be wide open at the fold point; "
           "if this fails the reference data has changed";

    for (int r = 0; r < 2; r++) {
        RebuildFiltersAtRate(rates[r]);
        const float32_t fs = AudioRateHz();
        const float32_t atFold = ToDB(FIRGain(coeffs12K_8K_LPF_FIR, 48, fs * 0.25f, fs));
        EXPECT_LT(atFold, -60.0f)
            << "generated transmit decimator is only " << atFold
            << " dB down at the fold point, running at " << SR[rates[r]].rate << " sps";

        // It must still pass the audio the transmit chain actually carries.
        EXPECT_NEAR(FIRGain(coeffs12K_8K_LPF_FIR, 48, 2500.0f, fs), 1.0f, 0.05f);
    }

    SampleRate = savedRate;
    RebuildFiltersAtRate(savedRate);
}

TEST(FilterDesign, AMDCBlockerCornerHoldsAcrossRates) {
    const uint8_t savedRate = SampleRate;
    const uint8_t rates[2] = {SAMPLE_RATE_192K, SAMPLE_RATE_176K};
    float32_t corner[2];

    for (int r = 0; r < 2; r++) {
        RebuildFiltersAtRate(rates[r]);
        const float32_t fs = AudioRateHz();
        // y[n] = x[n] - x[n-1] + pole*y[n-1] is a one pole highpass; read its
        // -3 dB corner straight off the pole.
        corner[r] = -logf(RXfilters.amDCBlockPole) * fs / TWO_PI;
        EXPECT_NEAR(corner[r], RXfilters.amDCBlockCorner_Hz, 1.0f)
            << "at " << SR[rates[r]].rate << " sps";
    }
    EXPECT_NEAR(corner[1] / corner[0], 1.0f, 0.01f);

    SampleRate = savedRate;
    RebuildFiltersAtRate(savedRate);
}

// ===========================================================================
//  4. The generated tables reach the filters that use them
// ===========================================================================

TEST(FilterDesign, LiveCWFilterUsesGeneratedCoefficients) {
    const uint8_t savedRate = SampleRate;
    const int32_t savedIndex = ED.CWFilterIndex;

    // Run a tone through the real CWAudioFilter() path and check it lands where
    // the coefficient table says it should. This is what catches a table that is
    // generated but never bound, or bound but never re-cleared.
    for (int r = 0; r < 2; r++) {
        RebuildFiltersAtRate(r == 0 ? SAMPLE_RATE_192K : SAMPLE_RATE_176K);
        const float32_t fs = AudioRateHz();
        ED.CWFilterIndex = 4;  // the 2.0 kHz filter

        const uint32_t nSamples = READ_BUFFER_SIZE / 8;
        std::vector<float32_t> I(nSamples), Q(nSamples);
        DataBlock data;
        data.I = I.data();
        data.Q = Q.data();
        data.N = nSamples;
        data.sampleRate_Hz = fs;

        // In band it should pass, well out of band it should not.
        uint32_t offset = 0;
        for (int block = 0; block < 8; block++) {
            FillTone(I.data(), nSamples, 1000.0f, fs, offset);
            CWAudioFilter(&data, &RXfilters);
            offset += nSamples;
        }
        EXPECT_NEAR(ToneAmplitude(data.I, nSamples, 1000.0f, fs), 1.0f, 0.05f)
            << "at " << SR[SampleRate].rate << " sps";

        offset = 0;
        for (int block = 0; block < 8; block++) {
            FillTone(I.data(), nSamples, 4000.0f, fs, offset);
            CWAudioFilter(&data, &RXfilters);
            offset += nSamples;
        }
        EXPECT_LT(ToDB(ToneAmplitude(data.I, nSamples, 4000.0f, fs)), -60.0f)
            << "at " << SR[SampleRate].rate << " sps";
    }

    ED.CWFilterIndex = savedIndex;
    SampleRate = savedRate;
    RebuildFiltersAtRate(savedRate);
}
