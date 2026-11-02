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

/**
 * The power calibration routine uses its own state machine to handle stepping through 
 * the process. The state machine itself is described by the StateSmith UML diagram
 * in PowerCalSm.drawio and the generated source code files PowerCalSm.cpp/h. This file
 * contains the functions used by the state machine to ensure clean separation between
 * the graphical code in MainBoard_DisplayCalibration_Power.cpp and the rest of the code
 */

#include "SDT.h"

PowerCalSm powerSM;

float32_t attenuations_dB[3];
float32_t powers_W[3];
static float32_t Pf_W[3];
static float32_t Pr_W[3];
static float32_t SWR[3];
uint32_t Npoints = 0;
float32_t measuredPower = 0.0;
float32_t targetPower = 0.0;
static float32_t powr_W; // SSB power point
extern float32_t TXgainDSP; // DSP gain applied to the last transmitted block (DSP.cpp)
uint8_t powerUnit = 1; // 1=W, 0=dBm

/**
 * @brief Get the current measured power value during calibration
 * @return Measured power in W (if powerUnit==1) or dBm (if powerUnit==0)
 */
float32_t GetMeasuredPower(void){
    return measuredPower;
}

/**
 * @brief Get the target power value during calibration
 * @return Target power in W (if powerUnit==1) or dBm (if powerUnit==0)
 */
float32_t GetTargetPower(void){
    return targetPower;
}

/**
 * @brief Get the current power unit setting
 * @return 1 for Watts, 0 for dBm
 */
uint8_t GetPowerUnit(void){
    return powerUnit;
}

/**
 * @brief Set the measured power value during calibration
 * @param newPower New power value in W (if powerUnit==1) or dBm (if powerUnit==0)
 */
void SetMeasuredPower(float32_t newPower){
    measuredPower = newPower;
}

/**
 * @brief Calculate sum of all power data points for change detection
 * @return Sum of absolute values of measured power and all recorded power points
 * @note Used by display code to detect when power data has changed
 */
float32_t GetPowDataSum(void){
    float32_t powsum = abs(measuredPower);
    for (size_t k=0; k<Npoints; k++){
        powsum += abs(powers_W[k]);
    }
    return powsum;
}

/**
 * @brief Get the number of power calibration data points recorded
 * @return Number of recorded points (0-3)
 */
uint32_t GetNpoints(void){
    return Npoints;
}

/**
 * @brief Get the attenuation value for a specific calibration data point
 * @param k Data point index (0-2)
 * @return Attenuation in dB for the specified point
 */
float32_t GetAttenuation_dB(uint32_t k){
    return attenuations_dB[k];
}

/**
 * @brief Get the power value for a specific calibration data point
 * @param k Data point index (0-2)
 * @return Power in Watts for the specified point
 */
float32_t GetPower_W(uint32_t k){
    return powers_W[k];
}

/**
 * @brief Get the SSB power calibration point measurement
 * @return SSB power in Watts
 */
float32_t GetSSBPower_W(void){
    return powr_W;
}


/**
 * @brief Increase transmit attenuation during power calibration
 * @note Increments by 0.5 dB, clamped to maximum 31.5 dB
 * @note Modifies ED.XAttenCW for current band
 * @note Called when user rotates volume encoder clockwise in power cal mode
 */
void IncrementCalibrationTransmitAtt(void){
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    ED.XAttenCW[currentBand] += 0.5;
    if (ED.XAttenCW[currentBand] > 31.5)
        ED.XAttenCW[currentBand] = 31.5;
    SetTXAttenuation(ED.XAttenCW[currentBand]);
}

/**
 * @brief Decrease transmit attenuation during power calibration
 * @note Decrements by 0.5 dB, clamped to minimum 0.0 dB
 * @note Modifies ED.XAttenCW for current band
 * @note Called when user rotates volume encoder counter-clockwise in power cal mode
 */
void DecrementCalibrationTransmitAtt(void){
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    ED.XAttenCW[currentBand] -= 0.5;
    if (ED.XAttenCW[currentBand] < 0.0)
        ED.XAttenCW[currentBand] = 0.0;
    SetTXAttenuation(ED.XAttenCW[currentBand]);
}

