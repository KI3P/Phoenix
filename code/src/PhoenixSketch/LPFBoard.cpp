#include "SDT.h"

static uint16_t LPF_register;

// For unit testing - functions to access the static register
uint16_t GetLPFRegisterState(void) {
    return LPF_register;
}

void SetLPFRegisterState(uint16_t value) {
    LPF_register = value;
}

#define LPF_GPA_state (LPF_register & 0xFF)          // Lower 8 bits
#define LPF_GPB_state ((LPF_register >> 8) & 0xFF)   // Upper 8 bits

#define SET_LPF_GPA(val) (LPF_register = (LPF_register & 0xFF00) | ((val) & 0xFF))
#define SET_LPF_GPB(val) (LPF_register = (LPF_register & 0x00FF) | (((val) & 0xFF) << 8))

#define SET_LPF_BAND(val) (LPF_register = (LPF_register & 0xFFF0) | ((val) & 0x0F))
#define SET_ANTENNA(val) (LPF_register = (LPF_register & 0b1111111111001111) | (((val) & 0b00000011) << 4))

void TXSelectBPF(void){
    SET_BIT(LPF_register, LPFTXBPFBIT);
    // And now actually change the hardware...

}

void TXBypassBPF(void){
    CLEAR_BIT(LPF_register, LPFTXBPFBIT);
    // And now actually change the hardware...

}

void RXSelectBPF(void){
    SET_BIT(LPF_register, LPFRXBPFBIT);
    // And now actually change the hardware...

}

void RXBypassBPF(void){
    CLEAR_BIT(LPF_register, LPFRXBPFBIT);
    // And now actually change the hardware...

}

errno_t InitBPFPathControl(void){
    return 0;
}

void SelectXVTR(void){
    // XVTR is active low
    CLEAR_BIT(LPF_register, LPFXVTRBIT);
    // And now actually change the hardware...

}

void BypassXVTR(void){
    SET_BIT(LPF_register, LPFXVTRBIT);
    // And now actually change the hardware...

}

errno_t InitXVTRControl(void){
    return 0;
}   

void Select100WPA(void){
    SET_BIT(LPF_register, LPF100WBIT);
}

void Bypass100WPA(void){
    CLEAR_BIT(LPF_register, LPF100WBIT);
}

errno_t Init100WPAControl(void){
    return 0;
}

void SelectLPFBand(int32_t band){
    switch (band){
        case BAND_160M:
            SET_LPF_BAND(LPF_BAND_160M);
            break;
        case BAND_80M:
            SET_LPF_BAND(LPF_BAND_80M);
            break;
        case BAND_60M:
            SET_LPF_BAND(LPF_BAND_60M);
            break;   
        case BAND_40M:
            SET_LPF_BAND(LPF_BAND_40M);
            break;
        case BAND_30M:
            SET_LPF_BAND(LPF_BAND_30M);
            break;
        case BAND_20M:
            SET_LPF_BAND(LPF_BAND_20M);
            break;
        case BAND_17M:
            SET_LPF_BAND(LPF_BAND_17M);
            break;
        case BAND_15M:
            SET_LPF_BAND(LPF_BAND_15M);
            break;
        case BAND_12M:
            SET_LPF_BAND(LPF_BAND_12M);
            break;
        case BAND_10M:
            SET_LPF_BAND(LPF_BAND_10M);
            break;
        case BAND_6M:
            SET_LPF_BAND(LPF_BAND_6M);
            break;
        default:
            SET_LPF_BAND(LPF_BAND_NF);
            break;
    }
    // And now actually change the hardware...

}

errno_t InitLPFControl(void){
    return 0;
}

void SelectAntenna(uint8_t antennaNum){
    if ((antennaNum >= 0) & (antennaNum <=3)){
        SET_ANTENNA(antennaNum);
    } else {
        Debug(String("V12 LPF Control: Invalid antenna selection! ") + String(antennaNum));
    }
    // And now actually change the hardware...

}

errno_t InitAntennaControl(void){
    return 0;
}

float32_t ReadSWR(void){
    return 0;
}

float32_t ReadForwardPower(void){
    return 0;
}

float32_t ReadReflectedPower(void){
    return 0;
}

errno_t InitSWRControl(void){
    return 0;
}

