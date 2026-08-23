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
#include "SDT.h"

// This file contains the entry and exit functions called upon changing states
// as well as functions used in guards

void UpdateHardwareState(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}

void ModeCWTransmitSpaceEnter(void){
    // Is the keyer still pressed?
    if (digitalRead(KEY1) == 0) {
        SetInterrupt(iKEY1_PRESSED);
    }
    if (digitalRead(KEY2) == 0) {
        SetInterrupt(iKEY2_PRESSED);
    }
    UpdateHardwareState();
}

bool IsTxAllowed(void) {
    return bands[ED.currentBand[ED.activeVFO]].band_type == HAM_BAND;
}

bool IsSSBTransmit(void) {
    return (modeSM.state_id == ModeSm_StateId_SSB_TRANSMIT) ||
           (modeSM.state_id == ModeSm_StateId_DIGITAL_TRANSMIT);
}

// The sample rate the radio was using before DIGITAL mode was entered, so it can
// be put back on the way out. SAMPLE_RATE_MAX+1 means "nothing saved".
#define NO_SAVED_SAMPLE_RATE (SAMPLE_RATE_MAX + 1)
static uint8_t preDigitalSampleRate = NO_SAVED_SAMPLE_RATE;

/**
 * Entry action for the DIGITAL_STATES composite.
 *
 * DIGITAL mode only works at 176.4 ksps: that is the rate at which the receive
 * chain's Fs/4 tap is exactly 44,100 Hz and one DSP block is exactly 512 samples,
 * which is what lets the USB audio endpoint run at its native 44.1 kHz with no
 * resampling. Force the rate here and remember what to restore.
 */
void EnterDigitalMode(void){
    preDigitalSampleRate = SampleRate;
    // ChangeSampleRate() is a no-op if we are already at 176.4 ksps
    ChangeSampleRate(SAMPLE_RATE_176K);
    USBAudioBegin();
}

/**
 * Exit action for the DIGITAL_STATES composite.
 *
 * Runs on every way out - the mode buttons, a CAT command, or a calibration
 * event dispatched at the NORMAL_STATES level - so the sample rate is always
 * restored no matter how DIGITAL mode is left.
 */
void ExitDigitalMode(void){
    USBAudioEnd();
    if (preDigitalSampleRate != NO_SAVED_SAMPLE_RATE){
        ChangeSampleRate(preDigitalSampleRate);
        preDigitalSampleRate = NO_SAVED_SAMPLE_RATE;
    }
}