uint8_t opu = 5;
/**
 * @brief Reset power target to calibration power point based on PA selection
 * @note Sets measuredPower and targetPower to CW calibration point
 * @note Uses CW_100W_CAL_POWER_POINT_W or CW_20W_CAL_POWER_POINT_W depending on PA100Wactive
 * @note Automatically converts to dBm if powerUnit==0
 */
void ResetPowerTarget(void){
    // Update the power units if they have changed
    if (ED.PA100Wactive){
        if (powerUnit)
            measuredPower = CW_100W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(CW_100W_CAL_POWER_POINT_W*1000); // dBm units
    }else{
        if (powerUnit)
            measuredPower = CW_20W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(CW_20W_CAL_POWER_POINT_W*1000); // dBm units
    }
    targetPower = measuredPower;
}

/**
 * @brief Toggle power units between Watts and dBm
 * @note Converts targetPower and measuredPower values between W and dBm
 * @note Called when user presses button 14
 */
void ChangePowerUnits(void){
    if (powerUnit){
        powerUnit = 0;
        targetPower = 10*log10f(targetPower*1000.0f);
        measuredPower = 10*log10f(measuredPower*1000.0f);
    } else {
        powerUnit = 1;
        targetPower = pow(10.0f,targetPower/10.0f)/1000.0f;
        measuredPower = pow(10.0f,measuredPower/10.0f)/1000.0f;
    }
}

/**
 * @brief Toggle PA selection between 20W and 100W during calibration
 * @note Updates measuredPower and targetPower based on new PA selection
 * @note Forces hardware state update and resets power calibration state machine
 * @note Called by Loop.cpp when button 16 is pressed
 */
void ChangeCalibrationPASelection(void){
    if (ED.PA100Wactive){
        if (powerUnit)
            measuredPower = CW_20W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(CW_20W_CAL_POWER_POINT_W*1000); // dBm units
        targetPower = measuredPower;
        ED.PA100Wactive = false;
    }else{
        if (powerUnit)
            measuredPower = CW_100W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(CW_100W_CAL_POWER_POINT_W*1000); // dBm units
        targetPower = measuredPower;
        ED.PA100Wactive = true;
    }
    // Force hardware update even though we're staying in the same calibration state
    // This ensures the PA selection gets applied to the hardware
    ForceUpdateRFHardwareState();
    // Force us back to start state
    PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_RESET);
}

/**
 * @brief Calculate power curve fit from recorded calibration data points
 * @note Automatically called upon entry to PowerCalSm_StateId_POWERCOMPLETE state
 * @note Uses attenuations_dB and powers_W arrays populated by prior measurement steps
 * @note Stores fitted Psat and kindex parameters in ED structure for current band
 */
void CalculatePowerCurveFit(void){
    Debug("Power curve fit");
    float32_t powers_mW[3];
    for (size_t k=0; k<3; k++){
        powers_mW[k] = powers_W[k]*1000.0f;
    }
    struct FitResult f;
    if (ED.PA100Wactive){
        f = FitPowerCurve(attenuations_dB, powers_mW, Npoints, 75000.0f,10.0f);
        ED.PowerCal_100W_Psat_mW[ED.currentBand[ED.activeVFO]] = f.P_sat;
        ED.PowerCal_100W_kindex[ED.currentBand[ED.activeVFO]] = f.k;
    } else {
        f = FitPowerCurve(attenuations_dB, powers_mW, Npoints, 15000.0f,6.0f);
        ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]] = f.P_sat;
        ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]] = f.k;
    }
    //Serial.println("| P_meas [W] | P_f [W] | P_r [W] | SWR |");
    //Serial.println("|------------|---------|---------|-----|");
    //for (size_t k=0; k<3; k++){
    //    sprintf(buff,"| %5.4f | %5.4f | %5.4f | %5.4f |",
    //                powers_W[k],Pf_W[k],Pr_W[k],SWR[k]);
    //    Serial.println(buff);
    //}
}

