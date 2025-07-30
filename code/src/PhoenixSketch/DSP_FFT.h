#ifndef FFT_H
#define FFT_H
#include "SDT.h"

float32_t log10f_fast(float32_t X);
void CalcPSD512(float32_t *real, float32_t *imag);
void FreqShiftFs4(DataBlock *data);
void FreqShiftF(DataBlock *data, float32_t freqShift_Hz);
void FreqShiftF2(float32_t *I, float32_t *Q, uint32_t blocksize, 
                float32_t freqShift_Hz, uint32_t sampleRate_Hz);
bool ZoomFFTExe(DataBlock *data, uint32_t spectrum_zoom, FilterConfig *filters);
void CalcCplxFIRCoeffs(float * coeffs_I, float * coeffs_Q, int numCoeffs, 
                    float32_t FLoCut, float32_t FHiCut, float SampleRate);
void CalcFIRCoeffs(float *coeffs_I, int numCoeffs, float32_t fc_Hz, float32_t Astop_dB, 
                  FilterType type, float dfc_Hz, float Fsamprate_Hz);
void SetIIRCoeffs(float32_t *coefficient_set, float32_t f0, float32_t Q, 
                    float32_t sample_rate, FilterType filter_type);
void UpdateFIRFilterMask(FilterConfig *filters);
void InitializeFilters(uint32_t spectrum_zoom, FilterConfig *filters);
void ZoomFFTPrep(uint32_t spectrum_zoom, FilterConfig *filters);
errno_t DecimateBy8(DataBlock *data, FilterConfig *filters);
errno_t DecimateBy4(DataBlock *data, FilterConfig *filters);
errno_t DecimateBy2(DataBlock *data, FilterConfig *filters);
void InitializeDecimationFilter(DecimationFilter *filter, float32_t DF, float32_t sampleRate_kHz, 
                                float32_t att_dB, float32_t bandwidth_kHz, uint32_t blockSize);
void decimate_f32(float32_t *in_buffer, float32_t *out_buffer, uint16_t M, uint32_t blockSize);
void ResetPSD(void);
void InitFilterMask(float32_t *FIR_filter_mask, FilterConfig *filters);
errno_t ConvolutionFilter(DataBlock *data, FilterConfig *filters, const char *fname);
void BandEQ(DataBlock *data, FilterConfig *filters, TXRXType TXRX);
//void ApplyEQBandFilter(DataBlock *data, FilterConfig *filters, uint8_t bf);
void ApplyEQBandFilter(DataBlock *data, FilterConfig *filters, uint8_t bf, TXRXType TXRX);

#ifdef TESTMODE
void CalcPSD256(float32_t *I, float32_t *Q);
float32_t * GetFilteredBufferAddress(void);
#endif


void TXDecInit(void);
void TXDecimateBy4(float32_t *I,float32_t *Q);
void TXDecimateBy2(float32_t *I,float32_t *Q);
void DoExciterEQ(float32_t *float_buffer_L_EX);
void TXDecimateBy2Again(float32_t *I,float32_t *Q);
void HilbertTransform(float32_t *I,float32_t *Q);
void TXInterpolateBy2Again(float32_t *I,float32_t *Q, float32_t *Iout,float32_t *Qout);
void TXInterpolateBy2(float32_t *I,float32_t *Q, float32_t *Iout,float32_t *Qout);
void TXInterpolateBy4(float32_t *I,float32_t *Q, float32_t *Iout,float32_t *Qout);
void SidebandSelection(float32_t *I, float32_t *Q);

#endif // FFT_H