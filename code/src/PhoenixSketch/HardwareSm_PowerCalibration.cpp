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

// True while the main loop is dispatching an event to powerSM. The 1 ms timer
// skips its DO event then, so that it cannot run in the middle of a transition.
static volatile bool powerSmDispatching = false;

/**
 * @brief Dispatch an event to the power calibration state machine from the main loop
 * @param event The event
 * @note Use this, not PowerCalSm_dispatch_event(), for every event except the
 *       timer's DO. The timer interrupt skips DO while this runs; missing a 1 ms
 *       tick is harmless. Interrupts stay enabled, because the manual flow's
 *       actions use I2C and fit the power curve.
 */
void PowerCalDispatch(PowerCalSm_EventId event){
    powerSmDispatching = true;
    PowerCalSm_dispatch_event(&powerSM, event);
    powerSmDispatching = false;
}

/**
 * @brief True while the main loop is dispatching an event to powerSM
 */
bool PowerCalSmBusy(void){
    return powerSmDispatching;
}

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
    PowerCalDispatch(PowerCalSm_EventId_RESET);
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
        ED.PowerCal_100W_shape[ED.currentBand[ED.activeVFO]] = f.shape;
    } else {
        f = FitPowerCurve(attenuations_dB, powers_mW, Npoints, 15000.0f,6.0f);
        ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]] = f.P_sat;
        ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]] = f.k;
        ED.PowerCal_20W_shape[ED.currentBand[ED.activeVFO]] = f.shape;
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
 * @note Resets all band attenuations to zero. The SWR calibration is kept: the
 *       manual flow does not use it, and the automatic flow replaces it per band.
 * @note Updates hardware state to apply settings
 */
void InitializePowerCalibration(void){
    // PowerCalSm_start() does not run AUTO's exit action, so clear it here
    autoCal.active = false;
    autoCal.keyed = false;
    autoCal.servicePending = false;
    powerSmDispatching = true;
    PowerCalSm_start(&powerSM);
    powerSmDispatching = false;
    // Make all the attenuations zero so we start off right
    for (size_t k = FIRST_BAND; k<=LAST_BAND; k++){
        ED.XAttenCW[k] = 0.0;
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
        PowerCalDispatch(PowerCalSm_EventId_CURVE_COMPLETE);
        // Change to offset measurement mode
        ModeSm_dispatch_event(&modeSM,ModeSm_EventId_OFFSET_START);
        UpdateRFHardwareState();
    }
}


/**
 * Reset the power calibration measurement state machine to the start state. This
 * invokes the Reset function.
 */
void ResetPowerCal(void){
    PowerCalDispatch(PowerCalSm_EventId_RESET);
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
    PowerCalDispatch(PowerCalSm_EventId_RECORD_DATA_POINT);
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