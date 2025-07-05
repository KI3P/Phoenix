#ifndef CWPROCESSING_H
#define CWPROCESSING_H
#include "SDT.h"

#define DECODER_BUFFER_SIZE 128  // Max chars in binary search string with , . ?
#define LOWEST_ATOM_TIME 20      // 60WPM has an atom of 20ms
#define HISTOGRAM_ELEMENTS 750
#define ADAPTIVE_SCALE_FACTOR 0.8                             // The amount of old histogram values are presesrved
#define SCALE_CONSTANT (1.0 / (1.0 - ADAPTIVE_SCALE_FACTOR))  // Insure array has enough observations to scale

float32_t * InitializeCWProcessing(uint32_t wpm, FilterConfig *filters);
void DoCWReceiveProcessing(DataBlock *data, FilterConfig *filters);
float32_t goertzel_mag(uint32_t numSamples, int32_t TARGET_FREQUENCY, uint32_t SAMPLING_RATE, float32_t *data);
void DoCWDecoding(uint8_t audioValue);
void DoGapHistogram(int64_t gapLen);
void SetDitLength(uint32_t wpm);
void JackClusteredArrayMax(int32_t *array, int32_t elements, int32_t *maxCount, int32_t *maxIndex, int32_t *firstNonZero, int32_t spread);
void DoSignalHistogram(int64_t val);
void ResetHistograms();
void CWAudioFilter(DataBlock *data, FilterConfig *filters);

#endif // CWPROCESSING_H