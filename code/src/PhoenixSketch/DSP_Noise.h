#ifndef NOISE_H
#define NOISE_H
#include "SDT.h"

#define NR_FFT_L 256
#define ANR_DLINE_SIZE 512  //funktioniert nicht, 128 & 256 OK

void Kim1_NR(DataBlock *data);
void InitializeKim1NoiseReduction(void);
void Xanr(DataBlock *data, uint8_t ANR_notch);
void InitializeXanrNoiseReduction(void);
void InitializeSpectralNoiseReduction(void);
void SpectralNoiseReduction(DataBlock *data);

#endif // NOISE_H