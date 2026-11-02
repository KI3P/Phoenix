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

/**
 * @brief Force the CW transmit attenuation, bypassing the power-curve model
 * @param att_dB 0 to 31.5 dB, or a negative value to return to the model
 * @note Takes effect at the next key-down. The PA selection still follows the
 *       CW power setting. Not saved; set by the CAT XA command for bench work.
 */
void SetCWAttenuationOverride(float32_t att_dB);

/**
 * @brief The forced CW transmit attenuation, or a negative value if none is set
 */
float32_t GetCWAttenuationOverride(void);

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

// Automatic power and SWR bridge calibration (the AUTO branch of PowerCalSm).
// See code/docs/PowerCalibration/AutoCalibration.md
enum AutoCalStage {
    AUTO_STAGE_LOAD = 0,   // 50 Ohm load, with the wattmeter at levels 2 and 3
    AUTO_STAGE_OPEN,       // nothing on the antenna connector
    AUTO_STAGE_SHORT,      // a short directly on the connector
};

struct AutoCalSession {
    bool active;              // the state machine is in AUTO
    uint8_t level;            // calibration level, 1 to 3
    uint32_t bandMask;        // bit k set: calibrate band k this session
    uint32_t failedMask;      // bit k set: band k failed its checks
    uint32_t openFailMask;    // bit k set: band k's open sweep was abandoned
    uint32_t shortFailMask;   // bit k set: band k's short sweep was abandoned
    AutoCalStage stage;       // standard on the connector
    bool loadSkipped;         // level 1 only: the load stage was skipped
    int32_t band;             // band being measured
    uint32_t point;           // index of the next point in this band and stage
    uint32_t nPoints;         // CW points in this band and stage
    bool forwardFitDone;      // load stage: forward line fitted for this band
    bool ssbPoint;            // the current point is the SSB tone point
    bool oscillation;         // the last point failed the oscillation check
    bool keyed;               // the calibration has the transmitter keyed
    volatile bool servicePending; // work is waiting for PowerCalAutoService()
};
extern AutoCalSession autoCal;

void StartPowerAutoCal(void);
bool PowerCalAutoActive(void);
void PowerCalAutoService(void);
void ChangeAutoCalLevel(int32_t delta);
void SkipPowerCalStage(void);
void RetryPowerCal(void);

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