/**
 * @brief Initialize power calibration state machine and reset all calibration parameters
 * @note Starts PowerCalSm state machine in the manual flow
 * @note Resets all band attenuations and SWR adjustments to zero
 * @note Updates hardware state to apply settings
 */
void InitializePowerCalibration(void){
    // PowerCalSm_start() does not run AUTO's exit action, so clear it here
    autoCal.active = false;
    autoCal.keyed = false;
    autoCal.servicePending = false;
    PowerCalSm_start(&powerSM);
    // Make all the attenuations zero so we start off right
    // Make the SWR offset adjustments zero as well
    for (size_t k = FIRST_BAND; k<=LAST_BAND; k++){
        ED.XAttenCW[k] = 0.0;
        ED.SWR_F_SlopeAdj[k] = 0.0;
        ED.SWR_R_SlopeAdj[k] = 0.0;
        ED.SWR_F_Offset[k] = 0.0;
        ED.SWR_R_Offset[k] = 0.0;
    }
    SetTXAttenuation(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);
    UpdateRFHardwareState();
}


/**
 * Called by Loop.cpp when button 12 is pressed. Change from CALIBRATE_POWER measurement
 * to CALIBRATE_OFFSET measurement when curve fit is complete. If we are not in the
 * POWERCOMPLETE state, ignore this button press.
 */
void ChangePowerCalibrationPhase(void){
    if (powerSM.state_id == PowerCalSm_StateId_POWERCOMPLETE){
        // Issue the event that causes state to change to SSBPOINT measurement
        PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_CURVE_COMPLETE);
        // Change to offset measurement mode
        ModeSm_dispatch_event(&modeSM,ModeSm_EventId_OFFSET_START);
        UpdateRFHardwareState();
    }
}

// ---------- Automatic power and SWR bridge calibration ----------
//
// The AUTO branch of PowerCalSm calibrates the PA power curve, the SSB gain and
// the SWR bridge on every band in one session. The operator fits each standard
// once: the 50 Ohm load (with the wattmeter at levels 2 and 3), then the open,
// then the short. See code/docs/PowerCalibration/AutoCalibration.md and
// code/docs/swr_calibration_approach.md.
//
// The functions below are the state machine's actions and guards. They run in
// the 1 ms timer interrupt, so anything that needs I2C, a long calculation or
// the storage is posted as a request (RequestAutoService) and done by
// PowerCalAutoService() from the main loop. The state machine waits for it
// with the AutoServiceIdle() guard.
//
// For now these only step through the session. Keying, the measurements, the
// fits, the checks and the storage are still to be written (marked TODO).

#define AUTO_DEFAULT_BAND_MASK  (((1u << (BAND_6M + 1)) - 1) & ~((1u << BAND_160M) - 1))
#define AUTO_LOAD_SWEEP_POINTS  12  // levels 1 and 2: 0, 3, ... 30, 31.5 dB
#define AUTO_LEVEL3_POINTS       8  // level 3: 0, 5, ... 25, 31.5 dB plus the drift repeat
#define AUTO_REFLECTION_POINTS   6  // open and short sweeps

// Fields in declaration order: active, level, bandMask; the rest start at zero
AutoCalSession autoCal = {false, 1, AUTO_DEFAULT_BAND_MASK};

void AutoUnkey(void);

static void RequestAutoService(void){
    autoCal.servicePending = true;
}

/**
 * @brief Run the work the AUTO calibration has posted from the state machine
 * @note Called from the main loop. Returns at once when there is nothing to do.
 */
void PowerCalAutoService(void){
    if (!autoCal.servicePending)
        return;
    // TODO: band change, attenuation and mode changes, fits,
    // checks and storage, depending on the state the request came from.
    autoCal.servicePending = false;
}

/**
 * @brief True while the power calibration state machine is in its AUTO branch
 */
bool PowerCalAutoActive(void){
    return autoCal.active;
}

/**
 * @brief Number of CW points to measure on the current band in the current stage
 * @note The load stage's SSB tone point comes after these
 */
