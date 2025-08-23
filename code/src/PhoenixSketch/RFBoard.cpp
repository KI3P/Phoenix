#include "SDT.h"
#include "si5351.h"

///////////////////////////////////////////////////////////////////////////////
// Variables that are only visible from within this file
///////////////////////////////////////////////////////////////////////////////

//#ifndef TESTMODE
static Adafruit_MCP23X17 mcp;
//#endif
#define MAX_ATTENUATION_VAL_DBx2 63
#define MIN_ATTENUATION_VAL_DBx2 0

//static bool failed;
static uint8_t GPB_state;
static uint8_t GPA_state;
static bool boardInitialized = false;
static errno_t error_state;
static ModeSm_StateId previousRadioState = ModeSm_StateId_ROOT;
static RFBoardState rfBoardState = RFBoardReceive;
static RFBoardState oldrfBoardState = RFBoardReceive;

// VFO related
Si5351 si5351;
#define SI5351_DRIVE_CURRENT SI5351_DRIVE_2MA
#define SI5351_LOAD_CAPACITANCE SI5351_CRYSTAL_LOAD_8PF
#define Si_5351_crystal 25000000L
static int32_t multiple, oldMultiple;
static bool cwState = false;
#define XMIT_SSB 1
#define XMIT_CW  0
static bool modulationState = XMIT_SSB;
#define CAL_OFF 0
#define CAL_ON  1
static bool calFeedbackState = CAL_OFF;
#define RX  0
#define TX  1
static bool rxtxState = CAL_OFF;

///////////////////////////////////////////////////////////////////////////////
// Functions that are only visible from within this file
///////////////////////////////////////////////////////////////////////////////

/**
 * PRIVATE: Initialize the I2C connection for the RF board.
 * 
 * PRIVATE: Initialize the I2C connection to the MCP23017 chip on the RF
 * board. This is invoked by the InitRXAttenuation and InitTXAttenuation functions, 
 * so it is made private to prevent it being invoked elsewhere.
 * 
 * Returns boolean true if the chip was found. Returns boolean false if it was not. 
 */
static bool InitI2C(){
    return true;
}

/**
 * PRIVATE: Write the value of the GPIOA register to the MCP23017 chip.
 * 
 * Returns boolean true if the write was successful. Returns boolean false if it was not. 
 */
static bool WriteGPIOARegister(){
    return true;
}

/**
 * PRIVATE: Write the value of the GPIOB register to the MCP23017 chip.
 * 
 * Returns boolean true if the write was successful. Returns boolean false if it was not. 
 */
static bool WriteGPIOBRegister(){
    return true;
}

/** 
 * PRIVATE: Checks that the specified value is in the permitted range.
 * 
 * Returns a corrected value. 
 */
static int32_t check_range(int32_t val){
    if (val > MAX_ATTENUATION_VAL_DBx2) val = MAX_ATTENUATION_VAL_DBx2;
    if (val < MIN_ATTENUATION_VAL_DBx2) val = MIN_ATTENUATION_VAL_DBx2;
    return val;
}

/**
 * PRIVATE:  Set the attenuation of an attenuator to the provided value. The attenuation 
 * must be specified in units of 2x dB. i.e., if you need 30 dB of attenuation, this value 
 * would be 60.
 *
 * @param Attenuation_dBx2 The attenuation in units of 2x dB. [0 to 63]
 * @param GPIO_register A pointer to the register for this attenuator
 * @param RegisterWriteFunc The function that writes the register to the attenuator
 * 
 * @return Error code. ESUCCESS if no failure. EGPIOWRITEFAIL if unable to write to GPIO bank.
 *  
 */
static errno_t SetAttenuator(int32_t Attenuation_dBx2, uint8_t *GPIO_register, bool (*RegisterWriteFunc)(void)){
    *GPIO_register = (uint8_t)check_range(Attenuation_dBx2);
    if (RegisterWriteFunc()){
        error_state = ESUCCESS;
    } else {
        error_state = EGPIOWRITEFAIL;
    }
    return error_state;
}

