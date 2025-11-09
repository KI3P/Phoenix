#ifndef DSP_NOISE_H
#define DSP_NOISE_H
#include "SDT.h"

#define NR_FFT_L 256
#define ANR_DLINE_SIZE 512  //funktioniert nicht, 128 & 256 OK

/**
 * @brief Apply Kim1 noise reduction algorithm to audio
 * @param data Pointer to DataBlock containing demodulated audio
 * @note Implements spectral subtraction for noise suppression
 * @note Estimates noise floor and subtracts from signal in frequency domain
 */
void Kim1_NR(DataBlock *data);

/**
 * @brief Initialize Kim1 noise reduction subsystem
 * @note Allocates buffers and initializes noise floor estimation
 * @note Must be called before using Kim1_NR()
 */
void InitializeKim1NoiseReduction(void);

/**
 * @brief Apply adaptive notch filter (XANR) for interference rejection
 * @param data Pointer to DataBlock containing demodulated audio
 * @param ANR_notch Notch mode: 0=off, 1=automatic notch, 2=manual notch
 * @note Implements LMS adaptive filter to remove single-tone interference
 * @note Effective against heterodyne whistles and carrier leakage
 */
void Xanr(DataBlock *data, uint8_t ANR_notch);

/**
 * @brief Initialize XANR adaptive notch filter
 * @note Allocates delay line and initializes LMS filter coefficients
 * @note Must be called before using Xanr()
 */
void InitializeXanrNoiseReduction(void);

/**
 * @brief Initialize spectral noise reduction subsystem
 * @note Allocates FFT buffers and initializes spectral analysis
 * @note Must be called before using SpectralNoiseReduction()
 */
void InitializeSpectralNoiseReduction(void);

/**
 * @brief Apply spectral noise reduction to audio
 * @param data Pointer to DataBlock containing demodulated audio
 * @note Implements frequency-domain noise reduction
 * @note Analyzes spectrum to distinguish signal from noise
 * @note More sophisticated than simple spectral subtraction
 */
void SpectralNoiseReduction(DataBlock *data);

#endif // DSP_NOISE_H
