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
