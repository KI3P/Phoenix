#ifndef HARDWARESM_H
#define HARDWARESM_H
#include "SDT.h"

// RF Board state machine functions
enum RFBoardState {
    RFBoardCWMark,
    RFBoardCWSpace,
    RFBoardReceive,
    RFBoardSSBTransmit,
    RFBoardCalIQ
};

ModeSm_StateId GetRFBoardPreviousState(void);
void UpdateRFBoardState(void);
void HandleRFBoardStateChange(RFBoardState newState);

#endif //HARDWARESM_H

