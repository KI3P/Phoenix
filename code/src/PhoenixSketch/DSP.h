#ifndef DSP_H
#define DSP_H
#include "SDT.h"

// Test Utilities

/**
 * @brief Get amplitude correction factor for I/Q imbalance compensation
 * @param bandN Band number (0-10)
 * @return Amplitude correction factor for specified band
 * @note This function is intended for unit testing only
 */
float32_t GetAmpCorrectionFactor(uint32_t bandN);

/**
 * @brief Get phase correction factor for I/Q imbalance compensation
 * @param bandN Band number (0-10)
 * @return Phase correction factor for specified band in radians
 * @note This function is intended for unit testing only
 */
float32_t GetPhaseCorrectionFactor(uint32_t bandN);

/**
 * @brief Set filename for DSP debugging output
 * @param fnm Filename for debug file
 * @note This function is intended for unit testing only
 */
void setfilename(char *fnm);

// Main Signal Processing

/**
 * @brief Perform complete signal processing chain (RX or TX)
 * @note Main DSP entry point called from main loop
 * @note Routes to either ReceiveProcessing() or TransmitProcessing() based on radio state
 */
void PerformSignalProcessing(void);

/**
 * @brief Execute receive signal processing chain
 * @param fname Optional filename for debugging (can be NULL)
 * @return Pointer to processed DataBlock ready for audio output
 * @note Complete RX chain: read I/Q → AGC → filter → demodulate → noise reduction → audio out
 */
DataBlock * ReceiveProcessing(const char *fname);

/**
 * @brief Execute transmit signal processing chain
 * @param fname Optional filename for debugging (can be NULL)
 * @return Pointer to processed DataBlock ready for RF output
 * @note Complete TX chain: read audio → filter → modulate → I/Q correction → RF out
 */
DataBlock * TransmitProcessing(const char *fname);

/**
 * @brief Read I/Q samples from ADC input buffer
 * @param data Pointer to DataBlock to fill with I/Q samples
 * @return ESUCCESS on success, error code on buffer overflow
 * @note Interfaces with OpenAudio library to retrieve digitized RF samples
 */
errno_t ReadIQInputBuffer(DataBlock *data);

// RF Gain and Calibration

/**
 * @brief Apply RF gain to received signal
 * @param data Pointer to DataBlock containing I/Q samples
 * @param rfGainAllBands_dB Global RF gain applied to all bands in dB
 * @param bandGain_dB Band-specific gain correction in dB
 * @note Compensates for frontend gain variations and user RF gain setting
 */
void ApplyRFGain(DataBlock *data, float32_t rfGainAllBands_dB, float32_t bandGain_dB);

/**
 * @brief Apply I/Q imbalance correction
 * @param data Pointer to DataBlock containing I/Q samples
 * @param amp_factor Amplitude imbalance correction factor
 * @param phs_factor Phase imbalance correction factor in radians
 * @note Corrects for quadrature mixer imperfections causing image rejection issues
 */
void ApplyIQCorrection(DataBlock *data, float32_t amp_factor, float32_t phs_factor);

// Automatic Gain Control

/**
 * @brief Initialize AGC (Automatic Gain Control) configuration
 * @param a Pointer to AGCConfig structure to initialize
 * @param sampleRate_Hz Audio sample rate in Hz
 * @note Configures attack/decay times and gain limits for AGC algorithm
 */
void InitializeAGC(AGCConfig *a, uint32_t sampleRate_Hz);

/**
 * @brief Apply AGC to received signal
 * @param data Pointer to DataBlock containing demodulated audio
 * @param a Pointer to AGC configuration
 * @note Dynamically adjusts audio gain to maintain constant output level
 * @note Implements fast attack, slow decay for natural sound
 */
void AGC(DataBlock *data, AGCConfig *a);

// Initialization

/**
 * @brief Initialize all signal processing subsystems
 * @note Configures filters, AGC, noise reduction, and demodulators
 * @note Called once at system startup
 */
void InitializeSignalProcessing(void);

// Demodulation

/**
 * @brief Demodulate I/Q signal to audio
 * @param data Pointer to DataBlock containing filtered I/Q samples
 * @param RXfilters Pointer to receive filter configuration
 * @note Extracts audio from SSB or CW signal using phasing method
 * @note Implements USB/LSB selection and optional synchronous AM detection
 */
void Demodulate(DataBlock *data, ReceiveFilterConfig *RXfilters);

float32_t GetSAMCarrierOffset(void);

/**
 * @brief Apply noise reduction to received audio
 * @param data Pointer to DataBlock containing demodulated audio
 * @note Implements spectral subtraction or adaptive filtering for noise suppression
 * @note Algorithm selection based on user configuration
 */
void NoiseReduction(DataBlock *data);

/**
 * @brief Interpolate received audio to output sample rate
 * @param data Pointer to DataBlock containing processed audio
 * @param RXfilters Pointer to receive filter configuration
 * @note Converts from DSP sample rate to DAC output rate
 * @note Uses polyphase interpolation filter for high quality
 */
void InterpolateReceiveData(DataBlock *data, ReceiveFilterConfig *RXfilters);

// Volume Control

/**
 * @brief Convert volume setting to linear amplification factor
 * @param volume Volume control setting (0-100)
 * @return Linear amplification factor
 * @note Implements logarithmic volume curve for natural perceived loudness
 */
float32_t VolumeToAmplification(int32_t volume);

/**
 * @brief Adjust audio output volume
 * @param data Pointer to DataBlock containing audio samples
 * @param RXfilters Pointer to receive filter configuration (contains volume setting)
 * @note Applies volume scaling to final audio output
 */
void AdjustVolume(DataBlock *data, ReceiveFilterConfig *RXfilters);

/**
 * @brief Send processed audio to output buffer for playback
 * @param data Pointer to DataBlock containing final audio samples
 * @note Interfaces with OpenAudio library to send samples to DAC
 */
void PlayBuffer(DataBlock *data);

#endif // DSP_H
