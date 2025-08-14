#ifndef RFBOARD_H
#define RFBOARD_H

#include <stdint.h>
#ifndef errno_t
typedef int errno_t;
#endif

// This file lists the functions that are globally visible

/*errno_t RXAttenuatorCreate(int32_t rxAttenuation_dBx2);
errno_t TXAttenuatorCreate(int32_t txAttenuation_dBx2);
errno_t SetRXAttenuator(int32_t rxAttenuation_dBx2);
errno_t SetTXAttenuator(int32_t txAttenuation_dBx2);
int32_t GetRXAttenuator();
int32_t GetTXAttenuator();*/

errno_t RXAttenuatorCreate(float rxAttenuation_dB);
errno_t TXAttenuatorCreate(float txAttenuation_dB);
errno_t SetRXAttenuator(float rxAttenuation_dB);
errno_t SetTXAttenuator(float txAttenuation_dB);
float GetRXAttenuator();
float GetTXAttenuator();
errno_t SetFreq(int64_t centerFreq_Hz);

// Transceiver mode selection
void InitStateSelect(void);
void SelectSSBState(void);
void SelectCWState(void);
void SelectCalState(void);

// VFOs
void InitVFOs(void);
void SetVFO1Frequency(int32_t freq_Hz);
void SetVFO1Power(int32_t power_dBm);
void DisableVFO1Output(void);
void EnableVFO1Output(void);
void SetVFO2Frequency(int32_t freq_Hz);
void SetVFO2Power(int32_t power_dBm);
void DisableVFO2Output(void);
void EnableVFO2Output(void);

#endif // RFBOARD_H