/**
 * PRIVATE: Initialize the I2C connection to an attenuator if not already done.
 * Then set the attenuation to the provided value.
 *
 * @param Attenuation_dB The attenuation in units of dB.
 * @param SetAtten The function for writing to the attenuator
 * 
 * @return Error code. ESUCCESS if no failure. ENOI2C if I2C connection failed. 
 *        EGPIOWRITEFAIL if unable to write to GPIO bank.
 * 
 */
static errno_t AttenuatorCreate(float32_t Attenuation_dB, errno_t (*SetAtten)(float32_t)){
    if (!boardInitialized){
        boardInitialized = InitI2C();
    }
    if (!boardInitialized) {
        error_state = ENOI2C;
    } else {
        error_state = SetAtten(Attenuation_dB);
    }
    return error_state;
}

/**
 * PRIVATE: Initialize the I2C connection to the receive attenuator if not already done.
 * Then set the attenuation to the provided value.
 *
 * @param rxAttenuation_dB The RX attenuation in units of dB. This value will be
 *                         rounded to the nearest 0.5 dB. Valid range: 0 to 31.5
 * 
 * @return Error code. ESUCCESS if no failure. ENOI2C if I2C connection failed. 
 *        EGPIOWRITEFAIL if unable to write to GPIO bank.
 * 
 */
errno_t RXAttenuatorCreate(float32_t rxAttenuation_dB){
    return AttenuatorCreate(rxAttenuation_dB, SetRXAttenuation);
}

/**
 * PRIVATE: Initialize the I2C connection to the transmit attenuator if not already done.
 * Then set the attenuation to the provided value.
 *
 * @param txAttenuation_dB The TX attenuation in units of dB. This value will be
 *                         rounded to the nearest 0.5 dB. Valid range: 0 to 31.5
 * 
 * @return Error code. ESUCCESS if no failure. ENOI2C if I2C connection failed. 
 *        EGPIOWRITEFAIL if unable to write to GPIO bank.
 * 
 */
errno_t TXAttenuatorCreate(float32_t txAttenuation_dB){
    return AttenuatorCreate(txAttenuation_dB, SetTXAttenuation);
}

///////////////////////////////////////////////////////////////////////////////
// Functions that are globally visible
///////////////////////////////////////////////////////////////////////////////

/**
 * Initialize the RX and TX attenuators. Sets up the I2C connection to the I2C
 * to GPIO chip that controls the attenuators.
 * 
 * @return Error code. ESUCCESS if no failure. ENOI2C if I2C connection failed. 
 *        EGPIOWRITEFAIL if unable to write to GPIO bank.
 */
errno_t InitAttenuation(void){
    errno_t err = TXAttenuatorCreate(EEPROMData.XAttenSSB[EEPROMData.currentBand]);
    if (err != ESUCCESS){
        // it might be somehow possible that writing to the TX attenuator would fail
        // while writing to the RX attenuator would succeed. If this is the case,
        // handle this situation here.
    }
    return RXAttenuatorCreate(EEPROMData.RAtten[EEPROMData.currentBand]);
}

/**
 * Returns the attenuation setting of the RX attenuator. 
 * 
 *  @return rxAttenuation_dB The RX attenuation in units of dB.
 * 
 */
float32_t GetRXAttenuation(){
    return ((float32_t)GPA_state)/2.0;
}

/**
 * Returns the attenuation setting of the TX attenuator. 
 * 
 *  @return txAttenuation_dB The RX attenuation in units of dB.
 * 
 */
float32_t GetTXAttenuation(){
    return ((float32_t)GPB_state)/2.0;
}

/**
 * Set the attenuation of the RX attenuator to the provided value. The RX attenuation must
 * be specified in units of dB. The attenuation is rounded to the nearest 0.5 dB. It only
 * performs a write over I2C if the attenuation level has changed from the previous state.
 *
 * @param rxAttenuation_dB The RX attenuation in units of dB. Valid range: 0 to 31.5
 *
 * @return Error code. ESUCCESS if no failure. EGPIOWRITEFAIL if unable to write to GPIO bank.
 *  
 */
