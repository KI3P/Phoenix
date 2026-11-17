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
static int16_t maxDBC_parameter = 0.0;
static int8_t iteration = 0;
static int8_t step = 0;
static bool bandCompleted[NUMBER_OF_BANDS]; // all should start as false
static int32_t currentBand = -1;
// The band the automatic run is working on, and its offsets before the run
// started on it, so that an aborted run can put them back
static int32_t savedBand = -1;
static int32_t savedI = 0;
static int32_t savedQ = 0;

/*
 * Performance log, for judging the algorithm on a real radio. Each step of the
 * run prints one line on the diagnostics serial port (Debug(): the first port of
 * a Dual Serial build; silent in USB audio builds, where CAT shares the port).
 * Comma-separated, each line starting with CARRCAL:
 *   CARRCAL,BAND,<band>,<I before>,<Q before>
 *   CARRCAL,READ,<band>,<pass>,<step>,<I>,<Q>,<tone dB>,<carrier dB>,<raw dBc>,<dBc>,<best dBc>,<best value>
 *       one reading: the offsets tried, the tone and carrier bins behind the
 *       latest measurement (raw dBc = tone - carrier), the smoothed suppression
 *       the algorithm compares (dBc), and the best value of the pass so far
 *   CARRCAL,PASS,<band>,<pass>,<I|Q>,<value kept>,<best dBc>,<valid 0|1>
 *       end of a pass; valid 0: no reading above 0 dB, the offset is unchanged
 *   CARRCAL,DONE,<band>,<I>,<Q>,<dBc>
 *   CARRCAL,ABORT,<band>,<I restored>,<Q restored>
 *   CARRCAL,END
 * Passes 0, 2, 4 sweep I and 1, 3, 5 sweep Q. code/tools/carrier_cal_log.py
 * records and summarises the log.
 */
static void LogCarrierCal(const char *line){
    Debug(line);
    (void)line;
}

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

void ResetTXCarrierCalBand(void){
    // Mark all the bands as not-completed:
    for (size_t k = FIRST_BAND; k<=LAST_BAND; k++)
        bandCompleted[k] = false;
    // Only the bands the radio may transmit on (see NextTXCalBand)
    currentBand = NextTXCalBand(FIRST_BAND);
    savedBand = -1;
    ForceUpdateRFHardwareState();
    step = 0;
    iteration = 0;
    maxDBC = 0;
}

/**
 * @brief Stop an automatic carrier null run, if one is in progress
 * @note Restores the I/Q offsets of the band being swept and releases the PTT
 *       the run engaged. Bands already completed keep their new values.
 */
void AbortTXCarrierCalibration(void){
    if (txcarrSM.state_id != TransmitCarrierCalSm_StateId_STANDBY){
        if ((savedBand >= FIRST_BAND) && (savedBand <= LAST_BAND) && !bandCompleted[savedBand]){
            ED.DCOffsetI[savedBand] = savedI;
            ED.DCOffsetQ[savedBand] = savedQ;
            char line[64];
            snprintf(line, sizeof(line), "CARRCAL,ABORT,%s,%d,%d", bands[savedBand].name,
                     (int)savedI, (int)savedQ);
            LogCarrierCal(line);
        }
        SetInterrupt(iPTT_RELEASED);
    }
    InitializeTXCarrierCalibration();
}

void AdjustTXCarrierBand(void){
    if ((currentBand < FIRST_BAND) || bandCompleted[currentBand]){
        // Go on to the next band the radio may transmit on; if there is none, exit
        int32_t next = (currentBand < FIRST_BAND) ? -1 : NextTXCalBand(currentBand + 1);
        if (next < 0){
            LogCarrierCal("CARRCAL,END");
            TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_AUTO_COMPLETE);
            SetInterrupt(iPTT_RELEASED);
            return;
        }
        currentBand = next;
    }
    // Set the hardware to the current band
    ED.currentBand[ED.activeVFO] = currentBand;
    // Set center frequency to band center, modulation mode based on the band (USB or LSB)
    ED.centerFreq_Hz[ED.activeVFO] = (bands[currentBand].fBandHigh_Hz + bands[currentBand].fBandLow_Hz) / 2 + SR[SampleRate].rate / 4;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 0;
    ED.modulation[ED.activeVFO] = bands[currentBand].mode;
    UpdateRFHardwareState();

    Debug(String("Calibrating TX Carrier band ") + String(bands[currentBand].name));
    // Remember this band's offsets in case the run is aborted
    if (savedBand != currentBand){
        savedBand = currentBand;
        savedI = ED.DCOffsetI[currentBand];
        savedQ = ED.DCOffsetQ[currentBand];
        char line[64];
        snprintf(line, sizeof(line), "CARRCAL,BAND,%s,%d,%d", bands[currentBand].name,
                 (int)savedI, (int)savedQ);
        LogCarrierCal(line);
    }
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

        char line[80];
        snprintf(line, sizeof(line), "CARRCAL,PASS,%s,%d,%s,%d,%.2f,%d", bands[currentBand].name,
                 (int)iteration, (iteration%2 == 0) ? "I" : "Q", (int)maxDBC_parameter,
                 (double)maxDBC, (maxDBC > 0) ? 1 : 0);
        LogCarrierCal(line);

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
        char line[80];
        snprintf(line, sizeof(line), "CARRCAL,DONE,%s,%d,%d,%.2f", bands[currentBand].name,
                 (int)ED.DCOffsetI[currentBand], (int)ED.DCOffsetQ[currentBand], (double)maxDBC_save);
        LogCarrierCal(line);
        TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_MIN_EXIT);
        return;
    }
    // Each pass starts from the setting it sweeps around. If no reading in
    // the pass is above 0 dB (a broken loopback), the setting stays where it
    // was, rather than taking the best value of the previous pass
    if (step == 0){
        int32_t b = ED.currentBand[ED.activeVFO];
        maxDBC_parameter = (iteration%2 == 0) ? ED.DCOffsetI[b] : ED.DCOffsetQ[b];
    }
    // Change the appropriate parameter
    SetOffset(iteration,step); 
    // Go to read data state after waiting for txcarrSM.vars.acquisitionDuration_ms
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_READ_DELTA);
}

void ReadTXCarrierDelta(void){
    bool ignore = (iteration == 0) && (step == 0);
    if ((GetTXCarrierVals(ED.currentBand[ED.activeVFO]) > maxDBC) && !ignore){
        // The value of the carrier suppression
        maxDBC = GetTXCarrierVals(ED.currentBand[ED.activeVFO]);
        // The amp/phase parameter that delivered this carrier suppression
        maxDBC_parameter = GetNewVal(iteration, step);
    }
    // Log the reading
    float32_t tone_dB, carrier_dB;
    GetTXCalBins_dB(&tone_dB, &carrier_dB);
    int32_t b = ED.currentBand[ED.activeVFO];
    char line[128];
    snprintf(line, sizeof(line), "CARRCAL,READ,%s,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d",
             bands[b].name, (int)iteration, (int)step, (int)ED.DCOffsetI[b], (int)ED.DCOffsetQ[b],
             (double)tone_dB, (double)carrier_dB, (double)(tone_dB - carrier_dB),
             (double)GetTXCarrierVals(b), (double)maxDBC, (int)maxDBC_parameter);
    LogCarrierCal(line);

    // Proceed to the next step in this iteration
    step++;
    
    // Go to ADJUST state for next offset step
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_NEXT_POINT);
}
