#ifndef DSP_FFT_H
#define DSP_FFT_H
#include "SDT.h"

// Audio Power and Utilities

/**
 * @brief Get the maximum audio power from the most recent spectrum analysis
 * @return Maximum audio power level in arbitrary units
 * @note Used for automatic gain control and signal strength indication
 */
float32_t GetAudioPowerMax(void);

/**
 * @brief Fast approximation of base-10 logarithm
 * @param X Input value (must be positive)
 * @return log10(X) approximation
 * @note Faster than standard log10f() but with reduced precision
 */
float32_t log10f_fast(float32_t X);

// Power Spectral Density Calculation

/**
 * @brief Calculate 512-point Power Spectral Density from I/Q data
 * @param real Pointer to real (I) component array
 * @param imag Pointer to imaginary (Q) component array
 * @note Computes magnitude spectrum for display on waterfall/spectrum scope
 */
void CalcPSD512(float32_t *real, float32_t *imag);

/**
 * @brief Calculate 256-point Power Spectral Density from I/Q data
 * @param I Pointer to in-phase component array
 * @param Q Pointer to quadrature component array
 * @note This function is primarily used for unit testing
 */
void CalcPSD256(float32_t *I, float32_t *Q);

// Frequency Shifting

/**
 * @brief Shift frequency by Fs/4 using efficient Hilbert transform method
 * @param data Pointer to DataBlock containing I/Q samples
 * @note Implements digital mixing by +/- 1/4 sample rate using sign changes
 * @note Very efficient for USB/LSB sideband selection
 */
void FreqShiftFs4(DataBlock *data);

/**
 * @brief Shift frequency by arbitrary amount using NCO (Numerically Controlled Oscillator)
 * @param data Pointer to DataBlock containing I/Q samples
 * @param freqShift_Hz Frequency shift in Hz (positive or negative)
 * @note Uses complex multiplication with NCO for arbitrary frequency shifts
 */
void FreqShiftF(DataBlock *data, float32_t freqShift_Hz);

/**
 * @brief Shift frequency with explicit sample rate parameter
 * @param I Pointer to in-phase component array
 * @param Q Pointer to quadrature component array
 * @param blocksize Number of samples to process
 * @param freqShift_Hz Frequency shift in Hz
 * @param sampleRate_Hz Sample rate in Hz
 * @note Allows frequency shifting at different sample rates than main processing
 */
void FreqShiftF2(float32_t *I, float32_t *Q, uint32_t blocksize,
                float32_t freqShift_Hz, uint32_t sampleRate_Hz);

// FFT and Zoom Functions

/**
 * @brief Execute zoom FFT for high-resolution spectrum display
 * @param data Pointer to DataBlock containing I/Q samples
 * @param spectrum_zoom Zoom factor (1, 2, 4, 8, 16, 32, 64)
 * @param RXfilters Pointer to receive filter configuration
 * @return true if zoom FFT executed successfully, false otherwise
 * @note Implements software-defined "panadapter zoom" for detailed spectrum view
 */
bool ZoomFFTExe(DataBlock *data, uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters);

/**
 * @brief Prepare zoom FFT by configuring decimation filters
 * @param spectrum_zoom Zoom factor (1, 2, 4, 8, 16, 32, 64)
 * @param RXfilters Pointer to receive filter configuration
 * @note Must be called when zoom level changes to reconfigure filter chain
 */
void ZoomFFTPrep(uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters);

// Filter Coefficient Calculation

/**
 * @brief Calculate complex FIR filter coefficients for SSB filtering
 * @param coeffs_I Output buffer for in-phase coefficients
 * @param coeffs_Q Output buffer for quadrature coefficients
 * @param numCoeffs Number of filter taps to generate
 * @param FLoCut Low frequency cutoff in Hz
 * @param FHiCut High frequency cutoff in Hz
 * @param SampleRate Sample rate in Hz
 * @note Generates asymmetric bandpass filter for SSB demodulation
 */
