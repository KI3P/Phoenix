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
#include "PowerCalSm.h" // PowerCalSm_EventId; SDT.h includes this header first

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

/**
 * @brief Force the total SSB transmit DSP gain, bypassing the band correction
 *        and the power setting
 * @param gain_dB Gain applied by TXGain(), in dB
 * @note Takes effect with the next transmit block. Not saved; set by the
 *       automatic power calibration to step the SSB tone's level.
 */
void SetSSBGainOverride(float32_t gain_dB);

/**
 * @brief Return the SSB transmit DSP gain to the band correction and power setting
 */
void ClearSSBGainOverride(void);

/**
 * @brief The forced SSB transmit DSP gain
 * @param gain_dB Set to the forced gain if there is one
 * @return true if an override is set
 */
bool GetSSBGainOverride(float32_t *gain_dB);

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
    bool keyed;               // the calibration wants the transmitter keyed
    bool ssbMode;             // the point uses the SSB tone (CALIBRATE_OFFSET), not CW
    float32_t drive_dB;       // drive of the current point: attenuation (CW) or DSP gain (SSB)
    uint32_t collectLoad_ms;      // collect time on the load
    uint32_t collectReflection_ms;// collect time on the open and short (keyed <= 2 s per point)
    uint32_t minReadings;     // a point with fewer bridge readings fails its band
    // Work posted by the actions for PowerCalAutoService()
    bool setDrive;            // set the drive for the current point
    bool clearStats;          // start a fresh set of bridge statistics
    bool storePoint;          // keep the readings of the point just collected
    bool fitForward;          // fit the band's forward line
    bool finishBand;          // finish the band on this standard
    bool fitAll;              // fit and check every band
    bool commit;              // store the bands that passed
    volatile bool servicePending; // work is waiting for PowerCalAutoService()
    // For the display
    float32_t lastVf_mV, lastVfSD_mV, lastVr_mV, lastVrSD_mV; // last point's readings
    uint32_t lastN;           // number of readings in the last point
    uint32_t resultsVersion;  // changes whenever a band's results change
};
extern AutoCalSession autoCal;

// Bits of AutoCalBandResult.failBits: the acceptance checks of
// swr_calibration_approach.md Section 8 that the band failed
#define AUTO_FAIL_FEW_READINGS   (1u << 0)  // a point had too few bridge readings
#define AUTO_FAIL_FORWARD_FIT    (1u << 1)  // no forward line, or level 3 fit rms/worst too large
#define AUTO_FAIL_FORWARD_SLOPE  (1u << 2)  // level 3 slope outside 22-28 mV/dB or 5 % of the median
#define AUTO_FAIL_DRIFT          (1u << 3)  // level 3 repeated point moved too far
#define AUTO_FAIL_LEVEL2_OFFSET  (1u << 4)  // level 2 offset too far from the level 1 default
#define AUTO_FAIL_OPEN           (1u << 5)  // open sweep abandoned or not fitted
#define AUTO_FAIL_REFLECTED_FIT  (1u << 6)  // reflected fit rms too large
#define AUTO_FAIL_REFLECTED_SLOPE (1u << 7) // reflected slope too far from the forward slope
#define AUTO_FAIL_GAP            (1u << 8)  // open-short intercept gap too large
#define AUTO_FAIL_S50            (1u << 9)  // the load's SWR too high
#define AUTO_FAIL_PA_CURVE       (1u << 10) // the PA curve or SSB gain could not be fitted

/** Results of one band in the automatic calibration */
struct AutoCalBandResult {
    bool inSession;           // the band is calibrated this session
    bool hasForward;          // forward line fitted
    float32_t m_f, b_f;       // forward line: slope (mV/dB) and intercept (dBm)
    float32_t forwardRms_dB, forwardWorst_dB; // level 3 fit residuals
    bool ceilingDropped;      // level 3: the 0 dB points were left out
    float32_t drift_dB;       // level 3: change of the repeated point
    float32_t level2Delta_dB; // level 2: offset minus the level 1 default
    bool hasPACurve;          // PA curve and SSB gain measured
    float32_t Psat_mW, kindex;// PA curve
    float32_t ceilingOverRead_dB; // level 2: detector at 0 dB minus the wattmeter
    float32_t ssbCorrection_dB; // SSB DSP gain correction
    bool hasReflected;        // reflected line fitted
    bool openOnly;            // reflected line from the open alone
    float32_t m_r, b_r;       // reflected line
    float32_t gap_dB;         // open intercept minus short intercept
    float32_t reflectedRms_dB;
    bool hasS50;              // S50 measured (needs the load stage)
    float32_t S50;            // SWR of the load
    bool checked;             // the acceptance checks have run
    uint32_t failBits;        // AUTO_FAIL_* bits; 0 means passed
};

/**
 * @brief Results of a band in the current automatic calibration session
 * @param band FIRST_BAND to LAST_BAND
 */
const AutoCalBandResult *GetAutoCalResult(int32_t band);

void PowerCalDispatch(PowerCalSm_EventId event);
bool PowerCalSmBusy(void);
void StartPowerAutoCal(void);
bool PowerCalAutoActive(void);
void PowerCalAutoService(void);
void ChangeAutoCalLevel(int32_t delta);
void ChangeAutoCalTurns(int32_t delta);
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

