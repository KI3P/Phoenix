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
#include <algorithm>

TransmitCarrierCalSm txcarrSM;

/**
 * TX Carrier Auto-Tune Algorithm
 *
 * Sweeps the I and Q DC offsets, one at a time, to maximise the carrier
 * suppression (dBc: how far the carrier is below the tone). Each band starts
 * from its stored offsets. Ten passes ("iterations"), even ones sweeping I and
 * odd ones Q:
 *   - 0, 1: coarse, -5000 to +4900 in steps of 100
 *   - 2, 3 and again 4, 5: medium, 24 steps of 10 around the value kept
 *   - 6, 7 and again 8, 9: fine, 24 steps of 1 around the value kept
 * Each step size runs I and Q twice (from the medium passes on): I and Q
 * interact when one of them is far off, so the second round refines the first.
 *
 * A pass keeps its best reading only if that reading stands out: at least
 * CARRIER_MIN_CONTRAST_DB above the median of the pass. A pass that is flat
 * (a broken loopback, the other offset far off, or a floor the offsets cannot
 * move) keeps the value it swept around instead of a value picked by noise. A
 * band with no such pass at all gets back the offsets it had before the run.
 * The first reading of every pass follows a large jump in the offset and is
 * not used.
 *
 * Each step waits before its reading: CARRIER_COARSE_DWELL_MS in the coarse
 * passes, CARRIER_DWELL_MS in the others. The spectrum blends each FFT frame
 * with the previous ones, so a reading still carries some of the previous
 * step's carrier; near the null that leftover dominates and shifts the peak in
 * the sweep direction. In the coarse passes the shift reached two 100-count
 * steps on the KI3P prototype (2026-11-17), which put the null outside every
 * later window; there the longer wait matters most.
 */
