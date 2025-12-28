/**
 * The transmit carrier calibration routine uses a state machine to handle stepping through 
 * the process. The state machine itself is described by the StateSmith UML diagram
 * in TransmitCarrierCalSm.drawio and the generated source code files TransmitCarrierCalSm.cpp/h. 
 * This file contains the functions used by the state machine to ensure clean separation 
 * between the graphical code in MainBoard_DisplayCalibration_TXIQ.cpp and the rest of 
 * the code
 */

#include "SDT.h"

TransmitCarrierCalSm txcarrSM;

/**
 * TX Carrier Auto-Tune Algorithm
 *
 * Systematically sweeps DC offsets for I and Q to maximize carrier suppresion (dBc).
 *
 * Three-pass approach with progressively finer resolution:
 *
 * Pass 1 (Coarse):
 *   - Iteration 0: I DC offset -5000 to 5000 in 100 steps
 *   - Iteration 1: Q DC offset -5000 to 5000 in 100 steps
 *
 * Pass 2 (Medium):
 *   - Iteration 2: I DC offset ±12 steps of 10 around Pass 1 optimum
 *   - Iteration 3: Q DC offset ±12 steps of 10 around Pass 1 optimum
 *
 * Pass 3 (Fine):
 *   - Iteration 4: I DC offset ±12 steps of 1 around Pass 2 optimum
 *   - Iteration 5: Q DC offset ±12 steps of 1 around Pass 2 optimum
 *
 * For each iteration, measures carrier suppression and records best-performing value.
 * Automatically advances through all bands.
 */
static int16_t center[] =  {0,                  0,                         0,  0,  0,  0 };
static int8_t NSteps[]  =  {(int)((5000+5000)/100),(int)((5000+5000)/100),24, 24, 24, 24 };
static int16_t Delta[] =   {100,               100,                       10, 10,  1,  1 };
static float32_t maxDBC = 0.0;
static float32_t maxDBC_parameter = 0.0;
static int8_t iteration = 0;
static int8_t step = 0;
static bool bandCompleted[NUMBER_OF_BANDS]; // all should start as false
static int32_t currentBand = -1;

/**
 * @brief Calculate parameter value for given iteration and step
 * @param iter Iteration number (0-5)
 * @param stp Step number within iteration
 * @return Calculated amplitude or phase value
 */
static int16_t GetNewVal(int8_t iter, int8_t stp){
    int16_t newval = center[iter]-(NSteps[iter]*Delta[iter])/2+stp*Delta[iter];
    return newval;
}

/**
 * @brief Set I or Q offset factor for auto-tune algorithm
 * @param iter Iteration number (even=I, odd=Q)
 * @param stp Step number within iteration
 */
static void SetOffset(int8_t iter, int8_t stp){
    int16_t newval = GetNewVal(iter, stp);
    if (iter%2 == 0){
        ED.DCOffsetI[ED.currentBand[ED.activeVFO]] = newval;
    } else {
        ED.DCOffsetQ[ED.currentBand[ED.activeVFO]] = newval;
    }
}

/**
 * @brief Initialize transmit carrier calibration state machine
 * @note Starts TransmitCarrierCalSm state machine with 150ms acquisition duration
 */
void InitializeTXCarrierCalibration(void){
    TransmitCarrierCalSm_start(&txcarrSM);
    txcarrSM.vars.acquisitionDuration_ms = 60;
}

void SetTXCarrierCurrentBand(int32_t band){
    currentBand = band;
}

void ResetTXCarrierCalBand(void){
    // Mark all the bands as not-completed:
    for (size_t k = FIRST_BAND; k<=LAST_BAND; k++)
        bandCompleted[k] = false;
    currentBand = FIRST_BAND;
    ForceUpdateRFHardwareState();
    step = 0;
    iteration = 0;
    maxDBC = 0;
}

void AdjustTXCarrierBand(void){
    if (bandCompleted[currentBand]){
        // Was this the last band? If so, exit.
        if (currentBand == LAST_BAND){
            TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_AUTO_COMPLETE);
            SetInterrupt(iPTT_RELEASED);
            return;
        }
        // Increment to the next band.
        currentBand++;
    }
    // Set the hardware to the current band
    ED.currentBand[ED.activeVFO] = currentBand;
    // Set center frequency to band center, modulation mode based on the band (USB or LSB)
    ED.centerFreq_Hz[ED.activeVFO] = (bands[currentBand].fBandHigh_Hz + bands[currentBand].fBandLow_Hz) / 2 + SR[SampleRate].rate / 4;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 0;
    ED.modulation[ED.activeVFO] = bands[currentBand].mode;
    UpdateRFHardwareState();

    Debug(String("Calibrating TX Carrier band ") + String(bands[currentBand].name));
    // Start by setting the Q offset to 0
    ED.DCOffsetQ[ED.currentBand[ED.activeVFO]] = 0;
    // Go to find minimum loop
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_FIND_MINIMUM);
}

void ResetTXCarrierCalSettings(void){
    step = 0;
    iteration = 0;
    maxDBC = 0;
}

void SetTXCarrierVals(int32_t band, float32_t value);
static float32_t maxDBC_save;

void AdjustTXCarrierCalSetting(void){
    // Have we completed all the steps in this iteration?
    if (step >= NSteps[iteration]){
        // Set the parameter we were changing to the minimum value
        if (iteration%2 == 0){
            ED.DCOffsetI[ED.currentBand[ED.activeVFO]] = maxDBC_parameter;
        } else {
            ED.DCOffsetQ[ED.currentBand[ED.activeVFO]] = maxDBC_parameter;
        }
        // The next time we step around the amplitude or phase, use this as our starting point
        int8_t nextIndex = iteration + 2;
        if (nextIndex < (int8_t)(sizeof(center)/sizeof(center[0])))
            center[nextIndex] = maxDBC_parameter;

        // Go to the next iteration
        step = 0;
        iteration++;
        maxDBC_save = maxDBC;
        maxDBC = 0.0;
    }
    // Have we completed all the iterations and ready to go to the next band?
    if (iteration >= (int8_t)(sizeof(center)/sizeof(center[0]))){
        // Set the parameter we were changing to the minimum value
        if ((iteration-1)%2 == 0){
            ED.DCOffsetI[currentBand] = maxDBC_parameter;
        } else {
            ED.DCOffsetQ[currentBand] = maxDBC_parameter;
        }
        SetTXCarrierVals(currentBand, maxDBC_save);
        bandCompleted[currentBand] = true;
        TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_MIN_EXIT);
        return;
    }
    // Change the appropriate parameter
    SetOffset(iteration,step); 

    // Go to read data state after waiting for txcarrSM.vars.acquisitionDuration_ms
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_READ_DELTA);
}

void ReadTXCarrierDelta(void){
    if (GetTXCarrierVals(ED.currentBand[ED.activeVFO]) > maxDBC){
        // The value of the carrier suppression
        maxDBC = GetTXCarrierVals(ED.currentBand[ED.activeVFO]);
        // The amp/phase parameter that delivered this carrier suppression
        maxDBC_parameter = GetNewVal(iteration, step);
    }
    // Proceed to the next step in this iteration
    step++;
    
    // Go to ADJUST state for next offset step
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_NEXT_POINT);
}
