#include "SDT.h"
#include "AD7991.h"

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
static AD7991 swrADC;

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

uint8_t BandToBCD(int32_t band){
    switch (band){
        case BAND_160M:
            return BAND_160M_BCD;
        case BAND_80M:
            return BAND_80M_BCD;
        case BAND_60M:
            return BAND_60M_BCD;
        case BAND_40M:
            return BAND_40M_BCD;
        case BAND_30M:
            return BAND_30M_BCD;
        case BAND_20M:
            return BAND_20M_BCD;
        case BAND_17M:
            return BAND_17M_BCD;
        case BAND_15M:
            return BAND_15M_BCD;
        case BAND_12M:
            return BAND_12M_BCD;
        case BAND_10M:
            return BAND_10M_BCD;
        case BAND_6M:
            return BAND_6M_BCD;
        default:
            return BAND_NF_BCD;
    }
}

errno_t InitLPFBoardMCP(void){
    if (LPFinitialized) return LPFerrno;

    /******************************************************************
     * Set up the LPF which is connected via the BANDS connector *
     ******************************************************************/
    // Prepare the register values for receive mode
    SET_LPF_BAND(BandToBCD(ED.currentBand[ED.activeVFO]));
    SET_ANTENNA(ED.antennaSelection[ED.currentBand[ED.activeVFO]]);
    CLEAR_BIT(hardwareRegister,PA100WBIT);
    CLEAR_BIT(hardwareRegister,RXTXBIT);
    CLEAR_BIT(hardwareRegister,XVTRBIT);
    CLEAR_BIT(hardwareRegister,TXBPFBIT);
    SET_BIT(hardwareRegister,RXBPFBIT);

    if (mcpLPF.begin_I2C(V12_LPF_MCP23017_ADDR,&Wire2)){
        Debug("Initializing LPF board");
        mcpLPF.enableAddrPins();
        // Set all pins to be outputs
        for (int i=0;i<16;i++){
            mcpLPF.pinMode(i, OUTPUT);
        }
        mcpA_old = LPF_GPA_STATE;
        mcpB_old = LPF_GPB_STATE;
        mcpLPF.writeGPIOA(LPF_GPA_STATE); 
        mcpLPF.writeGPIOB(LPF_GPB_STATE);
        Debug("Startup LPF GPA state: "+String(LPF_GPA_STATE,BIN));
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
    SET_LPF_BAND(BandToBCD(band));
    // And now actually change the hardware...
    UpdateMCPRegisters();
}

errno_t InitializeLPFBoard(void){
    errno_t val = InitSWRControl();
    val += InitLPFBoardMCP();
    return val;
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

static float32_t adcF_sRawOld;
static float32_t adcR_sRawOld;
static float32_t adcF_sRaw;
static float32_t adcR_sRaw;
static float32_t Pf_dBm;
static float32_t Pr_dBm;
static float32_t Pf_W;
static float32_t Pr_W;
static float32_t swr;

#define VREF_MV 4096     // the reference voltage on your board
#define PAD_ATTENUATION_DB 26 // attenuation of the pad
#define COUPLER_ATTENUATION_DB 20 // attenuation of the binocular toroid coupler

void read_SWR() {
  // Step 1. Measure the peak forward and Reverse voltages
  adcF_sRaw = (float32_t)swrADC.readADCsingle(0);
  adcR_sRaw = (float32_t)swrADC.readADCsingle(1);

  adcF_sRaw = 0.1 * adcF_sRaw + 0.9 * adcF_sRawOld;  //Running average
  adcR_sRaw = 0.1 * adcR_sRaw + 0.9 * adcR_sRawOld;
  adcF_sRawOld = adcF_sRaw;
  adcR_sRawOld = adcR_sRaw;

  //Convert ADC reading to mV
  adcF_sRaw = adcF_sRaw * VREF_MV / 4096.;
  adcR_sRaw = adcR_sRaw * VREF_MV / 4096.;

  Pf_dBm = adcF_sRaw/(25 + ED.SWR_F_SlopeAdj[ED.currentBand[ED.activeVFO]]) - 84 + ED.SWR_F_Offset[ED.currentBand[ED.activeVFO]] + PAD_ATTENUATION_DB + COUPLER_ATTENUATION_DB;
  Pr_dBm = adcR_sRaw/(25 + ED.SWR_R_SlopeAdj[ED.currentBand[ED.activeVFO]]) - 84 + ED.SWR_R_Offset[ED.currentBand[ED.activeVFO]] + PAD_ATTENUATION_DB + COUPLER_ATTENUATION_DB;

  // Convert to input voltage squared as read by ADC converted to before attenuation
  Pf_W = (float32_t)pow(10,Pf_dBm/10)/1000;
  Pr_W = (float32_t)pow(10,Pr_dBm/10)/1000;

  float32_t A = pow(Pr_W / Pf_W, 0.5);
  swr = (1.0 + A) / (1.0 - A);
}

float32_t ReadSWR(void){
    return swr;
}

float32_t ReadForwardPower(void){
    return Pf_W;
}

float32_t ReadReflectedPower(void){
    return Pr_W;
}


errno_t InitSWRControl(void){

    if (!swrADC.begin(AD7991_I2C_ADDR1,&Wire2)){
        bit_results.V12_LPF_AD7991_present = false;
        Debug("AD7991 not found at 0x"+String(AD7991_I2C_ADDR1,HEX));

        if (!swrADC.begin(AD7991_I2C_ADDR2,&Wire2)){
            bit_results.V12_LPF_AD7991_present = true;
            bit_results.AD7991_I2C_ADDR = AD7991_I2C_ADDR2;
            Debug("AD7991 found at alternative 0x"+String(AD7991_I2C_ADDR2,HEX));
            return ESUCCESS;
        }
        return ENOI2C;
    }
    return ESUCCESS;
}