#define CARRIER_DWELL_MS 60
#define CARRIER_COARSE_DWELL_MS 150
#define CARRIER_COARSE_PASSES 2
#define CARRIER_PASSES 10
#define CARRIER_MIN_CONTRAST_DB 3.0f
#define CARRIER_MAX_STEPS 100
static int16_t center[CARRIER_PASSES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const int8_t NSteps[CARRIER_PASSES] = {(5000+5000)/100, (5000+5000)/100, 24, 24, 24, 24, 24, 24, 24, 24};
static const int16_t Delta[CARRIER_PASSES] = {100, 100, 10, 10, 10, 10, 1, 1, 1, 1};
static float32_t maxDBC = -1e9f;           // best reading of the pass so far
static int16_t maxDBC_parameter = 0;       // the offset that gave it
static int16_t passStartValue = 0;         // the offset the pass sweeps around
static float32_t passReadings[CARRIER_MAX_STEPS];
static int8_t nPassReadings = 0;
static bool bandAnyValid = false;          // some pass on this band stood out
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
 *   CARRCAL,PASS,<band>,<pass>,<I|Q>,<value kept>,<best dBc>,<valid 0|1>,<median dBc>
 *       end of a pass; valid 0: the best reading was less than
 *       CARRIER_MIN_CONTRAST_DB above the median, the offset is unchanged
 *   CARRCAL,DONE,<band>,<I>,<Q>,<dBc>,<valid 0|1>
 *       valid 0: no pass on the band stood out, its old offsets are restored
 *   CARRCAL,ABORT,<band>,<I restored>,<Q restored>
 *   CARRCAL,END
 * Even passes sweep I, odd passes Q. code/tools/carrier_cal_log.py records
 * and summarises the log.
 */
static void LogCarrierCal(const char *line){
    Debug(line);
    (void)line;
}

/**
 * @brief Calculate parameter value for given iteration and step
 * @param iter Iteration number (0 to CARRIER_PASSES-1)
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
    txcarrSM.vars.acquisitionDuration_ms = CARRIER_DWELL_MS;
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
    maxDBC = -1e9f;
    nPassReadings = 0;
    bandAnyValid = false;
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
    // Start from the band's stored offsets: the first pass sweeps I with Q
    // where it was, which on a calibrated radio is near its null
    // Go to find minimum loop
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_FIND_MINIMUM);
}

void ResetTXCarrierCalSettings(void){
    step = 0;
    iteration = 0;
    maxDBC = -1e9f;
    nPassReadings = 0;
    bandAnyValid = false;
}

/** The median of the pass's readings (0 if there are none) */
static float32_t PassMedian(void){
    if (nPassReadings == 0) return 0.0f;
    float32_t sorted[CARRIER_MAX_STEPS];
    std::copy(passReadings, passReadings + nPassReadings, sorted);
    std::sort(sorted, sorted + nPassReadings);
    return sorted[nPassReadings / 2];
}

void SetTXCarrierVals(int32_t band, float32_t value);
static float32_t maxDBC_save;

void AdjustTXCarrierCalSetting(void){
    // Have we completed all the steps in this iteration?
    if (step >= NSteps[iteration]){
        // Keep the best reading's offset if it stood out from the pass, else
        // the offset the pass swept around
        float32_t median = PassMedian();
        bool valid = (nPassReadings > 0) && (maxDBC - median >= CARRIER_MIN_CONTRAST_DB);
        int16_t kept = valid ? maxDBC_parameter : passStartValue;
        if (valid) bandAnyValid = true;
        if (iteration%2 == 0){
            ED.DCOffsetI[currentBand] = kept;
        } else {
            ED.DCOffsetQ[currentBand] = kept;
        }
        // The next pass on this offset sweeps around the value kept
        int8_t nextIndex = iteration + 2;
        if (nextIndex < CARRIER_PASSES)
            center[nextIndex] = kept;

        char line[96];
        snprintf(line, sizeof(line), "CARRCAL,PASS,%s,%d,%s,%d,%.2f,%d,%.2f", bands[currentBand].name,
                 (int)iteration, (iteration%2 == 0) ? "I" : "Q", (int)kept,
                 (double)maxDBC, valid ? 1 : 0, (double)median);
        LogCarrierCal(line);

        // Go to the next iteration
        step = 0;
        iteration++;
        maxDBC_save = maxDBC;
        maxDBC = -1e9f;
        nPassReadings = 0;
    }
    // Have we completed all the iterations and ready to go to the next band?
    if (iteration >= CARRIER_PASSES){
        // A band on which nothing stood out gets its old offsets back
        if (!bandAnyValid){
            ED.DCOffsetI[currentBand] = savedI;
            ED.DCOffsetQ[currentBand] = savedQ;
        }
        SetTXCarrierVals(currentBand, maxDBC_save);
        bandCompleted[currentBand] = true;
        char line[96];
        snprintf(line, sizeof(line), "CARRCAL,DONE,%s,%d,%d,%.2f,%d", bands[currentBand].name,
                 (int)ED.DCOffsetI[currentBand], (int)ED.DCOffsetQ[currentBand], (double)maxDBC_save,
                 bandAnyValid ? 1 : 0);
        LogCarrierCal(line);
        TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_MIN_EXIT);
        return;
    }
    // Each pass starts from the setting it sweeps around
    if (step == 0){
        int32_t b = ED.currentBand[ED.activeVFO];
        passStartValue = (iteration%2 == 0) ? ED.DCOffsetI[b] : ED.DCOffsetQ[b];
        maxDBC_parameter = passStartValue;
    }
    // Change the appropriate parameter
    SetOffset(iteration,step); 
    txcarrSM.vars.acquisitionDuration_ms = (iteration < CARRIER_COARSE_PASSES) ? CARRIER_COARSE_DWELL_MS
                                                                              : CARRIER_DWELL_MS;
    // Go to read data state after waiting for txcarrSM.vars.acquisitionDuration_ms
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_READ_DELTA);
}

void ReadTXCarrierDelta(void){
    // The first reading of a pass follows a large jump in the offset, and the
    // smoothed reading has not caught up with it
    bool ignore = (step == 0);
    float32_t reading = GetTXCarrierVals(ED.currentBand[ED.activeVFO]);
    if (!ignore){
        if (nPassReadings < CARRIER_MAX_STEPS) passReadings[nPassReadings++] = reading;
        if (reading > maxDBC){
            // The value of the carrier suppression
            maxDBC = reading;
            // The offset that delivered this carrier suppression
            maxDBC_parameter = GetNewVal(iteration, step);
        }
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
