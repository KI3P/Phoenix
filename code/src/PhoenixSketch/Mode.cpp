//#include "ModeSm.h"
#include "SDT.h"

void ModeSSBReceiveEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}

void ModeSSBReceiveExit(void){    
}

void ModeSSBTransmitEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}

void ModeSSBTransmitExit(void){
}

void ModeCWReceiveEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}

void ModeCWReceiveExit(void){    
}

void ModeCWTransmitMarkEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}

void ModeCWTransmitMarkExit(void){    
}

void ModeCWTransmitSpaceEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}

void ModeCWTransmitSpaceExit(void){
    UpdateRFHardwareState();
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
    UpdateRFHardwareState();
    UpdateAudioIOState();
}
void CalibrateFrequencyExit(void){}
void CalibrateTXIQEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}
void CalibrateTXIQExit(void){}
void CalibrateRXIQEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}
void CalibrateRXIQExit(void){}
void CalibrateCWPAEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}
void CalibrateCWPAExit(void){}
void CalibrateSSBPAEnter(void){
    UpdateRFHardwareState();
    UpdateAudioIOState();
}
void CalibrateSSBPAExit(void){}