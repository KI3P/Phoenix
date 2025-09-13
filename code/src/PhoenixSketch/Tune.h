#ifndef TUNE_H
#define TUNE_H

enum TuneState {
    TuneReceive,
    TuneSSBTX,
    TuneCWTX
};

int64_t GetTXRXFreq_dHz(void);
int64_t GetCWTXFreq_dHz(void);
int8_t GetBand(int64_t freq);
void ChangeTune(void);
void ChangeBand(void);
void ChangeVFO(void);
void AdjustFineTune(int32_t filter_change);
void UpdateTuneState(void);
void HandleTuneState(void);

#endif // TUNE_H