void CalcCplxFIRCoeffs(float * coeffs_I, float * coeffs_Q, int numCoeffs,
                    float32_t FLoCut, float32_t FHiCut, float SampleRate);

/**
 * @brief Calculate real FIR filter coefficients using windowed sinc method
 * @param coeffs_I Output buffer for filter coefficients
 * @param numCoeffs Number of filter taps to generate
 * @param fc_Hz Center frequency in Hz
 * @param Astop_dB Stopband attenuation in dB
 * @param type Filter type (LOW_PASS, HIGH_PASS, BAND_PASS, BAND_STOP)
 * @param dfc_Hz Transition bandwidth in Hz
 * @param Fsamprate_Hz Sample rate in Hz
 * @note Uses Kaiser window for optimal stopband attenuation
 */
void CalcFIRCoeffs(float *coeffs_I, int numCoeffs, float32_t fc_Hz, float32_t Astop_dB,
                  FilterType type, float dfc_Hz, float Fsamprate_Hz);

/**
 * @brief Calculate IIR biquad filter coefficients
 * @param coefficient_set Output array for 5 biquad coefficients [b0,b1,b2,a1,a2]
 * @param f0 Center frequency in Hz
 * @param Q Quality factor (bandwidth control)
 * @param sample_rate Sample rate in Hz
 * @param filter_type Filter type (PEAK, NOTCH, LOW_SHELF, HIGH_SHELF)
 * @note Generates coefficients for parametric EQ and notch filtering
 */
void SetIIRCoeffs(float32_t *coefficient_set, float32_t f0, float32_t Q,
                    float32_t sample_rate, FilterType filter_type);

// Filter Management

/**
 * @brief Update FIR filter frequency mask for convolution filtering
 * @param RXfilters Pointer to receive filter configuration
 * @note Recalculates filter coefficients when filter bandwidth changes
 */
void UpdateFIRFilterMask(ReceiveFilterConfig *RXfilters);

/**
 * @brief Initialize all receive filters for specified zoom level
 * @param spectrum_zoom Zoom factor for FFT display
 * @param RXfilters Pointer to receive filter configuration
 * @note Called at startup and when sample rate changes
 */
void InitializeFilters(uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters);

/**
 * @brief Adjust SSB filter bandwidth
 * @param filter_change Direction of change (+1 wider, -1 narrower)
 * @param changeFilterHiCut 1=adjust high cutoff, 0=adjust low cutoff
 * @note Allows user to dynamically adjust filter passband during operation
 */
void FilterSetSSB(int32_t filter_change, uint8_t changeFilterHiCut);

/**
 * @brief Initialize FIR filter mask for frequency domain filtering
 * @param FIR_filter_mask Output buffer for filter frequency response
 * @param RXfilters Pointer to receive filter configuration
 * @note Used for overlap-save convolution filtering method
 */
void InitFilterMask(float32_t *FIR_filter_mask, ReceiveFilterConfig *RXfilters);

/**
 * @brief Apply convolution filter to received signal
 * @param data Pointer to DataBlock containing I/Q samples
 * @param RXfilters Pointer to receive filter configuration
 * @param fname Optional filename for debugging (can be NULL)
 * @return ESUCCESS on success, error code on failure
 * @note Implements efficient frequency-domain filtering via overlap-save method
 */
errno_t ConvolutionFilter(DataBlock *data, ReceiveFilterConfig *RXfilters, const char *fname);

// Decimation

/**
 * @brief Decimate signal by factor of 8 with anti-aliasing filtering
 * @param data Pointer to DataBlock containing I/Q samples
 * @param RXfilters Pointer to receive filter configuration
 * @return ESUCCESS on success, error code on failure
 * @note Reduces sample rate by 8x for zoom FFT processing
 */
errno_t DecimateBy8(DataBlock *data, ReceiveFilterConfig *RXfilters);