static uint32_t AutoPointCount(void){
    if (autoCal.stage != AUTO_STAGE_LOAD)
        return AUTO_REFLECTION_POINTS;
    switch (autoCal.level){
        case 3:
            return AUTO_LEVEL3_POINTS;
        case 2:
            return AUTO_LOAD_SWEEP_POINTS + 1; // the sweep, then one wattmeter point near 2 W
        default:
            return AUTO_LOAD_SWEEP_POINTS;
    }
}

/**
 * @brief The first band in the session's band mask at or above band
 * @return The band, or LAST_BAND+1 if there is none
 */
static int32_t AutoBandFrom(int32_t band){
    while ((band <= LAST_BAND) && !(autoCal.bandMask & (1u << band)))
        band++;
    return band;
}

/**
 * @brief Start the automatic calibration
 * @note Called by Loop.cpp when button 1 is pressed in power calibration
 */
void StartPowerAutoCal(void){
    PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_AUTO);
}

/**
 * @brief Skip the current standard (level 1 only: the 50 Ohm load)
 * @note Called by Loop.cpp when button 12 is pressed during the automatic calibration
 */
void SkipPowerCalStage(void){
    PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_SKIP);
}

/**
 * @brief Repeat the session for the bands that failed
 * @note Called by Loop.cpp when button 14 is pressed on the review screen
 */
void RetryPowerCal(void){
    PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_RETRY);
}

/**
 * @brief Change the calibration level on the setup screen
 * @param delta +1 or -1; the level is kept within 1 to 3
 */
void ChangeAutoCalLevel(int32_t delta){
    if (powerSM.state_id != PowerCalSm_StateId_SETUP)
        return;
    int32_t level = (int32_t)autoCal.level + delta;
    if (level < 1) level = 1;
    if (level > 3) level = 3;
    autoCal.level = (uint8_t)level;
}

/**
 * @brief Guard: the automatic calibration runs with the 20 W PA only
 */
bool AutoCalAllowed(void){
    return !ED.PA100Wactive;
}

/**
 * @brief Entry action of AUTO: set the timing and start a new session
 */
void AutoCalEnter(void){
    autoCal.active = true;
    autoCal.keyed = false;
    autoCal.servicePending = false;
    autoCal.failedMask = 0;
    powerSM.vars.bandSettle_ms = 300;
    powerSM.vars.settle_ms = 500;
    powerSM.vars.collect_ms = 2000;
    powerSM.vars.rest_ms = 1000;
    powerSM.vars.meterTimeout_ms = 30000;
    Debug("Auto calibration: choose the level and press SELECT");
}

/**
 * @brief Exit action of AUTO: always leaves the transmitter unkeyed
 * @note Runs on every way out, including RESET in the middle of a point
 */
void AutoCalExit(void){
    AutoUnkey();
    autoCal.active = false;
    // TODO: return the mode to CALIBRATE_POWER_SPACE and restore the band and attenuation
}

/**
 * @brief SETUP -> PROMPT: start measuring with the first standard
 */
void AutoBeginSession(void){
    autoCal.stage = AUTO_STAGE_LOAD;
    autoCal.loadSkipped = false;
    autoCal.openFailMask = 0;
    autoCal.shortFailMask = 0;
    autoCal.failedMask = 0;
    Debug("Auto calibration level " + String(autoCal.level));
}

/**
 * @brief Entry action of PROMPT: ask the operator to fit the next standard
 */
void AutoPromptStandard(void){
    switch (autoCal.stage){
        case AUTO_STAGE_LOAD:
            if (autoCal.level > 1)
                Debug("Fit the wattmeter and its 50 Ohm load, then press SELECT");
            else
                Debug("Fit a 50 Ohm load, then press SELECT (button 12 skips it)");
            break;
        case AUTO_STAGE_OPEN:
            Debug("Remove everything from the antenna connector, then press SELECT");
            break;
        case AUTO_STAGE_SHORT:
            Debug("Fit the short on the antenna connector, then press SELECT");
            break;
    }
}

