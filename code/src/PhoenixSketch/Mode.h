#ifndef MODE_H
#define MODE_H

void ModeSSBReceiveEnter(void);
void ModeSSBReceiveExit(void);
void ModeSSBTransmitEnter(void);
void ModeSSBTransmitExit(void);
void ModeCWReceiveEnter(void);
void ModeCWReceiveExit(void);
void ModeCWTransmitMarkEnter(void);
void ModeCWTransmitMarkExit(void);
void ModeCWTransmitSpaceEnter(void);
void ModeCWTransmitSpaceExit(void);
void CalibrateFrequencyEnter(void);
void CalibrateFrequencyExit(void);
void CalibrateTXIQEnter(void);
void CalibrateTXIQExit(void);
void CalibrateRXIQEnter(void);
void CalibrateRXIQExit(void);
void CalibrateCWPAEnter(void);
void CalibrateCWPAExit(void);
void CalibrateSSBPAEnter(void);
void CalibrateSSBPAExit(void);
void TriggerCalibrateFrequency(void);
void TriggerCalibrateExit(void);
void TriggerCalibrateRXIQ(void);
void TriggerCalibrateTXIQ(void);
void TriggerCalibrateCWPA(void);
void TriggerCalibrateSSBPA(void);


#endif // MODE_H
