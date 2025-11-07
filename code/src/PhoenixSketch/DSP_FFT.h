#ifndef FFT_H
#define FFT_H
#include "SDT.h"

float32_t GetAudioPowerMax(void);
float32_t log10f_fast(float32_t X);
void CalcPSD512(float32_t *real, float32_t *imag);
void FreqShiftFs4(DataBlock *data);
void FreqShiftF(DataBlock *data, float32_t freqShift_Hz);
void FreqShiftF2(float32_t *I, float32_t *Q, uint32_t blocksize, 
                float32_t freqShift_Hz, uint32_t sampleRate_Hz);
bool ZoomFFTExe(DataBlock *data, uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters);
void CalcCplxFIRCoeffs(float * coeffs_I, float * coeffs_Q, int numCoeffs, 
                    float32_t FLoCut, float32_t FHiCut, float SampleRate);
void CalcFIRCoeffs(float *coeffs_I, int numCoeffs, float32_t fc_Hz, float32_t Astop_dB, 
                  FilterType type, float dfc_Hz, float Fsamprate_Hz);
void SetIIRCoeffs(float32_t *coefficient_set, float32_t f0, float32_t Q, 
                    float32_t sample_rate, FilterType filter_type);
void UpdateFIRFilterMask(ReceiveFilterConfig *RXfilters);
void InitializeFilters(uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters);
void FilterSetSSB(int32_t filter_change, uint8_t changeFilterHiCut);
void ZoomFFTPrep(uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters);
errno_t DecimateBy8(DataBlock *data, ReceiveFilterConfig *RXfilters);
errno_t DecimateBy4(DataBlock *data, ReceiveFilterConfig *RXfilters);
errno_t DecimateBy2(DataBlock *data, ReceiveFilterConfig *RXfilters);
void InitializeDecimationFilter(DecimationFilter *filter, float32_t DF, float32_t sampleRate_kHz, 
                                float32_t att_dB, float32_t bandwidth_kHz, uint32_t blockSize);
void decimate_f32(float32_t *in_buffer, float32_t *out_buffer, uint16_t M, uint32_t blockSize);
void ResetPSD(void);
void InitFilterMask(float32_t *FIR_filter_mask, ReceiveFilterConfig *RXfilters);
errno_t ConvolutionFilter(DataBlock *data, ReceiveFilterConfig *RXfilters, const char *fname);
void BandEQ(DataBlock *data, ReceiveFilterConfig *RXfilters, TXRXType TXRX);
void ApplyEQBandFilter(DataBlock *data, ReceiveFilterConfig *RXfilters, uint8_t bf, TXRXType TXRX);

// used by unit tests
void CalcPSD256(float32_t *I, float32_t *Q);
float32_t * GetFilteredBufferAddress(void);
void setdspfirfilename(char *fnm);

void InitializeTransmitFilters(TransmitFilterConfig *TXfilters);
void TXDecimateBy4(DataBlock *data, TransmitFilterConfig *TXfilters);
void TXDecimateBy2(DataBlock *data, TransmitFilterConfig *TXfilters);
void TXDecimateBy2Again(DataBlock *data, TransmitFilterConfig *TXfilters);
void HilbertTransform(DataBlock *data, TransmitFilterConfig *TXfilters);
void TXInterpolateBy2Again(DataBlock *data, TransmitFilterConfig *TXfilters);
void TXInterpolateBy2(DataBlock *data, TransmitFilterConfig *TXfilters);
void TXInterpolateBy4(DataBlock *data, TransmitFilterConfig *TXfilters);
void SidebandSelection(DataBlock *data);

// The functions that are placed in a stub to enable unit test substitution
void FFT256Forward(float32_t *buffer);
void FFT256Reverse(float32_t *buffer);
void FFT512Forward(float32_t *buffer);
void FFT512Reverse(float32_t *buffer);
void WriteIQFile(DataBlock *data, const char* fname);
void WriteFloatFile(float32_t *data, size_t N, const char* fname);

#endif // FFT_H