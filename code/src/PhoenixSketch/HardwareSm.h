#ifndef HARDWARESM_H
#define HARDWARESM_H
#include "SDT.h"

// RF Board state machine states
enum RFHardwareState {
    RFCWMark,
    RFCWSpace,
    RFReceive,
    RFTransmit,
    RFCalReceiveIQ,
    RFCalTransmitIQ,
    RFCalTransmitIQSingleVFO,
    RFInvalid
};

enum TuneState {
    TuneReceive,
    TuneSSBTX,
    TuneCWTX,
    TuneCalReceiveIQ,
    TuneCalTransmitIQ,
    TuneInvalid
};

ModeSm_StateId GetRFHardwarePreviousState(void);
void UpdateRFHardwareState(void);
void ForceUpdateRFHardwareState(void);
void HandleRFHardwareStateChange(RFHardwareState newState);
errno_t InitializeRFHardware(void);
void UpdateTuneState(void);
RFHardwareState GetRFHardwareState(void);

// Power calibration routines
float32_t GetPowDataSum(void);
uint32_t GetNpoints(void);
float32_t GetAttenuation_dB(uint32_t k);
float32_t GetPower_W(uint32_t k);
float32_t GetSSBPower_W(void);
float32_t GetMeasuredPower(void);
void SetMeasuredPower(float32_t newPower);
float32_t GetTargetPower(void);
uint8_t GetPowerUnit(void);
void InitializePowerCalibration(void);

// Receive IQ calibration routines
void InitializeRXIQCalibration(void);
void ResetRXIQCalBand(void);
void AdjustRXIQBand(void);
void ResetRXIQCalSettings(void);
void AdjustRXIQCalSetting(void);
void ReadRXIQDelta(void);
void UpdateDeltaVal(void);
float32_t GetDeltaVals(int32_t band);

#endif //HARDWARESM_H

