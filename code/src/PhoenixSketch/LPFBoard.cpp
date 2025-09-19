#include "SDT.h"

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

#define LPF_BAND_NF   0b1111
#define LPF_BAND_6M   0b1010
#define LPF_BAND_10M  0b1001
#define LPF_BAND_12M  0b1000
#define LPF_BAND_15M  0b0111
#define LPF_BAND_17M  0b0110
#define LPF_BAND_20M  0b0101
#define LPF_BAND_30M  0b0100
#define LPF_BAND_40M  0b0011
#define LPF_BAND_60M  0b0000
#define LPF_BAND_80M  0b0010
#define LPF_BAND_160M 0b0001

// LPF_Register bit map:
// Bit   Pin    Description     Receive            Transmit
// 0     GPB0   Band I2C0       b                  b
// 1     GPB1   Band I2C1       b                  b
// 2     GPB2   Band I2C2       b                  b
// 3     GPB3   Band I2C3       b                  b
// 4     GPB4   Antenna I2C0    a                  a
// 5     GPB5   Antenna I2C1    a                  a
// 6     GPB6   XVTR_SEL        0                  1
// 7     GPB7   100W_PA_SEL     0                  0
// 8     GPA0   TX BPF          0                  1
// 9     GPA1   RX BPF          1                  0
// 10-15 GPA2-GPA7   Not used   0                  0

#define LPF_REGISTER_STARTUP_STATE 0x020F // receive mode, antenna 0, filter bypass

static Adafruit_MCP23X17 mcpLPF;
static bool LPFinitialized = false;
static errno_t LPFerrno = EFAIL;
static uint8_t mcpA_old = 0x00;
static uint8_t mcpB_old = 0x00;
//static AD7991 swrADC;

#define LPF_GPA_STATE (uint8_t)((hardwareRegister >> 8) & 0x00000003)   // Bits 8 & 9
#define LPF_GPB_STATE (uint8_t)(hardwareRegister & 0x000000FF)          // Lowest byte

#define SET_LPF_GPA(val) (hardwareRegister = (hardwareRegister & 0xFFFFFCFF) | (((uint32_t)val & 0x00000003) << 8));buffer_add()
#define SET_LPF_GPB(val) (hardwareRegister = (hardwareRegister & 0xFFFFFF00) | ((uint32_t)val  & 0x000000FF));buffer_add()
#define SET_LPF_BAND(val) (hardwareRegister = (hardwareRegister & 0xFFFFFFF0) | ((uint32_t)val & 0x0000000F));buffer_add()
#define SET_ANTENNA(val) (hardwareRegister = (hardwareRegister & 0xFFFFFFCF) | (((uint32_t)val & 0x00000003) << 4));buffer_add()

// For unit testing - functions to access the static register
uint16_t GetLPFRegisterState(void) {
    return (uint16_t)(hardwareRegister & 0x000003FF);
}

void SetLPFRegisterState(uint16_t value) {
    hardwareRegister = (hardwareRegister & 0xFFFFFC00) | (value & 0x03FF);
}

uint8_t GetMCPAOld(void) {
    return mcpA_old;
}

uint8_t GetMCPBOld(void) {
    return mcpB_old;
}

void SetMCPAOld(uint8_t value) {
    mcpA_old = value;
}

void SetMCPBOld(uint8_t value) {
    mcpB_old = value;
}
// end of unit testing section

errno_t InitLPFBoardMCP(void){
    if (LPFinitialized) return LPFerrno;

    /******************************************************************
     * Set up the V12 LPF which is connected via the BANDS connector *
     ******************************************************************/
    SET_LPF_BAND(ED.currentBand[ED.activeVFO]);
    SET_ANTENNA(ED.antennaSelection[ED.currentBand[ED.activeVFO]]);;
    if (mcpLPF.begin_I2C(V12_LPF_MCP23017_ADDR,&Wire2)){
        Debug("Initializing V12 LPF board");
        mcpLPF.enableAddrPins();
        // Set all pins to be outputs
        for (int i=0;i<16;i++){
            mcpLPF.pinMode(i, OUTPUT);
        }
        mcpLPF.writeGPIOA(LPF_GPA_STATE); 
        mcpLPF.writeGPIOB(LPF_GPB_STATE);
        mcpA_old = LPF_GPA_STATE;
        mcpB_old = LPF_GPB_STATE;
        Debug("Startup LPF GPB state: "+String(LPF_GPB_STATE,BIN));
        bit_results.V12_LPF_I2C_present = true;
        LPFerrno = ESUCCESS;
    } else {
        Debug("LPF MCP23017 not found at 0x"+String(V12_LPF_MCP23017_ADDR,HEX));
        bit_results.V12_LPF_I2C_present = false;
        LPFerrno = ENOI2C;
    }
    LPFinitialized = true;
    return LPFerrno;
}

