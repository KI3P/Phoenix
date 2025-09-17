#ifndef LPFBOARD_H
#define LPFBOARD_H
#include "SDT.h"

#define GET_BIT(byte, bit) (((byte) >> (bit)) & 1)
#define SET_BIT(byte, bit) ((byte) |= (1 << (bit)))
#define CLEAR_BIT(byte, bit) ((byte) &= ~(1 << (bit)))
#define TOGGLE_BIT(byte, bit) ((byte) ^= (1 << (bit)))

#define LPFBAND0BIT 0
#define LPFBAND1BIT 1
#define LPFBAND2BIT 2
#define LPFBAND3BIT 3
#define LPFANT0BIT  4
#define LPFANT1BIT  5
#define LPFXVTRBIT  6
#define LPF100WBIT  7
#define LPFTXBPFBIT 8
#define LPFRXBPFBIT 9

#define LPF_BAND_NF 0b1111
#define LPF_BAND_6M 0b1010
#define LPF_BAND_10M 0b1001
#define LPF_BAND_12M 0b1000
#define LPF_BAND_15M 0b0111
#define LPF_BAND_17M 0b0110
#define LPF_BAND_20M 0b0101
#define LPF_BAND_30M 0b0100
#define LPF_BAND_40M 0b0011
#define LPF_BAND_60M 0b0000
#define LPF_BAND_80M 0b0010
#define LPF_BAND_160M 0b0001

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

#endif // LPFBOARD_H