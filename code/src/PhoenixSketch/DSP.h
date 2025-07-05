#ifndef SIGNALPROCESSING_H
#define SIGNALPROCESSING_H
#include "SDT.h"

#ifdef TESTMODE
//float32_t * GetLBufferAddress(void);
//float32_t * GetRBufferAddress(void);
float32_t GetAmpCorrectionFactor(uint32_t bandN);
float32_t GetPhaseCorrectionFactor(uint32_t bandN);
#endif
void PerformSignalProcessing(void);
DataBlock * ReceiveProcessing(const char *fname);
errno_t ReadIQInputBuffer(DataBlock *data);
void ApplyRFGain(DataBlock *data, float32_t rfGainAllBands_dB, float32_t bandGain_dB);
void ApplyIQCorrection(DataBlock *data, float32_t amp_factor, float32_t phs_factor);
void InitializeAGC(AGCConfig *a, uint32_t sampleRate_Hz);
void AGC(DataBlock *data, AGCConfig *a);
void InitializeSignalProcessing(void);
void Demodulate(DataBlock *data, FilterConfig *filters);
void NoiseReduction(DataBlock *data);
void InterpolateReceiveData(DataBlock *data, FilterConfig *filters);
float32_t VolumeToAmplification(int32_t volume);
void AdjustVolume(DataBlock *data, FilterConfig *filters);
void PlayBuffer(DataBlock *data);

#endif // SIGNALPROCESSING_H