errno_t SetRXAttenuation(float32_t rxAttenuation_dB){
    // Only do this if the attenuation value has changed from the current value. This avoids
    // unecessary I2C writes that slow things down and generates noise
    uint8_t newRegisterValue = (uint8_t)check_range((int32_t)round(2*rxAttenuation_dB));
    if (newRegisterValue == GPA_state){
        return ESUCCESS;
    } else {
        return SetAttenuator((int32_t)round(2*rxAttenuation_dB), &GPA_state, WriteGPIOARegister);
    }
}

/**
 * Set the attenuation of the TX attenuator to the provided value. The TX attenuation must
 * be specified in units of dB. The attenuation is rounded to the nearest 0.5 dB. It only
 * performs a write over I2C if the attenuation level has changed from the previous state.
 *
 * @param txAttenuation_dB The TX attenuation in units of dB. Valid range: 0 to 31.5
 * 
 * @return Error code. ESUCCESS if no failure. EGPIOWRITEFAIL if unable to write to GPIO bank.
 *  
 */
errno_t SetTXAttenuation(float32_t txAttenuation_dB){
    // Only do this if the attenuation value has changed from the current value. This avoids
    // unecessary I2C writes that slow things down and generates noise
    uint8_t newRegisterValue = (uint8_t)check_range((int32_t)round(2*txAttenuation_dB));
    if (newRegisterValue == GPB_state){
        return ESUCCESS;
    } else {
        return SetAttenuator((int32_t)round(2*txAttenuation_dB), &GPB_state, WriteGPIOBRegister);
    }
}

/**
 * Set the frequency of the Si5351
 * 
 * @param centerFreq_Hz THe desired frequency in Hz
 */
void SetFreq(int64_t centerFreq_Hz){
    return SetSSBVFOFrequency(centerFreq_Hz*SI5351_FREQ_MULT);
}

// SSB VFO Control Functions

/**
 * Set the power of the VFO used to drive the SSB portion of the radio.
 * 
 * @param power Expect one of the SI5351_DRIVE_?MA parameters
 */
void SetSSBVFOPower(int32_t power){
    si5351.drive_strength(SI5351_CLK0, (si5351_drive)power);
    si5351.drive_strength(SI5351_CLK1, (si5351_drive)power);
}

/**
 * Initialize the SSB VFO. This is done once at startup and is invoked by InitVFOs().
 * Set the power and configure the PLL source, does not set the frequency.
 */
errno_t InitSSBVFO(void){
    // Set driveCurrentSSB_mA to appropriate value
    SetSSBVFOPower( SI5351_DRIVE_CURRENT );
    si5351.set_ms_source(SI5351_CLK0, SI5351_PLLA);
    return ESUCCESS;
}

/**
 * Calculate the even divisor used in the configuration of the PLL for the SSB
 * VFO frequency.
 * 
 * @param freq2_Hz The desired VFO frequency in units of Hz.
 */