/**
 * @brief Guard: only the level 1 load stage may be skipped
 */
bool AutoCanSkipStage(void){
    return (autoCal.level == 1) && (autoCal.stage == AUTO_STAGE_LOAD);
}

/**
 * @brief Skip the load stage: keep the stored PA curves, no S50 check
 */
void AutoSkipStage(void){
    autoCal.loadSkipped = true;
    autoCal.stage = AUTO_STAGE_OPEN;
}

/**
 * @brief PROMPT -> BAND: start with the first band in the mask
 */
void AutoFirstBand(void){
    autoCal.band = AutoBandFrom(FIRST_BAND);
}

/**
 * @brief Entry action of BAND: tune to the band and plan its points
 */
void AutoSelectBand(void){
    autoCal.point = 0;
    autoCal.nPoints = AutoPointCount();
    autoCal.forwardFitDone = false;
    autoCal.ssbPoint = false;
    autoCal.oscillation = false;
    // TODO: band, mid-band frequency, LPF and the point list for this stage
    RequestAutoService();
}

/**
 * @brief Entry action of POINT: set the drive for the next point
 */
void AutoSetupPoint(void){
    // TODO: attenuation (and DSP gain) for this point; CW, or SSB for the tone point
    RequestAutoService();
}

/**
 * @brief Key the transmitter for a calibration point
 * @note Does nothing if the calibration has already keyed it
 */
void AutoKey(void){
    if (autoCal.keyed)
        return;
    autoCal.keyed = true;
    // TODO: SetInterrupt(iPTT_PRESSED)
}

/**
 * @brief Unkey the transmitter if the calibration keyed it
 */
void AutoUnkey(void){
    if (!autoCal.keyed)
        return;
    autoCal.keyed = false;
    // TODO: SetInterrupt(iPTT_RELEASED)
}

/**
 * @brief Guard: this point needs a wattmeter reading from the operator
 * @note Level 3: every CW point. Level 2: the one point after the sweep.
 */
bool AutoNeedsMeterEntry(void){
    if ((autoCal.stage != AUTO_STAGE_LOAD) || autoCal.ssbPoint)
        return false;
    if (autoCal.level == 3)
        return true;
    return (autoCal.level == 2) && (autoCal.point == autoCal.nPoints - 1);
}

/**
 * @brief Entry action of METER_ENTRY: start the operator's wattmeter entry
 */
void AutoStartMeterEntry(void){
    // TODO: target power for this point
    ResetPowerTarget();
}

/**
 * @brief COLLECT -> choice: keep the point's readings and check them
 */
void AutoStorePoint(void){
    // TODO: GetSWRBridgeStats(), the wattmeter entry, the oscillation check and a log row
    if (!autoCal.ssbPoint)
        autoCal.point++;
}

/**
 * @brief Guard: the last open or short point shows the PA oscillating
 */
bool AutoOscillationDetected(void){
    return autoCal.oscillation;
}

/**
 * @brief Abandon this band's sweep on this standard after an oscillation
 */
void AutoAbortBandStage(void){
    if (autoCal.stage == AUTO_STAGE_OPEN)
        autoCal.openFailMask |= (1u << autoCal.band);
    else if (autoCal.stage == AUTO_STAGE_SHORT)
        autoCal.shortFailMask |= (1u << autoCal.band);
    autoCal.oscillation = false;
}

/**
 * @brief Guard: CW points remain on this band and standard
 */
bool AutoMorePoints(void){
    return !autoCal.ssbPoint && (autoCal.point < autoCal.nPoints);
}

/**
 * @brief Guard: stay keyed between points (load stage only; the open and
 *        short sweeps rest in receive between points to protect the PA)
 */
bool AutoKeepKeyed(void){
    return autoCal.stage == AUTO_STAGE_LOAD;
}

/**
 * @brief Guard: the load stage's CW points are done, fit the forward line next
 */
bool AutoForwardFitDue(void){
    return (autoCal.stage == AUTO_STAGE_LOAD) && !autoCal.forwardFitDone;
}

