#ifndef RFBOARD_H
#define RFBOARD_H
#include "SDT.h"

// This file lists the functions that are globally visible

// Attenuator control functions
errno_t TXAttenuatorCreate(float32_t txAttenuation_dB);
errno_t RXAttenuatorCreate(float32_t rxAttenuation_dB);
errno_t SetRXAttenuation(float32_t rxAttenuation_dB);
errno_t SetTXAttenuation(float32_t txAttenuation_dB);
float32_t GetRXAttenuation(void);
float32_t GetTXAttenuation(void);
errno_t InitAttenuation(void);

// SSB VFO Control Functions
void SetSSBVFOFrequency(int64_t frequency_dHz);
void SetSSBVFOPower(int32_t power);
void EnableSSBVFOOutput(void);
void DisableSSBVFOOutput(void);
errno_t InitSSBVFO(void);

// CW VFO Control Functions
void SetCWVFOFrequency(int64_t frequency_dHz); // frequency in Hz * 100
void SetCWVFOPower(int32_t power);
void EnableCWVFOOutput(void);
void DisableCWVFOOutput(void);
errno_t InitCWVFO(void);
void CWon(void);
void CWoff(void);
errno_t InitVFOs(void);


// Transmit Modulation Control
void SelectTXSSBModulation(void);
void SelectTXCWModulation(void);
errno_t InitTXModulation(void);

// Calibration Control
void EnableCalFeedback(void);
void DisableCalFeedback(void);
errno_t InitCalFeedbackControl(void);

// RXTX Control
void SelectTXMode(void);
void SelectRXMode(void);
errno_t InitRXTX(void);

void SetFreq(int64_t centerFreq_Hz);

// RF Board state machine functions
enum RFBoardState {
    RFBoardCWMark,
    RFBoardCWSpace,
    RFBoardReceive,
    RFBoardSSBTransmit,
    RFBoardCalIQ
};

ModeSm_StateId GetRFBoardPreviousState(void);
void UpdateRFBoardState(void);

#endif // RFBOARD_H