int32_t EvenDivisor(int64_t freq2_Hz) {
    int32_t mult = 1;
    // Use phase method of time delay described by
    // https://tj-lab.org/2020/08/27/si5351単体で3mhz以下の直交信号を出力する/
    // for below 3.2MHz the ~limit of PLLA @ 400MHz for a 126 divider
    if (freq2_Hz < 100000)
        mult = 8192;

    if ((freq2_Hz >= 100000) && (freq2_Hz < 200000))   // PLLA 409.6 MHz to 819.2 MHz
        mult = 4096;
    
    if ((freq2_Hz >= 200000) && (freq2_Hz < 400000))   //   ""          "" 
        mult = 2048;

    if ((freq2_Hz >= 400000) && (freq2_Hz < 800000))   //    ""          ""
        mult = 1024;

    if ((freq2_Hz >= 800000) && (freq2_Hz < 1600000))   //    ""         ""
        mult = 512;

    if ((freq2_Hz >= 1600000) && (freq2_Hz < 3200000))   //    ""        ""
        mult = 256;

    // Above 3.2 MHz
    if ((freq2_Hz >= 3200000) && (freq2_Hz < 6850000))   // 403.2 MHz - 863.1 MHz
        mult = 126;

    if ((freq2_Hz >= 6850000) && (freq2_Hz < 9500000))
        mult = 88;

    if ((freq2_Hz >= 9500000) && (freq2_Hz < 13600000))
        mult = 64;

    if ((freq2_Hz >= 13600000) && (freq2_Hz < 17500000))
        mult = 44;

    if ((freq2_Hz >= 17500000) && (freq2_Hz < 25000000))
        mult = 34;

    if ((freq2_Hz >= 25000000) && (freq2_Hz < 36000000))
        mult = 24;

    if ((freq2_Hz >= 36000000) && (freq2_Hz < 45000000))
        mult = 18;

    if ((freq2_Hz >= 45000000) && (freq2_Hz < 60000000))
        mult = 14;

    if ((freq2_Hz >= 60000000) && (freq2_Hz < 80000000))
        mult = 10;

    if ((freq2_Hz >= 80000000) && (freq2_Hz < 100000000))
        mult = 8;

    if ((freq2_Hz >= 100000000) && (freq2_Hz < 150000000))
        mult = 6;

    if ((freq2_Hz >= 150000000) && (freq2_Hz < 220000000))
        mult = 4;

    if(freq2_Hz>=220000000)
        mult = 2;

    return mult;
}

/**
 * Set the CLK0 and CLK1 outputs as quadrature outputs at the specified frequency.
 * 
 * @param frequency_dHz The desired clock frequency in (Hz * 100)
 */
void SetSSBVFOFrequency(int64_t frequency_dHz){
    int64_t Clk1SetFreq = frequency_dHz;
    multiple = EvenDivisor(Clk1SetFreq / SI5351_FREQ_MULT);
    uint64_t pll_freq = Clk1SetFreq * multiple;
    uint64_t freq = pll_freq / multiple;
    
    if ( multiple == oldMultiple) {               // Still within the same multiple range 
        si5351.set_pll(pll_freq, SI5351_PLLA);    // just change PLLA on each frequency change of encoder
                                                  // this minimizes I2C data for each frequency change within a 
                                                  // multiple range
    } else { 
        if ( multiple <= 126) {                                 // this the library setting of phase for freqs
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK0);  // greater than 3.2MHz where multiple is <= 126
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK1);   // set both clocks to new frequency
            si5351.set_phase(SI5351_CLK0, 0);                      // CLK0 phase = 0 
            si5351.set_phase(SI5351_CLK1, multiple);               // Clk1 phase = multiple for 90 degrees(digital delay)
            si5351.pll_reset(SI5351_PLLA);                         // reset PLLA to align outputs 
            si5351.output_enable(SI5351_CLK0, 1);                  // set outputs on or off
            si5351.output_enable(SI5351_CLK1, 1);
            //si5351.output_enable(SI5351_CLK2, 0);
        }
        else {        // this is the timed delay technique for frequencies below 3.2MHz as detailed in 
                    // https://tj-lab.org/2020/08/27/si5351単体で3mhz以下の直交信号を出力する/
            cli();                //__disable_irq(); or __enable_irq();     // or cli()/sei() pair; needed to get accurate timing??
            //si5351.output_enable(SI5351_CLK0, 0);  // optional switch off clocks if audio effects are generated
            //si5351.output_enable(SI5351_CLK1, 0);  //  with the change of multiple below 3.2MHz
            si5351.set_freq_manual((freq - 400ULL), pll_freq, SI5351_CLK0);  // set up frequencies of CLK 0/1 4 Hz low
            si5351.set_freq_manual((freq - 400ULL), pll_freq, SI5351_CLK1);  // as per TJ-Labs article 
            si5351.set_phase(SI5351_CLK0, 0);                          // set phase registers to 0 just to be sure
            si5351.set_phase(SI5351_CLK1, 0);                          
            si5351.pll_reset(SI5351_PLLA);                             // align both clockss in phase
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK0);       // set clock 0  to required freq
            //delayNanoseconds(625000000);       // 62.5 * 1000000      //configured for a 62.5 mSec delay at 4 Hz difference 
            delayMicroseconds(58500);                       //nominally 62500 this figure can be adjusted for a more exact delay which is phase
            si5351.set_freq_manual(freq, pll_freq, SI5351_CLK1);       // set CLK 1 to the required freq after delay
            sei();
            si5351.output_enable(SI5351_CLK0, 1);                      // switch them on to be sure
            si5351.output_enable(SI5351_CLK1, 1);                      //    ""        ""
            //si5351.output_enable(SI5351_CLK2, 0);
            
        }
    }
    oldMultiple = multiple; 
}

