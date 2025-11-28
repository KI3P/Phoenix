#include "SDT.h"

// This file contains the entry and exit functions called upon changing states

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