/**
 * @brief Entry action of FIT_FORWARD: fit the band's forward line, then
 *        measure the SSB tone point with it
 */
void AutoFitForwardLine(void){
    // TODO: level 1 defaults, level 2 one-point offset, level 3 least squares
    autoCal.forwardFitDone = true;
    autoCal.ssbPoint = true;
    RequestAutoService();
}

/**
 * @brief Entry action of BAND_DONE: finish the band on this standard
 */
void AutoFinishBand(void){
    // TODO: load stage: PA curve and SSB gain into the session results
    autoCal.ssbPoint = false;
    RequestAutoService();
}

/**
 * @brief Guard: more bands to measure on this standard
 */
bool AutoMoreBands(void){
    return AutoBandFrom(autoCal.band + 1) <= LAST_BAND;
}

/**
 * @brief Move on to the next band in the mask
 */
void AutoNextBand(void){
    autoCal.band = AutoBandFrom(autoCal.band + 1);
}

/**
 * @brief Guard: another standard follows this one
 */
bool AutoMoreStages(void){
    return autoCal.stage != AUTO_STAGE_SHORT;
}

/**
 * @brief Move on to the next standard
 */
void AutoNextStage(void){
    autoCal.stage = (AutoCalStage)(autoCal.stage + 1);
}

/**
 * @brief Entry action of FIT_ALL: fit the reflected lines and check every band
 * @note A band whose short failed falls back to the open alone. A band whose
 *       open failed has no reflected line and fails.
 */
void AutoFitAndCheckAll(void){
    // TODO: reflected fits, S50 and the acceptance checks
    autoCal.failedMask = autoCal.openFailMask & autoCal.bandMask;
    RequestAutoService();
}

/**
 * @brief REVIEW -> DONE: store the bands that passed
 */
void AutoCommitResults(void){
    // TODO: passing bands into ED, then SaveDataToStorage(), from the main loop
    RequestAutoService();
}

/**
 * @brief Guard: at least one band failed
 */
bool AutoAnyFailed(void){
    return autoCal.failedMask != 0;
}

/**
 * @brief REVIEW -> SETUP: run the session again for the failed bands only
 */
void AutoRetryFailed(void){
    autoCal.bandMask = autoCal.failedMask;
    autoCal.failedMask = 0;
}

/**
 * @brief Guard: the main loop has finished the work the state machine posted
 */
bool AutoServiceIdle(void){
    return !autoCal.servicePending;
}


/**
 * Reset the power calibration measurement state machine to the start state. This
 * invokes the Reset function.
 */
void ResetPowerCal(void){
    PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_RESET);
}

/**
 * @brief Reset power calibration to initial state
 * @note Begins measurement sequence with power at 0dB attenuation, CW mode
 * @note Ensures ModeSm is in CALIBRATE_POWER_SPACE state
 * @note Resets data point counter and power target
 */
void ResetPowerCalibration(void){
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 0.0;
    SetTXAttenuation(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);
    // Make sure we are in CALIBRATE_POWER_SPACE state
    ModeSm_dispatch_event(&modeSM,ModeSm_EventId_OFFSET_END); // this updates the hardware state
    if (modeSM.state_id != ModeSm_StateId_CALIBRATE_POWER_SPACE)
        Debug("Error (MainBoard_DisplayCalibration_Power.cpp)! Should be in POWER_SPACE. Instead in "+String(modeSM.state_id));
    Npoints = 0;
    ResetPowerTarget(); 
}


/**
 * @brief Initialize next power calibration point measurement
 * @note Called upon entry to second and third power point measurements
 * @note Reduces target power by 6 dB (factor of 4) from previous point
 * @note Updates both targetPower and measuredPower values
 */
void StartPowerPoint(void){
    // We are starting a new power curve point. Change the target power by factor of 6 dB
    if (powerUnit)
        targetPower = targetPower / 4.0;
    else
        targetPower = targetPower - 6.0;
    measuredPower = targetPower;
}

