/**
 * The power calibration routine uses its own state machine to handle stepping through 
 * the process. The state machine itself is described by the StateSmith UML diagram
 * in PowerCalSm.drawio and the generated source code files PowerCalSm.cpp/h. This file
 * contains the functions used by the state machine to ensure clean separation between
 * the graphical code in MainBoard_DisplayCalibration_Power.cpp and the rest of the code
 */

#include "SDT.h"

PowerCalSm powerSM;
static char buff[100];

float32_t attenuations_dB[3];
float32_t powers_W[3];
static float32_t Pf_W[3];
static float32_t Pr_W[3];
static float32_t SWR[3];
uint32_t Npoints = 0;
float32_t measuredPower = 0.0;
float32_t targetPower = 0.0;
static float32_t powr_W; // SSB power point
uint8_t powerUnit = 1; // 1=W, 0=dBm

float32_t GetMeasuredPower(void){
    return measuredPower;
}

float32_t GetTargetPower(void){
    return targetPower;
}

uint8_t GetPowerUnit(void){
    return powerUnit;
}

void SetMeasuredPower(float32_t newPower){
    measuredPower = newPower;
}

float32_t GetPowDataSum(void){
    float32_t powsum = abs(measuredPower);
    for (size_t k=0; k<Npoints; k++){
        powsum += abs(powers_W[k]);
    }
    return powsum;
}

uint32_t GetNpoints(void){
    return Npoints;
}

float32_t GetAttenuation_dB(uint32_t k){
    return attenuations_dB[k];
}

float32_t GetPower_W(uint32_t k){
    return powers_W[k];
}

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
 * Called by Loop.cpp when button 16 is pressed.
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
 * Automatically called upon entry to the PowerCalSm_StateId_POWERCOMPLETE state
 * attenuations_dB and powers_W have been populated by prior steps
 */
void CalculatePowerCurveFit(void){
    Debug("Invoked power curve fit");
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
    Serial.println("| P_meas [W] | P_f [W] | P_r [W] | SWR |");
    Serial.println("|------------|---------|---------|-----|");
    for (size_t k=0; k<3; k++){
        sprintf(buff,"| %5.4f | %5.4f | %5.4f | %5.4f |",
                    powers_W[k],Pf_W[k],Pr_W[k],SWR[k]);
        Serial.println(buff);
    }
}

void InitializePowerCalibration(void){
    PowerCalSm_start(&powerSM);
    powerSM.vars.acquisitionDuration_ms = 100;
    // Make all the attenuations zero so we start off right
    // Make the SWR offset adjustments zero as well
    for (size_t k = FIRST_BAND; k<=LAST_BAND; k++){
        ED.XAttenCW[k] = 0.0;
        ED.SWR_F_SlopeAdj[k] = 0.0;
        ED.SWR_R_SlopeAdj[k] = 0.0;
        ED.SWR_F_Offset[ED.currentBand[ED.activeVFO]] = 0.0;
        ED.SWR_R_Offset[ED.currentBand[ED.activeVFO]] = 0.0;
    }
    SetTXAttenuation(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);
}


static float32_t forwardPowerAtt_dB[40];
static float32_t forwardPower_W[40];
static float32_t scaledForwardPower_mW[40];
static uint32_t Nswrpoints = 0;

void StartPowerAutoCal(void){
    if (powerSM.state_id == PowerCalSm_StateId_POWERPOINT1){
        // Make sure attenuation is zero
        ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 0.0;
        SetTXAttenuation(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);
        Nswrpoints = 0;

        // Issue a PTT event
        SetInterrupt(iPTT_PRESSED);

        // Change the state
        PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_AUTO);
    }
}

// Used by the auto cal
void AdjustAttenuation(void){
    // Read the current forward power
    forwardPowerAtt_dB[Nswrpoints] = ED.XAttenCW[ED.currentBand[ED.activeVFO]];
    forwardPower_W[Nswrpoints] = ReadForwardPower();
    Nswrpoints++;
    // Change attenuation by 1 dB
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] += 1.0;
    if (ED.XAttenCW[ED.currentBand[ED.activeVFO]] <= 31.5){
        SetTXAttenuation(ED.XAttenCW[ED.currentBand[ED.activeVFO]]);
        PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_NEXT_POINT);
    } else {
        // Leave this place
        SetInterrupt(iPTT_RELEASED);

        // Fit curve
        // 1. Calculate power for each point
        if (powerUnit == 0){
            // Convert from dBm to W
            measuredPower = pow(10.0f,measuredPower/10.0f)/1000.0f;
        }
        float32_t scale = measuredPower / forwardPower_W[0];

        sprintf(buff,"External measured power at 0dB is %5.4fW",measuredPower);
        Serial.println(buff);
        sprintf(buff,"Internal measured power at 0dB is %5.4fW",forwardPower_W[0]);
        Serial.println(buff);
        sprintf(buff,"Scale factor %5.4f",scale);
        Serial.println(buff);

        Serial.println("| Att [dB] | Pswr [W] | Pscaled [W] |");
        Serial.println("|----------|----------|-------------|");
        for (size_t k=0; k<Nswrpoints; k++){
            scaledForwardPower_mW[k] = scale*forwardPower_W[k]*1000.0;
            sprintf(buff,"| %3.2f | %5.4f | %5.4f |",
                        forwardPowerAtt_dB[k],forwardPower_W[k],scaledForwardPower_mW[k]/1000.0);
            Serial.println(buff);
        }

        // 2. Fit the curve
        struct FitResult f;
        if (ED.PA100Wactive){
            f = FitPowerCurve(forwardPowerAtt_dB, scaledForwardPower_mW, Nswrpoints, 75000.0f,10.0f);
            ED.PowerCal_100W_Psat_mW[ED.currentBand[ED.activeVFO]] = f.P_sat;
            ED.PowerCal_100W_kindex[ED.currentBand[ED.activeVFO]] = f.k;
        } else {
            f = FitPowerCurve(forwardPowerAtt_dB, scaledForwardPower_mW, Nswrpoints, 15000.0f,6.0f);
            ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]] = f.P_sat;
            ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]] = f.k;
        }

        PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_CURVE_COMPLETE);
    }
}


/**
 * Reset the power calibration measurement state machine to the start state. This
 * invokes the Reset function.
 */
void ResetPowerCal(void){
    PowerCalSm_dispatch_event(&powerSM,PowerCalSm_EventId_RESET);
}

/**
 * Begin the measurement sequence with the power at 0dB of attenuation, CW
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

/**
 * Called upon entry to second and third power point measurements
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
 * Called upon entry to the SSBPOINT state when the CURVE_COMPLETE event is issued after
 * button 12 is pressed.
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
 * Called upon entry to the MeasurementComplete mode when the record data point button is pressed
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

    Debug("Factor = " + String(factor));
    float32_t corr = 10*log10f(factor);
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