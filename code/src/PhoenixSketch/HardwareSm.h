/* 
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the 
terms of the GNU General Public License as published by the Free Software 
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix. 
If not, see <https://www.gnu.org/licenses/>.
*/

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
void UpdateRXDeltaVal(void);
float32_t GetRXDeltaVals(int32_t band);
void InitializeTXIQCalibration(void);
void InitializeTXCarrierCalibration(void);
float32_t GetTXDeltaVals(int32_t band);
float32_t GetTXCarrierVals(int32_t band);
void SetTXIQCurrentBand(int32_t band);

#endif //HARDWARESM_H

