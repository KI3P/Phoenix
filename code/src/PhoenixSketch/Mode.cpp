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
