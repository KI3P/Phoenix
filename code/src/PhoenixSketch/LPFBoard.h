#ifndef LPFBOARD_H
#define LPFBOARD_H
#include "SDT.h"

errno_t InitializeLPFBoard(void);
void TXSelectBPF(void);
void TXBypassBPF(void);
void RXSelectBPF(void);
void RXBypassBPF(void);
errno_t InitBPFPathControl(void);
void SelectXVTR(void);
void BypassXVTR(void);
errno_t InitXVTRControl(void);
void Select100WPA(void);
void Bypass100WPA(void);
errno_t Init100WPAControl(void);
void SelectLPFBand(int32_t band);
void SelectAntenna(uint8_t antenna);
errno_t InitAntennaControl(void);
float32_t ReadSWR(void);
float32_t ReadForwardPower(void);
float32_t ReadReflectedPower(void);
errno_t InitSWRControl(void);
uint8_t BandToBCD(int32_t band);
void UpdateMCPRegisters(void);

// For unit testing - access to internal register state
uint16_t GetLPFRegisterState(void);
void SetLPFRegisterState(uint16_t value);
uint16_t GetLPFMCPRegisters(void);
uint8_t GetLPFMCPAOld(void);
uint8_t GetLPFMCPBOld(void);
void SetLPFMCPAOld(uint8_t value);
void SetLPFMCPBOld(uint8_t value);

#endif // LPFBOARD_H