#ifndef RFBOARD_H
#define RFBOARD_H
#include "SDT.h"

//#include <stdint.h>
#ifndef errno_t
typedef int errno_t;
#endif

// This file lists the functions that are globally visible
errno_t RXAttenuatorCreate(float rxAttenuation_dB);
errno_t TXAttenuatorCreate(float txAttenuation_dB);
errno_t SetRXAttenuation(float rxAttenuation_dB);
errno_t SetTXAttenuation(float txAttenuation_dB);
float GetRXAttenuation();
float GetTXAttenuation();
errno_t SetFreq(int64_t centerFreq_Hz);

// Transceiver mode selection
void InitStateSelect(void);
void SelectSSBState(void);
void SelectCWState(void);
void SelectCalState(void);

// VFOs
void InitVFOs(void);
void SetVFO1Frequency(int64_t freq_Hz);
void SetVFO1Power(int32_t power_dBm);
void DisableVFO1Output(void);
void EnableVFO1Output(void);
void SetVFO2Frequency(int64_t freq_Hz);
void SetVFO2Power(int32_t power_dBm);
void DisableVFO2Output(void);
void EnableVFO2Output(void);

ModeSm_StateId GetRFBoardPreviousState(void);
void UpdateRFBoardState(void);

#endif // RFBOARD_H