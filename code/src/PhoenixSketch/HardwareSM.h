#ifndef HARDWARESM_H
#define HARDWARESM_H
#include "SDT.h"

// RF Board state machine states
enum RFHardwareState {
    RFCWMark,
    RFCWSpace,
    RFReceive,
    RFTransmit,
    RFCalIQ,
    RFinvalid
};

enum TuneState {
    TuneReceive,
    TuneSSBTX,
    TuneCWTX
};

ModeSm_StateId GetRFHardwarePreviousState(void);
void UpdateRFHardwareState(void);
void HandleRFHardwareStateChange(RFHardwareState newState);
errno_t InitializeRFHardware(void);
void UpdateTuneState(void);
RFHardwareState GetRFHardwareState(void);

#endif //HARDWARESM_H