/**
 * Enable the SSB VFO I & Q outputs (CLK0 & CLK1)
 */
void EnableSSBVFOOutput(void){
    si5351.output_enable(SI5351_CLK0, 1);
    si5351.output_enable(SI5351_CLK1, 1);
}

/**
 * Disable the SSB VFO I & Q outputs (CLK0 & CLK1)
 */
void DisableSSBVFOOutput(void){
    si5351.output_enable(SI5351_CLK0, 0);
    si5351.output_enable(SI5351_CLK1, 0);
}

// CW VFO Control Functions
void SetCWVFOFrequency(int64_t frequency_dHz){} // frequency in Hz * 100

/**
 * Enable the CW VFO output (CLK2)
 */
void EnableCWVFOOutput(void){
    si5351.output_enable(SI5351_CLK2, 1);
}

/**
 * Disable the CW VFO output (CLK2)
 */
void DisableCWVFOOutput(void){
    si5351.output_enable(SI5351_CLK2, 0);
}

/**
 * Set the power of the VFO used to drive the CW portion of the radio.
 * 
 * @param power Expect one of the SI5351_DRIVE_?MA parameters
 */
void SetCWVFOPower(int32_t power){
    si5351.drive_strength(SI5351_CLK2, (si5351_drive)power);
}

/**
 * Initialize the CW VFO. This is done once at startup and is invoked by InitVFOs().
 * Set the power and configure the PLL source, does not set the frequency. CW VFO
 * output is off after initialization.
 */
errno_t InitCWVFO(void){
    SetCWVFOPower( SI5351_DRIVE_CURRENT );
    si5351.set_ms_source(SI5351_CLK1, SI5351_PLLA);
    pinMode(CW_ON_OFF, OUTPUT);
    digitalWrite(CW_ON_OFF, 0);
    cwState = false;
    return ESUCCESS;
}

/**
 * Turn on CW output
 */
void CWon(void){
    if (!cwState) digitalWrite(CW_ON_OFF, 1);
    cwState = true;
}

/**
 * Turn off CW output
 */
void CWoff(void){
    if (cwState) digitalWrite(CW_ON_OFF, 0);
    cwState = false;
}

/**
 * Set up the communication with the Si5351, initialize its capacitance and crystal
 * settings, and initialize the clock signals
 */
errno_t InitVFOs(void){
    si5351.reset();
    si5351.init(SI5351_LOAD_CAPACITANCE, Si_5351_crystal, EEPROMData.freqCorrectionFactor);
    MyDelay(100L);
    if (!si5351.init(SI5351_LOAD_CAPACITANCE, Si_5351_crystal, EEPROMData.freqCorrectionFactor)) {
        bit_results.RF_Si5351_present = false;
        Debug("Initialize si5351 failed!");
        return EFAIL;
    } else {
        bit_results.RF_Si5351_present = true;
    }
    MyDelay(100L);

    InitSSBVFO();
    InitCWVFO();
    return ESUCCESS;
}

// Transmit Modulation Control

/**
 * Set up the transmit modulation control. The transmit modulation type will
 * be SSB after this step.
 */
errno_t InitTXModulation(void){
    pinMode(XMIT_MODE, OUTPUT);
    digitalWrite(XMIT_MODE, XMIT_SSB);
    modulationState = XMIT_SSB;
    return ESUCCESS;
}

/**
 * Select SSB modulation circuit. Only change the digital control line if
 * the modulation type is changing.
 */
void SelectTXSSBModulation(void){
    if (modulationState == XMIT_CW) digitalWrite(XMIT_MODE, XMIT_SSB);
    modulationState = XMIT_SSB;
}