/**
 * @brief Decimate signal by factor of 4 with anti-aliasing filtering
 * @param data Pointer to DataBlock containing I/Q samples
 * @param RXfilters Pointer to receive filter configuration
 * @return ESUCCESS on success, error code on failure
 * @note Reduces sample rate by 4x for zoom FFT processing
 */
errno_t DecimateBy4(DataBlock *data, ReceiveFilterConfig *RXfilters);

/**
 * @brief Decimate signal by factor of 2 with anti-aliasing filtering
 * @param data Pointer to DataBlock containing I/Q samples
 * @param RXfilters Pointer to receive filter configuration
 * @return ESUCCESS on success, error code on failure
 * @note Reduces sample rate by 2x for zoom FFT processing
 */
errno_t DecimateBy2(DataBlock *data, ReceiveFilterConfig *RXfilters);

/**
 * @brief Initialize a decimation filter structure
 * @param filter Pointer to DecimationFilter structure to initialize
 * @param DF Decimation factor (2, 4, or 8)
 * @param sampleRate_kHz Input sample rate in kHz
 * @param att_dB Stopband attenuation in dB
 * @param bandwidth_kHz Passband bandwidth in kHz
 * @param blockSize Number of samples per processing block
 * @note Calculates FIR coefficients for anti-aliasing filter
 */
void InitializeDecimationFilter(DecimationFilter *filter, float32_t DF, float32_t sampleRate_kHz,
                                float32_t att_dB, float32_t bandwidth_kHz, uint32_t blockSize);

/**
 * @brief Decimate float array by factor M (generic decimator)
 * @param in_buffer Input sample buffer
 * @param out_buffer Output decimated buffer
 * @param M Decimation factor
 * @param blockSize Number of input samples
 * @note Simple decimation without filtering - use only after anti-aliasing filter
 */
void decimate_f32(float32_t *in_buffer, float32_t *out_buffer, uint16_t M, uint32_t blockSize);

// Spectral Analysis

/**
 * @brief Reset Power Spectral Density accumulators
 * @note Clears spectrum averaging buffers for fresh spectrum display
 */
void ResetPSD(void);

// Equalization

/**
 * @brief Apply band equalizer to received or transmitted signal
 * @param data Pointer to DataBlock containing I/Q samples
 * @param RXfilters Pointer to receive filter configuration (contains EQ settings)
 * @param TXRX Indicates RX or TX processing path
 * @note Applies multi-band parametric EQ for audio shaping
 */
void BandEQ(DataBlock *data, ReceiveFilterConfig *RXfilters, TXRXType TXRX);

/**
 * @brief Apply single EQ band filter to signal
 * @param data Pointer to DataBlock containing I/Q samples
 * @param RXfilters Pointer to receive filter configuration
 * @param bf Band filter index (0-7)
 * @param TXRX Indicates RX or TX processing path
 * @note Implements one stage of multi-band parametric equalizer
 */
void ApplyEQBandFilter(DataBlock *data, ReceiveFilterConfig *RXfilters, uint8_t bf, TXRXType TXRX);

// Test Utilities

/**
 * @brief Get pointer to filtered output buffer for testing
 * @return Pointer to internal filtered data buffer
 * @note This function is intended for unit testing only
 */
float32_t * GetFilteredBufferAddress(void);

/**
 * @brief Set filename for FIR filter coefficient debugging
 * @param fnm Filename for debug output
 * @note This function is intended for unit testing only
 */
void setdspfirfilename(char *fnm);

// Transmit Filter Chain

/**
 * @brief Initialize transmit filter chain
 * @param TXfilters Pointer to transmit filter configuration
 * @note Sets up interpolation filters and Hilbert transform for SSB generation
 */
void InitializeTransmitFilters(TransmitFilterConfig *TXfilters);

/**
 * @brief Decimate transmit signal by factor of 4
 * @param data Pointer to DataBlock containing I/Q samples
 * @param TXfilters Pointer to transmit filter configuration
 * @note First stage of transmit sample rate reduction
 */
void TXDecimateBy4(DataBlock *data, TransmitFilterConfig *TXfilters);

