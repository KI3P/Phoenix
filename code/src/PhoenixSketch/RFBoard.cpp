#include "SDT.h"
#include "RFBoard_si5351.h"

///////////////////////////////////////////////////////////////////////////////
// Variables that are only visible from within this file
///////////////////////////////////////////////////////////////////////////////

static Adafruit_MCP23X17 mcpAtten;
#define MAX_ATTENUATION_VAL_DBx2 63
#define MIN_ATTENUATION_VAL_DBx2 0

static bool boardInitialized = false;
static errno_t error_state;

// VFO related
Si5351 si5351;
#define SI5351_LOAD_CAPACITANCE SI5351_CRYSTAL_LOAD_8PF
#define Si_5351_crystal 25000000L
bool dualVFO = true; /** Set to true if we have separate RX and TX VFOs */

// There are three VFOs: RX, TX, and CW. They are controlled separately.
#define SI5351_DRIVE_CURRENT_RX SI5351_DRIVE_8MA
static int32_t rxmultiple, oldrxMultiple;
static int64_t RXVFOFreq_dHz;
#define CLK0RX   SI5351_CLK1 
#define CLK90RX  SI5351_CLK0

#define SI5351_DRIVE_CURRENT_TX SI5351_DRIVE_2MA
static int32_t txmultiple, oldtxMultiple;
static int64_t TXVFOFreq_dHz;
#define CLK0TX   SI5351_CLK5 
#define CLK90TX  SI5351_CLK4

#define SI5351_DRIVE_CURRENT_CW SI5351_DRIVE_2MA
static int64_t CWVFOFreq_dHz;
#define CLKCW  SI5351_CLK6
#define CLKCWSINGLEVFO SI5351_CLK2

#define XMIT_SSB 1
#define XMIT_CW  0
#define CAL_OFF 0
#define CAL_ON  1
#define RX  0
#define TX  1

static uint8_t mcpA_old = 0x00;
static uint8_t mcpB_old = 0x00;

// Macros to get and set the relevant parts of the hardware register
#define RF_GPA_RXATT_STATE (uint8_t)((hardwareRegister >> RXATTLSB) & 0x0000003F)
#define RF_GPB_TXATT_STATE (uint8_t)((hardwareRegister >> TXATTLSB) & 0x0000003F)
#define SET_RF_GPA_RXATT(val) (hardwareRegister = (hardwareRegister & 0xF03FFFFF) | (((uint64_t)val & 0x0000003F) << RXATTLSB));buffer_add()
#define SET_RF_GPB_TXATT(val) (hardwareRegister = (hardwareRegister & 0xFFC0FFFF) | (((uint64_t)val & 0x0000003F) << TXATTLSB));buffer_add()

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
static bool InitI2C(void){
    Debug("Initializing RF board");
    //mcp.begin_I2C(RF_MCP23017_ADDR);
    if (!mcpAtten.begin_I2C(RF_MCP23017_ADDR)) {
        bit_results.RF_I2C_present = false;
        //ShowMessageOnWaterfall("RF MCP23017 not found at 0x"+String(RF_MCP23017_ADDR,HEX));
    } else {
        bit_results.RF_I2C_present = true;
    }
    
    if(bit_results.RF_I2C_present) {
        for (int i=0;i<16;i++){
            mcpAtten.pinMode(i, OUTPUT);
        }
        // Set all pins to zero. This means no attenuation
        SET_RF_GPA_RXATT(0x00);
        SET_RF_GPB_TXATT(0x00);
        mcpAtten.writeGPIOA(RF_GPA_RXATT_STATE); 
        mcpAtten.writeGPIOB(RF_GPB_TXATT_STATE);
        mcpA_old = RF_GPA_RXATT_STATE;
        mcpB_old = RF_GPB_TXATT_STATE;
    }
    return true;
}

/**
 * PRIVATE: Write the value of the GPIOA register to the MCP23017 chip.
 * 
 * Returns boolean true if the write was performed. Returns boolean false if it was not
 * because the new register value matches the old value. 
 */
static bool WriteGPIOARegister(void){
    if (RF_GPA_RXATT_STATE == mcpA_old) return false;
    mcpAtten.writeGPIOA(RF_GPA_RXATT_STATE);
    mcpA_old = RF_GPA_RXATT_STATE;
    return true;
}