/**
 * Select CW modulation circuit. Only change the digital control line if
 * the modulation type is changing.
 */
void SelectTXCWModulation(void){
    if (modulationState == XMIT_SSB) digitalWrite(XMIT_MODE, XMIT_CW);
    modulationState = XMIT_CW;
}


// Calibration Control

/**
 * Set up the calibration signal feedback control. It is turned off after this step.
 */
errno_t InitCalFeedbackControl(void){
    pinMode(CAL, OUTPUT);
    digitalWrite(CAL, CAL_OFF);
    calFeedbackState = CAL_OFF;
    return ESUCCESS;
}

/**
 * Enable cal feedback. Only change the digital control line if state is changing.
 */
void EnableCalFeedback(void){
    if (calFeedbackState == CAL_OFF) digitalWrite(CAL, CAL_ON);
    modulationState = CAL_ON;
}

/**
 * Disable cal feedback. Only change the digital control line if state is changing.
 */
void DisableCalFeedback(void){
    if (calFeedbackState == CAL_ON) digitalWrite(CAL, CAL_OFF);
    modulationState = CAL_OFF;
}

// RXTX Control

/**
 * Set up the RXTX control. It is in RX mode after this step.
 */
errno_t InitRXTX(void){
    pinMode(RXTX, OUTPUT);
    digitalWrite(RXTX, RX);
    rxtxState = RX;
    return ESUCCESS;
}

/**
 * Select TX mode. Only change the digital control line if state is changing.
 */
void SelectTXMode(void){
    if (rxtxState == RX) digitalWrite(RXTX, TX);
    rxtxState = TX;
}

/**
 * Select RX mode. Only change the digital control line if state is changing.
 */
void SelectRXMode(void){
    if (rxtxState == TX) digitalWrite(RXTX, RX);
    rxtxState = RX;
}


////////////////////////////////////////////////////////////////////////////////////
// State machine code
////////////////////////////////////////////////////////////////////////////////////

ModeSm_StateId GetRFBoardPreviousState(void){
    return previousRadioState;
}

int64_t GetTXRXFreq_dHz(void){
    return 100*(EEPROMData.centerFreq_Hz + EEPROMData.fineTuneFreq_Hz - SR[SampleRate].rate/4);
}

int64_t GetCWTXFreq_dHz(void){
    return GetTXRXFreq_dHz() + (int64_t)(100*CWToneOffsetsHz[EEPROMData.CWToneIndex]);
}

