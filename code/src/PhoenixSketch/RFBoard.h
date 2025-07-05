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


#endif // RFBOARD_H