void UpdateMCPRegisters(void){
    if (mcpA_old != LPF_GPA_STATE){
        mcpLPF.writeGPIOA(LPF_GPA_STATE); 
        mcpA_old = LPF_GPA_STATE;
    }
    if (mcpB_old != LPF_GPB_STATE){
        mcpLPF.writeGPIOB(LPF_GPB_STATE); 
        mcpB_old = LPF_GPB_STATE;
    }
}

void TXSelectBPF(void){
    SET_BIT(hardwareRegister, TXBPFBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

void TXBypassBPF(void){
    CLEAR_BIT(hardwareRegister, TXBPFBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

void RXSelectBPF(void){
    SET_BIT(hardwareRegister, RXBPFBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

void RXBypassBPF(void){
    CLEAR_BIT(hardwareRegister, RXBPFBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

errno_t InitBPFPathControl(void){
    return InitLPFBoardMCP();
}

void SelectXVTR(void){
    // XVTR is active low
    CLEAR_BIT(hardwareRegister, XVTRBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

void BypassXVTR(void){
    SET_BIT(hardwareRegister, XVTRBIT);
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

errno_t InitXVTRControl(void){
    return InitLPFBoardMCP();
}   

void Select100WPA(void){
    SET_BIT(hardwareRegister, PA100WBIT);
    UpdateMCPRegisters();
}

void Bypass100WPA(void){
    CLEAR_BIT(hardwareRegister, PA100WBIT);
    UpdateMCPRegisters();
}

errno_t Init100WPAControl(void){
    return InitLPFBoardMCP();
}

void SelectLPFBand(int32_t band){
    if (band == -1){
        // We are in the case where the selected frequency is outside a ham band
        // We want to maintain FCC compliance on harmonic strength. So select the 
        // LPF for the nearest band that is higher than our current frequency.
        if (ED.centerFreq_Hz[ED.activeVFO] < bands[FIRST_BAND].fBandLow_Hz){
            band = FIRST_BAND;
        } else {
            for(uint8_t i = FIRST_BAND; i <= LAST_BAND-1; i++){
                if((ED.centerFreq_Hz[ED.activeVFO] > bands[i].fBandHigh_Hz) && 
                   (ED.centerFreq_Hz[ED.activeVFO] < bands[i+1].fBandLow_Hz)){
                    band = i;
                    break;
                }
            }
        }
        if (band == -1){
            // This is the case where the frequency is higher than the highest band
            band = LAST_BAND + 10; // force it to pick no filter. You're on your own now
        }
    }

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
    UpdateMCPRegisters();
}

errno_t InitializeLPFBoard(void){
    return InitLPFBoardMCP();
}

void SelectAntenna(uint8_t antennaNum){
    if ((antennaNum >= 0) & (antennaNum <=3)){
        SET_ANTENNA(antennaNum);
    } else {
        Debug(String("V12 LPF Control: Invalid antenna selection! ") + String(antennaNum));
    }
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

errno_t InitAntennaControl(void){
    return InitLPFBoardMCP();
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

    /*if (!swrADC.begin(AD7991_I2C_ADDR1,&Wire2)){
        bit_results.V12_LPF_AD7991_present = false;
        Debug("AD7991 not found at 0x"+String(AD7991_I2C_ADDR1,HEX));

        if (!swrADC.begin(AD7991_I2C_ADDR2,&Wire2)){
            bit_results.V12_LPF_AD7991_present = true;
            bit_results.AD7991_I2C_ADDR = AD7991_I2C_ADDR2;
            Debug("AD7991 found at alternative 0x"+String(AD7991_I2C_ADDR2,HEX));
            return ESUCCESS;
        }
        return ENOI2C;
    }*/
    return ESUCCESS;
}

