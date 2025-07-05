//#include "ModeSm.h"
#include "SDT.h"

void ModeSSBReceiveEnter(void){    
    UpdateAudioIOState();
}

void ModeSSBReceiveExit(void){    
}

void ModeSSBTransmitEnter(void){
    UpdateAudioIOState();
}

void ModeSSBTransmitExit(void){
}

void ModeCWReceiveEnter(void){
    UpdateAudioIOState();
}

void ModeCWReceiveExit(void){    
}

void ModeCWTransmitMarkEnter(void){
    UpdateAudioIOState();
}

void ModeCWTransmitMarkExit(void){    
}

void ModeCWTransmitSpaceEnter(void){
    UpdateAudioIOState();
}

void ModeCWTransmitSpaceExit(void){
    UpdateAudioIOState();
}

void TriggerCalibrateFrequency(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_FREQUENCY);
}
void TriggerCalibrateExit(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_EXIT);
}
void TriggerCalibrateRXIQ(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_RX_IQ);
}
void TriggerCalibrateTXIQ(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_TX_IQ);
}
void TriggerCalibrateCWPA(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_CW_PA);
}
void TriggerCalibrateSSBPA(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_SSB_PA);
}

void CalibrateFrequencyEnter(void){
    UpdateAudioIOState();
}
void CalibrateFrequencyExit(void){}
void CalibrateTXIQEnter(void){
    UpdateAudioIOState();
}
void CalibrateTXIQExit(void){}
void CalibrateRXIQEnter(void){
    UpdateAudioIOState();
}
void CalibrateRXIQExit(void){}
void CalibrateCWPAEnter(void){
    UpdateAudioIOState();
}
void CalibrateCWPAExit(void){}
void CalibrateSSBPAEnter(void){
    UpdateAudioIOState();
}
void CalibrateSSBPAExit(void){}