/**
 * @brief Initialize SSB power calibration point measurement
 * @note Called upon entry to SSBPOINT state after CURVE_COMPLETE event (button 12)
 * @note Transitions to CALIBRATE_OFFSET_SPACE mode
 * @note Sets target power based on PA selection (100W or 20W)
 * @note Initializes DSP gain correction to -3.0 dB
 */
void StartSSBPoint(void){
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 0.0;
    SetTXAttenuation(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);
    // Move to CALIBRATE_OFFSET_SPACE state
    ModeSm_dispatch_event(&modeSM,ModeSm_EventId_OFFSET_START); // this updates the hardware state
    // Update target power and measured power
    if (ED.PA100Wactive){
        if (powerUnit)
            measuredPower = SSB_100W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(SSB_100W_CAL_POWER_POINT_W*1000); // dBm units
        targetPower = measuredPower;
        ED.PowerCal_100W_DSP_Gain_correction_dB[ED.currentBand[ED.activeVFO]] = -3.0;
    } else {
        if (powerUnit)
            measuredPower = SSB_20W_CAL_POWER_POINT_W;
        else
            measuredPower = 10*log10f(SSB_20W_CAL_POWER_POINT_W*1000); // dBm units
        targetPower = measuredPower;
        ED.PowerCal_20W_DSP_Gain_correction_dB[ED.currentBand[ED.activeVFO]] = -3.0;
    }
}

/**
 * @brief Calculate SSB power gain correction factor from measured power
 * @note Called upon entry to MeasurementComplete mode when record button pressed
 * @note Compares measured power to expected SSB calibration point
 * @note Stores DSP gain correction in dB in ED structure for current band
 */
void CalculateSSBPoint(void){
    // The measuredPower is what we're actually reading
    float32_t factor;
    powr_W = measuredPower;
    if (powerUnit == 0){
        powr_W = pow(10.0f,measuredPower/10.0f)/1000.0f;
    }
    if (ED.PA100Wactive){
        factor = SSB_100W_CAL_POWER_POINT_W / powr_W;
    } else {
        factor = SSB_20W_CAL_POWER_POINT_W / powr_W;
    }

    Debug("SSB power ratio = " + String(factor));
    // The measurement was made with TXgainDSP applied, so the correction is relative to it
    float32_t corr = TXgainDSP + 10*log10f(factor);
    Debug("Therefore gain correction is [dB] = " + String(corr));
    if (ED.PA100Wactive)
        ED.PowerCal_100W_DSP_Gain_correction_dB[ED.currentBand[ED.activeVFO]] = corr;
    else
        ED.PowerCal_20W_DSP_Gain_correction_dB[ED.currentBand[ED.activeVFO]] = corr;

}

/**
 * Called when the MENU SELECT button is pressed. Issues an event to the state machine
 */
void RecordPowerButtonPressed(void){
    PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_RECORD_DATA_POINT);
}

/**
 * Called when exiting the PowerPoint1, PowerPoint2, PowerPoint3, and SSBPoint states.
 * Invoked when the RECORD_DATA_POINT event is issued
 */
void RecordPowerDataPoint(void){
    switch (modeSM.state_id){
        case ModeSm_StateId_CALIBRATE_POWER_SPACE:{
            if (Npoints >= sizeof(powers_W)/sizeof(powers_W[0]))
                Npoints = 0;
            attenuations_dB[Npoints] = ED.XAttenCW[ED.currentBand[ED.activeVFO]];
            if (powerUnit)
                powers_W[Npoints] = measuredPower;
            else
                powers_W[Npoints] = pow(10.0f,measuredPower/10.0f)/1000.0f;
            // Record the P_f[W], P_r[W], and SWR values
            Pf_W[Npoints] = ReadForwardPower();
            Pr_W[Npoints] = ReadReflectedPower();
            SWR[Npoints] = ReadSWR();
            Npoints++;
            break;
        }
        case ModeSm_StateId_CALIBRATE_OFFSET_SPACE:{
            // Do nothing when measuring the SSB power point -- we only record one
            // data point which is kept in measuredPower and is handled by the 
            // CalculateSSBPoint function.
            break;
        }
        default:
            break;
    }
}