/**
 * PRIVATE: Write the value of the GPIOB register to the MCP23017 chip.
 * 
 * Returns boolean true if the write was successful. Returns boolean false if it was not. 
 */
static bool WriteGPIOBRegister(void){
    if (RF_GPB_TXATT_STATE == mcpB_old) return false;
    mcpAtten.writeGPIOB(RF_GPB_TXATT_STATE);
    mcpB_old = RF_GPB_TXATT_STATE;
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
 * @param GPIO_register A flag identifying the register for this attenuator
 * 
 * @return Error code. ESUCCESS if no failure. EGPIOWRITEFAIL if unable to write to GPIO bank.
 *  
 */
static errno_t SetAttenuator(int32_t Attenuation_dBx2, uint8_t GPIO_register){
    if (GPIO_register == TX) {
        SET_RF_GPB_TXATT( (uint8_t)check_range(Attenuation_dBx2) );
        WriteGPIOBRegister();
    }
    if (GPIO_register == RX) {
        SET_RF_GPA_RXATT( (uint8_t)check_range(Attenuation_dBx2) );
        WriteGPIOARegister();
    }
    return ESUCCESS;
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
    errno_t err = TXAttenuatorCreate(0.0f);
    if (err != ESUCCESS){
        // it might be somehow possible that writing to the TX attenuator would fail
        // while writing to the RX attenuator would succeed. If this is the case,
        // handle this situation here.
    }
    return RXAttenuatorCreate(ED.RAtten[ED.currentBand[ED.activeVFO]]);
}

/**
 * Returns the attenuation setting of the RX attenuator. 
 * 
 *  @return rxAttenuation_dB The RX attenuation in units of dB.
 * 
 */
float32_t GetRXAttenuation(){
    return ((float32_t)RF_GPA_RXATT_STATE)/2.0;
}

/**
 * Returns the attenuation setting of the TX attenuator. 
 * 
 *  @return txAttenuation_dB The RX attenuation in units of dB.
 * 
 */
float32_t GetTXAttenuation(){
    return ((float32_t)RF_GPB_TXATT_STATE)/2.0;
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
    if (newRegisterValue == RF_GPA_RXATT_STATE){
        return ESUCCESS;
    } else {
        return SetAttenuator((int32_t)round(2*rxAttenuation_dB), RX);
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
    if (newRegisterValue == RF_GPB_TXATT_STATE){
        return ESUCCESS;
    } else {
        return SetAttenuator((int32_t)round(2*txAttenuation_dB), TX);
    }
}

// RX & TX VFO Control Functions

void SetFrequencyCorrectionFactor(int32_t corr){
    int64_t freq = GetRXVFOFrequency();
    RXVFOFreq_dHz = 0;

    si5351.reset();
    si5351.init(SI5351_LOAD_CAPACITANCE, Si_5351_crystal, corr);
    InitRXVFO();
    InitTXVFO();
    InitCWVFO();
    SetRXVFOFrequency(freq*100);
    SetRXVFOFrequency((freq+5000000)*100);
    SetRXVFOFrequency(freq*100);
}

/**
 * Get the current RX VFO frequency setting.
 *
 * Returns the frequency of the RX VFO (CLK0RX & CLK90RX quadrature outputs)
 * used for reception. The internal frequency is stored in deci-Hertz (Hz × 10) 
 * but this function returns the value in Hertz.
 *
 * @return Current RX VFO frequency in Hz
 *
 * @see SetRXVFOFrequency() to change the frequency
 * @see RXVFOFreq_dHz internal storage variable
 */
int64_t GetRXVFOFrequency(void){
    return RXVFOFreq_dHz/100;
}

/**
 * Get the current TX VFO frequency setting.
 *
 * Returns the frequency of the TX VFO (CLK0TX & CLK90TX quadrature outputs)
 * used for SSB transmission. The internal frequency is stored in deci-Hertz 
 * (Hz × 10) but this function returns the value in Hertz.
 *
 * @return Current TX VFO frequency in Hz
 *
 * @see SetTXVFOFrequency() to change the frequency
 * @see TXVFOFreq_dHz internal storage variable
 */
int64_t GetTXVFOFrequency(void){
    if (!dualVFO){
        return GetRXVFOFrequency();
    }
    return TXVFOFreq_dHz/100;
}

/**
 * Set the power of the VFO used to drive the RX portion of the radio.
 * 
 * @param power Expect one of the SI5351_DRIVE_?MA parameters
 */
void SetRXVFOPower(int32_t power){
    si5351.drive_strength(CLK0RX, (si5351_drive)power);
    si5351.drive_strength(CLK90RX, (si5351_drive)power);
}

/**
 * Set the power of the VFO used to drive the TX portion of the radio.
 * 
 * @param power Expect one of the SI5351_DRIVE_?MA parameters
 */
void SetTXVFOPower(int32_t power){
    if (!dualVFO){
        SetRXVFOPower(power);
        return;
    }
    si5351.drive_strength(CLK0TX, (si5351_drive)power);
    si5351.drive_strength(CLK90TX, (si5351_drive)power);
}

/**
 * Initialize the RX VFO. This is done once at startup and is invoked by InitVFOs().
 * Set the power and configure the PLL source, does not set the frequency.
 */
errno_t InitRXVFO(void){
    // Set driveCurrentSSB_mA to appropriate value
    SetRXVFOPower( SI5351_DRIVE_CURRENT_RX );
    si5351.set_ms_source(CLK0RX, SI5351_PLLA);
    si5351.set_ms_source(CLK90RX, SI5351_PLLA);
    return ESUCCESS;
}

/**
 * Initialize the TX VFO. This is done once at startup and is invoked by InitVFOs().
 * Set the power and configure the PLL source, does not set the frequency.
 */
errno_t InitTXVFO(void){
    if (!dualVFO){
        return InitRXVFO();
    }
    // Set driveCurrentSSB_mA to appropriate value
    SetTXVFOPower( SI5351_DRIVE_CURRENT_TX );
    si5351.set_ms_source(CLK0TX, SI5351_PLLB);
    si5351.set_ms_source(CLK90TX, SI5351_PLLB);
    return ESUCCESS;
}

/**
 * Calculate the even divisor used in the configuration of the PLL for the VFO.
 * 
 * @param freq2_Hz The desired VFO frequency in units of Hz.
 */
int32_t EvenDivisor(int64_t freq2_Hz) {
    int32_t mult = 1;
    // Use phase method of time delay described by
    // https://tj-lab.org/2020/08/27/si5351%E5%8D%98%E4%BD%93%E3%81%A73mhz%E4%BB%A5%E4%B8%8B%E3%81%AE%E7%9B%B4%E4%BA%A4%E4%BF%A1%E5%8F%B7%E3%82%92%E5%87%BA%E5%8A%9B%E3%81%99%E3%82%8B/
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
 * Set the CLK0RX and CLK90RX outputs as quadrature outputs at the specified frequency.
 *
 * @param frequency_dHz The desired clock frequency in (Hz * 100)
 */
void SetRXVFOFrequency(int64_t frequency_dHz){
    // No need to change if it's already at this setting
    if (frequency_dHz == RXVFOFreq_dHz) return;
    RXVFOFreq_dHz = frequency_dHz;
    int64_t ClkSetFreq = frequency_dHz;
    rxmultiple = EvenDivisor(ClkSetFreq / SI5351_FREQ_MULT);
    uint64_t pll_freq = ClkSetFreq * rxmultiple;
    uint64_t freq = pll_freq / rxmultiple;

    if ( rxmultiple == oldrxMultiple) {               // Still within the same multiple range
        si5351.set_pll(pll_freq, SI5351_PLLA);    // just change PLLA on each frequency change of encoder
                                                  // this minimizes I2C data for each frequency change within a
                                                  // multiple range
    } else {
        if ( rxmultiple <= 126) {                            // this the library setting of phase for freqs
            si5351.set_freq_manual(freq, pll_freq, CLK0RX);  // greater than 3.2MHz where multiple is <= 126
            si5351.set_freq_manual(freq, pll_freq, CLK90RX); // set both clocks to new frequency
            si5351.set_phase(CLK0RX, 0);                     // CLK0 phase = 0
            si5351.set_phase(CLK90RX, rxmultiple);           // Clk90 phase = multiple for 90 degrees(digital delay)
            si5351.pll_reset(SI5351_PLLA);                   // reset PLLA to align outputs
            si5351.output_enable(CLK0RX, 1);                 // set outputs on or off
            si5351.output_enable(CLK90RX, 1);
            SET_BIT(hardwareRegister,RXVFOBIT);
        } else {    // this is the timed delay technique for frequencies below 3.2MHz as detailed in
                    // https://tj-lab.org/2020/08/27/si5351単体で3mhz以下の直交信号を出力する/
            cli();                //__disable_irq(); or __enable_irq();     // or cli()/sei() pair; needed to get accurate timing??
            //si5351.output_enable(CLK0RX, 0);   // optional switch off clocks if audio effects are generated
            //si5351.output_enable(CLK90RX, 0);  //  with the change of multiple below 3.2MHz
            si5351.set_freq_manual((freq - 400ULL), pll_freq, CLK0RX);  // set up frequencies of CLK 0/90 4 Hz low
            si5351.set_freq_manual((freq - 400ULL), pll_freq, CLK90RX); // as per TJ-Labs article
            si5351.set_phase(CLK0RX, 0);                                // set phase registers to 0 just to be sure
            si5351.set_phase(CLK90RX, 0);
            si5351.pll_reset(SI5351_PLLA);                              // align both clockss in phase
            si5351.set_freq_manual(freq, pll_freq, CLK0RX);             // set clock 0  to required freq
            //delayNanoseconds(625000000);       // 62.5 * 1000000      // configured for a 62.5 mSec delay at 4 Hz difference
            delayMicroseconds(58500);                                   // nominally 62500 this figure can be adjusted for a more exact delay which is phase
            si5351.set_freq_manual(freq, pll_freq, CLK90RX);            // set CLK90 to the required freq after delay
            sei();
            si5351.output_enable(CLK0RX, 1);                            // switch them on to be sure
            si5351.output_enable(CLK90RX, 1);                           //    ""        ""
            SET_BIT(hardwareRegister,RXVFOBIT);
        }
    }
    oldrxMultiple = rxmultiple;
}

/**
 * Set the CLK0TX and CLK90TX outputs as quadrature outputs at the specified frequency.
 *
 * @param frequency_dHz The desired clock frequency in (Hz * 100)
 */
void SetTXVFOFrequency(int64_t frequency_dHz){
    if (!dualVFO){
        SetRXVFOFrequency(frequency_dHz);
        return;
    }
    // No need to change if it's already at this setting
    if (frequency_dHz == TXVFOFreq_dHz) return;
    TXVFOFreq_dHz = frequency_dHz;
    int64_t ClkSetFreq = frequency_dHz;
    txmultiple = EvenDivisor(ClkSetFreq / SI5351_FREQ_MULT);
    uint64_t pll_freq = ClkSetFreq * txmultiple;
    uint64_t freq = pll_freq / txmultiple;

    if ( txmultiple == oldtxMultiple) {           // Still within the same multiple range
        si5351.set_pll(pll_freq, SI5351_PLLB);    // just change PLLB on each frequency change of encoder
                                                  // this minimizes I2C data for each frequency change within a
                                                  // multiple range
    } else {
        if ( txmultiple <= 126) {                            // this the library setting of phase for freqs
            si5351.set_freq_manual(freq, pll_freq, CLK0TX);  // greater than 3.2MHz where multiple is <= 126
            si5351.set_freq_manual(freq, pll_freq, CLK90TX); // set both clocks to new frequency
            si5351.set_phase(CLK0TX, 0);                     // CLK0 phase = 0
            si5351.set_phase(CLK90TX, txmultiple);           // CLK90 phase = multiple for 90 degrees(digital delay)
            si5351.pll_reset(SI5351_PLLB);                   // reset PLLB to align outputs
            si5351.output_enable(CLK0TX, 1);                 // set outputs on or off
            si5351.output_enable(CLK90TX, 1);
            SET_BIT(hardwareRegister,TXVFOBIT);
        } else {    // this is the timed delay technique for frequencies below 3.2MHz as detailed in
                    // https://tj-lab.org/2020/08/27/si5351単体で3mhz以下の直交信号を出力する/
            cli();                              // or cli()/sei() pair; needed to get accurate timing??
            //si5351.output_enable(CLK0TX, 0);  // optional switch off clocks if audio effects are generated
            //si5351.output_enable(CLK90TX, 0); //  with the change of multiple below 3.2MHz
            si5351.set_freq_manual((freq - 400ULL), pll_freq, CLK0TX);  // set up frequencies of CLK 0/90 4 Hz low
            si5351.set_freq_manual((freq - 400ULL), pll_freq, CLK90TX); // as per TJ-Labs article
            si5351.set_phase(CLK0TX, 0);                                // set phase registers to 0 just to be sure
            si5351.set_phase(CLK90TX, 0);
            si5351.pll_reset(SI5351_PLLB);                              // align both clocks in phase
            si5351.set_freq_manual(freq, pll_freq, CLK0TX);             // set clock 4 to required freq
            //delayNanoseconds(625000000);                              // 62.5 * 1000000      //configured for a 62.5 mSec delay at 4 Hz difference
            delayMicroseconds(58500);                                   //nominally 62500 this figure can be adjusted for a more exact delay which is phase
            si5351.set_freq_manual(freq, pll_freq, CLK90TX);            // set CLK90 to the required freq after delay
            sei();
            si5351.output_enable(CLK0TX, 1);                            // switch them on to be sure
            si5351.output_enable(CLK90TX, 1);                           //    ""        ""
            SET_BIT(hardwareRegister,TXVFOBIT);
        }
    }
    oldtxMultiple = txmultiple;
}

/**
 * Enable the RX VFO I & Q outputs
 */
void EnableRXVFOOutput(void){
    si5351.output_enable(CLK0RX, 1);
    si5351.output_enable(CLK90RX, 1);
    SET_BIT(hardwareRegister,RXVFOBIT);
}

/**
 * Enable the TX VFO I & Q outputs
 */
void EnableTXVFOOutput(void){
    if (!dualVFO){
        EnableRXVFOOutput();
        return;
    }
    si5351.output_enable(CLK0TX, 1);
    si5351.output_enable(CLK90TX, 1);
    SET_BIT(hardwareRegister,TXVFOBIT);
}

/**
 * Disable the RX VFO I & Q outputs
 */
void DisableRXVFOOutput(void){
    si5351.output_enable(CLK0RX, 0);
    si5351.output_enable(CLK90RX, 0);
    CLEAR_BIT(hardwareRegister,RXVFOBIT);
}

/**
 * Disable the TX VFO I & Q outputs
 */
void DisableTXVFOOutput(void){
    if (!dualVFO){
        DisableRXVFOOutput();
        return;
    }
    si5351.output_enable(CLK0TX, 0);
    si5351.output_enable(CLK90TX, 0);
    CLEAR_BIT(hardwareRegister,TXVFOBIT);
}

// CW VFO Control Functions

/**
 * Set the CW VFO frequency for Morse code transmission.
 *
 * Configures the Si5351 CLKCW output frequency used for CW (Morse code) operation.
 * The frequency is stored and set in deci-Hertz units (Hz × 10) for high precision.
 *
 * Optimization: Skips the Si5351 write if the requested frequency matches the
 * current setting, preventing unnecessary I2C transactions.
 *
 * In CW mode, this frequency typically includes an offset for the sidetone pitch
 * (e.g., centerFreq + 700Hz for a 700Hz tone). The tune state machine handles
 * this offset calculation.
 *
 * @param frequency_dHz Desired CW VFO frequency in deci-Hertz (Hz × 10)
 *
 * @see GetCWVFOFrequency() to read current frequency
 * @see EnableCWVFOOutput() to enable CLK2 output
 * @see Tune.cpp for CW frequency offset handling
 */
void SetCWVFOFrequency(int64_t frequency_dHz){
    // No need to change if it's already at this setting
    if (frequency_dHz == CWVFOFreq_dHz) return;
    CWVFOFreq_dHz = frequency_dHz;
    if (dualVFO)
        si5351.set_freq(CWVFOFreq_dHz, CLKCW);
    else
        si5351.set_freq(CWVFOFreq_dHz, CLKCWSINGLEVFO);
}

/**
 * Get the current CW VFO frequency setting.
 *
 * Returns the frequency of the CW VFO (CLK2 output) used for CW transmission.
 * The internal frequency is stored in deci-Hertz (Hz × 10) but this function
 * returns the value in Hertz.
 *
 * @return Current CW VFO frequency in Hz
 *
 * @see SetCWVFOFrequency() to change the frequency
 * @see CWVFOFreq_dHz internal storage variable
 */
int64_t GetCWVFOFrequency(void){
    return CWVFOFreq_dHz/100;
}

/**
 * Enable the CW VFO output (CLK2)
 */
void EnableCWVFOOutput(void){
    if (dualVFO)
        si5351.output_enable(CLKCW, 1);
    else
        si5351.output_enable(CLKCWSINGLEVFO, 1);
    SET_BIT(hardwareRegister,CWVFOBIT);
}

/**
 * Disable the CW VFO output (CLK2)
 */
void DisableCWVFOOutput(void){
    if (dualVFO)
        si5351.output_enable(CLKCW, 0);
    else
        si5351.output_enable(CLKCWSINGLEVFO, 0);
    CLEAR_BIT(hardwareRegister,CWVFOBIT);
}

/**
 * Set the power of the VFO used to drive the CW portion of the radio.
 * 
 * @param power Expect one of the SI5351_DRIVE_?MA parameters
 */
void SetCWVFOPower(int32_t power){
    if (dualVFO){
        si5351.drive_strength(CLKCW, (si5351_drive)power);
        si5351.set_ms_source(CLKCW, SI5351_PLLB);
    } else {
        si5351.drive_strength(CLKCWSINGLEVFO, (si5351_drive)power);
        si5351.set_ms_source(CLKCWSINGLEVFO, SI5351_PLLB);
    }
}

/**
 * Initialize the CW VFO. This is done once at startup and is invoked by InitVFOs().
 * Set the power and configure the PLL source, does not set the frequency. CW VFO
 * output is off after initialization.
 */
errno_t InitCWVFO(void){
    SetCWVFOPower( SI5351_DRIVE_CURRENT_CW );
    if (dualVFO)
        si5351.set_ms_source(CLKCW, SI5351_PLLB);
    else
        si5351.set_ms_source(CLKCWSINGLEVFO, SI5351_PLLB);
    pinMode(CW_ON_OFF, OUTPUT);
    CLEAR_BIT(hardwareRegister,CWBIT);
    digitalWrite(CW_ON_OFF, 0);
    return ESUCCESS;
}

/**
 * Turn on CW output
 */
void CWon(void){
    if (!GET_BIT(hardwareRegister,CWBIT)) digitalWrite(CW_ON_OFF, 1);
    SET_BIT(hardwareRegister,CWBIT);
}

/**
 * Turn off CW output
 */
void CWoff(void){
    if (GET_BIT(hardwareRegister,CWBIT)) digitalWrite(CW_ON_OFF, 0);
    CLEAR_BIT(hardwareRegister,CWBIT);
}

/**
 * Get the current CW keying state.
 *
 * Returns the state of the CW transmit keying line (CW_ON_OFF pin).
 * This indicates whether CW RF output is currently enabled (key down)
 * or disabled (key up).
 *
 * The state is read from the CWBIT in the hardware register, which tracks
 * the CW_ON_OFF digital output pin state.
 *
 * @return true if CW output is enabled (key down), false if disabled (key up)
 *
 * @see CWon() to enable CW output
 * @see CWoff() to disable CW output
 */
bool getCWState(void){
    return GET_BIT(hardwareRegister,CWBIT);
}

/**
 * Set up the communication with the Si5351, initialize its capacitance and crystal
 * settings, and initialize the clock signals.
 *
 * Detects which I2C address the Si5351 is at:
 * - SI5351_BUS_BASE_ADDR (0x60): Single VFO hardware, dualVFO = false
 * - SI5351_DUAL_VFO_ADDR (0x61): Dual VFO hardware, dualVFO = true
 */
errno_t InitVFOs(void){
    bool foundDevice = false;

    // Try single VFO address first (0x60)
    si5351.set_address(SI5351_BUS_BASE_ADDR);
    si5351.reset();
    si5351.init(SI5351_LOAD_CAPACITANCE, Si_5351_crystal, ED.freqCorrectionFactor);
    MyDelay(100L);
    if (si5351.init(SI5351_LOAD_CAPACITANCE, Si_5351_crystal, ED.freqCorrectionFactor)) {
        dualVFO = false;
        foundDevice = true;
        Debug("Found Si5351 at single VFO address (0x60)");
    } else {
        // Try dual VFO address (0x61)
        si5351.set_address(SI5351_DUAL_VFO_ADDR);
        si5351.reset();
        si5351.init(SI5351_LOAD_CAPACITANCE, Si_5351_crystal, ED.freqCorrectionFactor);
        MyDelay(100L);
        if (si5351.init(SI5351_LOAD_CAPACITANCE, Si_5351_crystal, ED.freqCorrectionFactor)) {
            dualVFO = true;
            foundDevice = true;
            Debug("Found Si5351 at dual VFO address (0x61)");
        }
    }

    if (!foundDevice) {
        bit_results.RF_Si5351_present = false;
        Debug("Initialize si5351 failed!");
        return EFAIL;
    }

    bit_results.RF_Si5351_present = true;
    // Disable all clock outputs.
    // The ones we will actually use are enabled by the init functions below.
    for (int k=0; k<8; k++){
        si5351.output_enable((si5351_clock)k, 0);
    }
    MyDelay(100L);

    InitRXVFO();
    InitTXVFO();
    InitCWVFO();
    return ESUCCESS;
}

bool HasDualVFOs(void){
    return dualVFO;
}

// Only used during unit tests!
void SetDualVFOs(bool val){
    dualVFO = val;
}

// Transmit Modulation Control

/**
 * Set up the transmit modulation control. The transmit modulation type will
 * be SSB after this step.
 */
errno_t InitTXModulation(void){
    pinMode(XMIT_MODE, OUTPUT);
    digitalWrite(XMIT_MODE, XMIT_SSB);
    SET_BIT(hardwareRegister,MODEBIT); // XMIT_SSB
    return ESUCCESS;
}

/**
 * Select SSB modulation circuit. Only change the digital control line if
 * the modulation type is changing.
 */
void SelectTXSSBModulation(void){
    if (GET_BIT(hardwareRegister,MODEBIT) == XMIT_CW) digitalWrite(XMIT_MODE, XMIT_SSB);
    SET_BIT(hardwareRegister,MODEBIT); // XMIT_SSB
}

/**
 * Select CW modulation circuit. Only change the digital control line if
 * the modulation type is changing.
 */
void SelectTXCWModulation(void){
    if (GET_BIT(hardwareRegister,MODEBIT) == XMIT_SSB) digitalWrite(XMIT_MODE, XMIT_CW);
    CLEAR_BIT(hardwareRegister,MODEBIT); // XMIT_CW
}

/**
 * Get the current transmit modulation mode.
 *
 * Returns the state of the transmit modulation selector (XMIT_MODE pin).
 * This determines which modulation circuit is active for transmission:
 * - SSB modulation path (for voice/SSB)
 * - CW modulation path (for Morse code)
 *
 * The state is read from the MODEBIT in the hardware register.
 *
 * @return XMIT_SSB (1) if SSB modulation is selected, XMIT_CW (0) if CW modulation is selected
 *
 * @see SelectTXSSBModulation() to select SSB mode
 * @see SelectTXCWModulation() to select CW mode
 */
bool getModulationState(void){
    return GET_BIT(hardwareRegister,MODEBIT);
}

// Calibration Control

/**
 * Set up the calibration signal feedback control. It is turned off after this step.
 */
errno_t InitCalFeedbackControl(void){
    pinMode(CAL, OUTPUT);
    digitalWrite(CAL, CAL_OFF);
    CLEAR_BIT(hardwareRegister,CALBIT); // CAL_OFF
    return ESUCCESS;
}

/**
 * Enable cal feedback. Only change the digital control line if state is changing.
 */
void EnableCalFeedback(void){
    if (GET_BIT(hardwareRegister,CALBIT) == CAL_OFF) digitalWrite(CAL, CAL_ON);
    SET_BIT(hardwareRegister,CALBIT); // CAL_ON
}

/**
 * Disable cal feedback. Only change the digital control line if state is changing.
 */
void DisableCalFeedback(void){
    if (GET_BIT(hardwareRegister,CALBIT) == CAL_ON) digitalWrite(CAL, CAL_OFF);
    CLEAR_BIT(hardwareRegister,CALBIT); // CAL_OFF
}

/**
 * Get the current calibration feedback state.
 *
 * Returns whether the calibration signal feedback path is currently enabled
 * or disabled. The calibration feedback allows the transmit signal to be
 * routed back to the receiver for calibration and testing purposes.
 *
 * The state is read from the CALBIT in the hardware register, which tracks
 * the CAL digital output pin state.
 *
 * @return CAL_ON (1) if calibration feedback is enabled, CAL_OFF (0) if disabled
 *
 * @see EnableCalFeedback() to enable calibration feedback
 * @see DisableCalFeedback() to disable calibration feedback
 */
bool getCalFeedbackState(void){
    return GET_BIT(hardwareRegister,CALBIT);
}

// RXTX Control

/**
 * Set up the RXTX control. It is in RX mode after this step.
 */
errno_t InitRXTX(void){
    pinMode(RXTX, OUTPUT);
    digitalWrite(RXTX, RX);
    CLEAR_BIT(hardwareRegister,RXTXBIT); //RX
    return ESUCCESS;
}

/**
 * Select TX mode. Only change the digital control line if state is changing.
 */
void SelectTXMode(void){
    if (GET_BIT(hardwareRegister,RXTXBIT) == RX) digitalWrite(RXTX, TX);
    SET_BIT(hardwareRegister,RXTXBIT); //TX
}

/**
 * Select RX mode. Only change the digital control line if state is changing.
 */
void SelectRXMode(void){
    if (GET_BIT(hardwareRegister,RXTXBIT) == TX) digitalWrite(RXTX, RX);
    CLEAR_BIT(hardwareRegister,RXTXBIT); //RX
}

/**
 * Get the current RX/TX state.
 *
 * Returns whether the radio is currently in receive or transmit mode.
 * This state controls the antenna relay, power amplifier enable, and
 * receiver/transmitter signal routing.
 *
 * The state is read from the RXTXBIT in the hardware register, which tracks
 * the RXTX digital output pin state.
 *
 * @return TX (1) if in transmit mode, RX (0) if in receive mode
 *
 * @see SelectTXMode() to switch to transmit
 * @see SelectRXMode() to switch to receive
 */
bool getRXTXState(void){
    return GET_BIT(hardwareRegister,RXTXBIT);
}

/**
 * Read both GPIO registers from the RF board MCP23017 chip.
 *
 * Performs a 16-bit read of both GPIOA and GPIOB registers from the
 * MCP23017 I2C GPIO expander on the RF board. These registers control
 * the digital attenuators:
 * - GPIOA: RX attenuation (6 bits, 0-31.5 dB in 0.5 dB steps)
 * - GPIOB: TX attenuation (6 bits, 0-31.5 dB in 0.5 dB steps)
 *
 * Used for debugging and diagnostics to verify the actual hardware
 * register states match the intended settings.
 *
 * @return 16-bit value with GPIOB in upper byte, GPIOA in lower byte
 *
 * @see SetRXAttenuation() to set RX attenuator
 * @see SetTXAttenuation() to set TX attenuator
 */
uint16_t GetRFMCPRegisters(void){
    return mcpAtten.readGPIOAB();
}

/**
 * Reset VFO state for testing.
 *
 * Resets the static VFO multiple tracking variables to force fresh
 * frequency calculations on the next SetRXVFOFrequency() or SetTXVFOFrequency() call.
 * This is used by unit tests to ensure clean state between test runs.
 */
void ResetVFOState(void){
    oldrxMultiple = 0;
    oldtxMultiple = 0;
    RXVFOFreq_dHz = 0;
    TXVFOFreq_dHz = 0;
    CWVFOFreq_dHz = 0;
}