/**
 * @brief Decimate transmit signal by factor of 2 (first stage)
 * @param data Pointer to DataBlock containing I/Q samples
 * @param TXfilters Pointer to transmit filter configuration
 * @note Second stage of transmit sample rate reduction
 */
void TXDecimateBy2(DataBlock *data, TransmitFilterConfig *TXfilters);

/**
 * @brief Decimate transmit signal by factor of 2 (second stage)
 * @param data Pointer to DataBlock containing I/Q samples
 * @param TXfilters Pointer to transmit filter configuration
 * @note Third stage of transmit sample rate reduction
 */
void TXDecimateBy2Again(DataBlock *data, TransmitFilterConfig *TXfilters);

/**
 * @brief Apply Hilbert transform to generate SSB signal
 * @param data Pointer to DataBlock containing I/Q samples
 * @param TXfilters Pointer to transmit filter configuration
 * @note Converts real audio signal to complex I/Q for SSB modulation
 */
void HilbertTransform(DataBlock *data, TransmitFilterConfig *TXfilters);

/**
 * @brief Interpolate transmit signal by factor of 2 (second stage)
 * @param data Pointer to DataBlock containing I/Q samples
 * @param TXfilters Pointer to transmit filter configuration
 * @note Third stage of transmit sample rate increase
 */
void TXInterpolateBy2Again(DataBlock *data, TransmitFilterConfig *TXfilters);

/**
 * @brief Interpolate transmit signal by factor of 2 (first stage)
 * @param data Pointer to DataBlock containing I/Q samples
 * @param TXfilters Pointer to transmit filter configuration
 * @note Second stage of transmit sample rate increase
 */
void TXInterpolateBy2(DataBlock *data, TransmitFilterConfig *TXfilters);

/**
 * @brief Interpolate transmit signal by factor of 4
 * @param data Pointer to DataBlock containing I/Q samples
 * @param TXfilters Pointer to transmit filter configuration
 * @note Final stage of transmit sample rate increase to DAC rate
 */
void TXInterpolateBy4(DataBlock *data, TransmitFilterConfig *TXfilters);

/**
 * @brief Select USB or LSB sideband for transmission
 * @param data Pointer to DataBlock containing I/Q samples
 * @note Implements sideband selection by conjugating I/Q signal for LSB
 */
void SidebandSelection(DataBlock *data);

// FFT Functions (Stubbed for Testing)

/**
 * @brief Perform 256-point forward FFT
 * @param buffer Pointer to interleaved I/Q data [I0,Q0,I1,Q1,...]
 * @note This function is stubbed in test builds for deterministic testing
 */
void FFT256Forward(float32_t *buffer);

/**
 * @brief Perform 256-point inverse FFT
 * @param buffer Pointer to interleaved I/Q data [I0,Q0,I1,Q1,...]
 * @note This function is stubbed in test builds for deterministic testing
 */
void FFT256Reverse(float32_t *buffer);

/**
 * @brief Perform 512-point forward FFT
 * @param buffer Pointer to interleaved I/Q data [I0,Q0,I1,Q1,...]
 * @note This function is stubbed in test builds for deterministic testing
 */
void FFT512Forward(float32_t *buffer);

/**
 * @brief Perform 512-point inverse FFT
 * @param buffer Pointer to interleaved I/Q data [I0,Q0,I1,Q1,...]
 * @note This function is stubbed in test builds for deterministic testing
 */
void FFT512Reverse(float32_t *buffer);

/**
 * @brief Write I/Q data to file for debugging
 * @param data Pointer to DataBlock containing I/Q samples
 * @param fname Filename for output file
 * @note This function is stubbed in test builds
 */
void WriteIQFile(DataBlock *data, const char* fname);

/**
 * @brief Write float array to file for debugging
 * @param data Pointer to float array
 * @param N Number of samples to write
 * @param fname Filename for output file
 * @note This function is stubbed in test builds
 */
void WriteFloatFile(float32_t *data, size_t N, const char* fname);

#endif // DSP_FFT_H
