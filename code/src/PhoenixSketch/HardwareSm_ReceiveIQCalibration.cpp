/**
 * The receive IQ calibration routine uses a state machine to handle stepping through 
 * the process. The state machine itself is described by the StateSmith UML diagram
 * in ReceiveIQCalSm.drawio and the generated source code files ReceiveIQCalSm.cpp/h. 
 * This file contains the functions used by the state machine to ensure clean separation 
 * between the graphical code in MainBoard_DisplayCalibration_RXIQ.cpp and the rest of 
 * the code
 */

#include "SDT.h"

ReceiveIQCalSm rxiqSM;

/**
 * RX IQ Auto-Tune Algorithm
 *
 * Systematically sweeps amplitude and phase parameters to maximize sideband separation.
 *
 * Three-pass approach with progressively finer resolution:
 *
 * Pass 1 (Coarse):
 *   - Iteration 0: Amplitude 0.5 to 1.5 in 0.01 steps
 *   - Iteration 1: Phase -0.2 to 0.2 in 0.01 steps
 *
 * Pass 2 (Medium):
 *   - Iteration 2: Amplitude ±4 steps around Pass 1 optimum
 *   - Iteration 3: Phase ±4 steps around Pass 1 optimum
 *
 * Pass 3 (Fine):
 *   - Iteration 4: Amplitude ±10 steps (0.001) around Pass 2 optimum
 *   - Iteration 5: Phase ±10 steps (0.001) around Pass 2 optimum
 *
 * For each iteration, measures sideband separation and records best-performing value.
 * Automatically advances through all bands.
 */
static float32_t center[] ={1.0,                  0.0,                  0.0, 0.0, 0.0, 0.0};
static int8_t NSteps[]  =  {(int)((1.5-0.5)/0.01),(int)((0.2+0.2)/0.01),9,   21,  9,   21 }; 
static float32_t Delta[] = {0.02,                 0.01,                 0.01,0.01,0.001,0.001};
static float32_t maxSBS = 0.0;
static float32_t maxSBS_parameter = 0.0;
static int8_t iteration = 0;
static int8_t step = 0;
static bool bandCompleted[NUMBER_OF_BANDS]; // all should start as false
static float32_t deltaVals[NUMBER_OF_BANDS];  // Sideband separation values for each band
static float32_t sideband_separation = 0.0;
static int32_t currentBand = -1;
static bool finalMeasurement = false;  // Flag to indicate final measurement at optimal point

/**
 * @brief Calculate parameter value for given iteration and step
 * @param iter Iteration number (0-5)
 * @param stp Step number within iteration
 * @return Calculated amplitude or phase value
 */
static float32_t GetNewVal(int8_t iter, int8_t stp){
    float32_t newval = center[iter]-(NSteps[iter]*Delta[iter])/2.0+stp*Delta[iter];
    return newval;
}

/**
 * @brief Set amplitude or phase correction factor for auto-tune algorithm
 * @param iter Iteration number (even=amplitude, odd=phase)
 * @param stp Step number within iteration
 */
static void SetAmpPhase(int8_t iter, int8_t stp){
    float32_t newval = GetNewVal(iter, stp);
    if (iter%2 == 0){
        ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = newval;
    } else {
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = newval;
    }
}

/**
 * @brief Initialize receive IQ calibration state machine
 * @note Starts ReceiveIQCalSm state machine with 150ms acquisition duration
 */
void InitializeRXIQCalibration(void){
    ReceiveIQCalSm_start(&rxiqSM);
    rxiqSM.vars.acquisitionDuration_ms = 60;
}

void ResetRXIQCalBand(void){
    // Mark all the bands as not-completed:
    for (size_t k = FIRST_BAND; k<=LAST_BAND; k++)
        bandCompleted[k] = false;
    currentBand = FIRST_BAND;
    ForceUpdateRFHardwareState();
    step = 0;
    iteration = 0;
    maxSBS = 0;
}

