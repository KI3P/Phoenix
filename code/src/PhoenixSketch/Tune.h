#ifndef TUNE_H
#define TUNE_H

int64_t GetTXRXFreq_dHz(void);
int8_t GetBand(uint64_t freq);
void ReceiveTune(void);
void ChangeBand(void);
void ChangeVFO(void);
void AdjustFineTune(int32_t filter_change);

#endif // TUNE_H