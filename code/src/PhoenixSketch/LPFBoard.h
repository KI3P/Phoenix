#ifndef LPFBOARD_H
#define LPFBOARD_H
#include "SDT.h"

#define GET_BIT(byte, bit) (((byte) >> (bit)) & 1)
#define SET_BIT(byte, bit) ((byte) |= (1 << (bit)))
#define CLEAR_BIT(byte, bit) ((byte) &= ~(1 << (bit)))
#define TOGGLE_BIT(byte, bit) ((byte) ^= (1 << (bit)))


errno_t InitLPFBoard(void);
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
errno_t InitLPFControl(void);
void SelectAntenna(uint8_t antenna);
errno_t InitAntennaControl(void);
float32_t ReadSWR(void);
float32_t ReadForwardPower(void);
float32_t ReadReflectedPower(void);
errno_t InitSWRControl(void);

// For unit testing - access to internal register state
uint16_t GetLPFRegisterState(void);
void SetLPFRegisterState(uint16_t value);
void UpdateMCPRegisters(void);
uint8_t GetMCPAOld(void);
uint8_t GetMCPBOld(void);
void SetMCPAOld(uint8_t value);
void SetMCPBOld(uint8_t value);

#endif // LPFBOARD_H