void AdjustRXIQBand(void){
    if (bandCompleted[currentBand]){
        // Was this the last band? If so, exit.
        if (currentBand == LAST_BAND){
            ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_AUTO_COMPLETE);
            return;
        }
        // Increment to the next band.
        currentBand++;
    }
    // Set the hardware to the current band
    ED.currentBand[ED.activeVFO] = currentBand;
    UpdateRFHardwareState();

    Debug(String("Calibrating RXIQ band ") + String(bands[currentBand].name));
    // Start by setting the phase to 0
    ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = 0.0f;

    // Go to find minimum loop
    ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_FIND_MINIMUM);
}

void ResetRXIQCalSettings(void){
    step = 0;
    iteration = 0;
    maxSBS = 0;
}

float32_t maxSBS_save;

void AdjustRXIQCalSetting(void){
    // Have we completed all the steps in this iteration?
    if (step >= NSteps[iteration]){
        // Set the parameter we were changing to the minimum value
        if (iteration%2 == 0){
            ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]] = maxSBS_parameter;
        } else {
            ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]] = maxSBS_parameter;
        }
        // The next time we step around the amplitude or phase, use this as our starting point
        int8_t nextIndex = iteration + 2;
        if (nextIndex < 6)
            center[nextIndex] = maxSBS_parameter;

        // Go to the next iteration
        step = 0;
        iteration++;
        maxSBS_save = maxSBS;
        maxSBS = 0.0;
    }
    // Have we completed all the iterations and ready to go to the next band?
    if (iteration > 5){
        // Set the parameter we were changing to the minimum value
        if ((iteration-1)%2 == 0){
            ED.IQAmpCorrectionFactor[currentBand] = maxSBS_parameter;
        } else {
            ED.IQPhaseCorrectionFactor[currentBand] = maxSBS_parameter;
        }
        // Take a final measurement at the optimal point
        finalMeasurement = true;
        ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_READ_DELTA);
        return;
    }
    finalMeasurement = false;
    // Change the appropriate parameter
    SetAmpPhase(iteration,step); 

    // Go to read data state after waiting for rxiqSM.vars.acquisitionDuration_ms
    ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_READ_DELTA);
}

static int32_t deltaCount = 0;
/**
 * Calculate the difference in dB between the tone in the upper and lower
 * sidebands of the psd. This function is called every 1ms while the PSD 
 * is only updated every 10ms, so include a counter that runs the code
 * every 10th call
 */
void UpdateRXDeltaVal(void){
    if (deltaCount++ == 10){
        // Because we set the CW tone to be 48 kHz above or below the LO, the upper
        // and lower sideband products will be in very specific bins. Upper will be
        // in bin 3/4*512 = 384, lower will be in bin 1/4*512 = 128
        float32_t upper = psdnew[384]; 
        float32_t lower = psdnew[128]; 
        if (bands[ED.currentBand[ED.activeVFO]].mode == LSB){
            sideband_separation = (upper-lower)*10;
        } else {
            sideband_separation = (lower-upper)*10;
        }
        if (finalMeasurement) {
            // For final measurement, use direct value (no IIR filter smoothing)
            deltaVals[ED.currentBand[ED.activeVFO]] = sideband_separation;
        } else {
            // During optimization, use IIR filter to smooth noisy measurements
            deltaVals[ED.currentBand[ED.activeVFO]] = 0.5*deltaVals[ED.currentBand[ED.activeVFO]]+0.5*sideband_separation;
        }
        deltaCount = 0;
    }
}

float32_t GetRXDeltaVals(int32_t band){
    if ((band >= 0) && (band < NUMBER_OF_BANDS))
        return deltaVals[band];
    else
        return NAN;
}

void ReadRXIQDelta(void){
    if (finalMeasurement){
        // Final measurement at optimal point - complete the band
        bandCompleted[currentBand] = true;
        ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_MIN_EXIT);
        return;
    }
    if (deltaVals[ED.currentBand[ED.activeVFO]] > maxSBS){
        // The value of the sideband separation
        maxSBS = deltaVals[ED.currentBand[ED.activeVFO]];
        // The amp/phase parameter that delivered this sideband separation
        maxSBS_parameter = GetNewVal(iteration, step);
    }
    // Proceed to the next step in this iteration
    step++;

    // Go to ADJUST state for next amp/phase step
    ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_NEXT_POINT);
}