void HandleRFBoardStateChange(RFBoardState newState){
    if (newState = oldrfBoardState){
        return;
    }
    // Following the state diagram in RF_board_api.drawio, implement the actions
    // required to enter the new state.
    switch (newState){
        case RFBoardReceive:{
            // Set GPA_state to appropriate value
            SetRXAttenuation( EEPROMData.RAtten[EEPROMData.currentBand] );
            // Set clockEnableCW to LO
            DisableCWVFOOutput();
            // Set cwState to LO
            CWoff();
            // Set frequencySSB_Hz to appropriate value
            SetSSBVFOFrequency( EEPROMData.centerFreq_Hz*100 );
            // Set driveCurrentSSB_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableSSB to HI (SSB)
            EnableSSBVFOOutput();
            // Set calFeedbackState to LO
            DisableCalFeedback();
            // Set rxtxState to LO (RX)
            SelectRXMode();
            break;
        }
        case RFBoardSSBTransmit:{
            // Set GPB state to appropriate value
            SetTXAttenuation( EEPROMData.XAttenSSB[EEPROMData.currentBand] );
            // Set clockEnableCW to LO
            DisableCWVFOOutput();
            // Set cwState to LO
            CWoff();
            // Set frequencySSB_Hz to the TXRX frequency
            SetSSBVFOFrequency( GetTXRXFreq_dHz() );
            // Set driveCurrentSSB_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableSSB to HI
            EnableSSBVFOOutput();
            // Set modulationState to HI (SSB)
            SelectTXSSBModulation();
            // Set calFeedbackState to LO
            DisableCalFeedback();
            // Set rxtxState to HI (TX)
            SelectTXMode();
            break;
        }
        case RFBoardCWMark:{
            // Set GPB state to appropriate value
            SetTXAttenuation( EEPROMData.XAttenCW[EEPROMData.currentBand] );
            // Set clockEnableSSB to LO
            DisableSSBVFOOutput();
            // Set frequencyCW_Hz to appropriate value
            SetCWVFOFrequency( GetCWTXFreq_dHz() );
            // Set driveCurrentCW_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableCW to HI
            EnableCWVFOOutput();
            // Set cwState to HI
            CWon();
            // Set rxtxState to HI (TX)
            SelectTXMode();
            // Set modulationState to LO (CW)
            SelectTXCWModulation();
            // Set calFeedbackState to LO
            DisableCalFeedback();
            break;
        }
        case RFBoardCWSpace:{
            // Set GPB state to appropriate value
            SetTXAttenuation( EEPROMData.XAttenCW[EEPROMData.currentBand] );
            // Set clockEnableSSB to LO
            DisableSSBVFOOutput();
            // Set frequencyCW_Hz to appropriate value
            SetCWVFOFrequency( GetCWTXFreq_dHz() );
            // Set driveCurrentCW_mA to appropriate value
            // > This does not change after initialization, so do nothing
            // Set clockEnableCW to HI
            EnableCWVFOOutput();
            // Set cwState to LO
            CWoff();
            // Set rxtxState to HI (TX)
            SelectTXMode();
            // Set modulationState to LO (CW)
            SelectTXCWModulation();
            // Set calFeedbackState to LO
            DisableCalFeedback();
            break;
        }
        case RFBoardCalIQ:{
            // Set GPA_state to appropriate value
            // Set GPB state to appropriate value
            // Set frequencySSB_Hz to appropriate value
            // Set driveCurrentSSB_mA to appropriate value
            // Set clockEnableSSB to HI
            // Set frequencyCW_Hz to appropriate value
            // Set driveCurrentCW_mA to appropriate value
            // Set clockEnableCW to HI
            // Set cwState to LO
            // Set rxtxState to LO
            // Set modulationState to LO
            // Set calFeedbackState to LO
            break;
        }
    }
    oldrfBoardState = newState;
}

void UpdateRFBoardState(void){
    if (modeSM.state_id == previousRadioState){
        // Already in this state, no need to change
        return;
    }
    // Several transceiver states map to the same RF board state. Handle this mapping
    // and call the function to handle the new RF board state.
    switch (modeSM.state_id){
        case (ModeSm_StateId_CW_RECEIVE):
        case (ModeSm_StateId_SSB_RECEIVE):{
            rfBoardState = RFBoardReceive;
            break;
        }
        case (ModeSm_StateId_SSB_TRANSMIT):{
            rfBoardState = RFBoardSSBTransmit;
            break;
        }
        case (ModeSm_StateId_CW_TRANSMIT_DIT_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_DAH_MARK):
        case (ModeSm_StateId_CW_TRANSMIT_MARK):{
            rfBoardState = RFBoardCWMark;
            break;
        }
        case (ModeSm_StateId_CW_TRANSMIT_SPACE):
        case (ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE):
        case (ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT):{
            rfBoardState = RFBoardCWSpace;
            break;
        }

        //case (ModeSm_StateId_CALIBRATE_FREQUENCY):
        //case (ModeSm_StateId_CALIBRATE_RX_IQ):
        //case (ModeSm_StateId_CALIBRATE_TX_IQ):
        //case (ModeSm_StateId_CALIBRATE_CW_PA):
        //case (ModeSm_StateId_CALIBRATE_SSB_PA):{
        //    break;
        //}

        default:{
            Debug("Unhandled modeSM.state_id state in UpdateRFBoardState!");
            char strbuf[10];
            sprintf(strbuf, "> %d",(uint32_t)modeSM.state_id);
            Debug(strbuf);
            break;
        }
    }
    HandleRFBoardStateChange(rfBoardState);
    previousRadioState = modeSM.state_id;
}