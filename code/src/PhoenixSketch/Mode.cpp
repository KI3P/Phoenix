//#include "ModeSm.h"
#include "SDT.h"

void ModeSSBReceiveEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}

void ModeSSBReceiveExit(void){    
}

void ModeSSBTransmitEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}

void ModeSSBTransmitExit(void){
}

void ModeCWReceiveEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}

void ModeCWReceiveExit(void){    
}

void ModeCWTransmitMarkEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}

void ModeCWTransmitMarkExit(void){    
}

void ModeCWTransmitSpaceEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}

void ModeCWTransmitSpaceExit(void){
    UpdateRFBoardState();
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
    UpdateRFBoardState();
    UpdateAudioIOState();
}
void CalibrateFrequencyExit(void){}
void CalibrateTXIQEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}
void CalibrateTXIQExit(void){}
void CalibrateRXIQEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}
void CalibrateRXIQExit(void){}
void CalibrateCWPAEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}
void CalibrateCWPAExit(void){}
void CalibrateSSBPAEnter(void){
    UpdateRFBoardState();
    UpdateAudioIOState();
}
void CalibrateSSBPAExit(void){}