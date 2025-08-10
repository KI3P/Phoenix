#ifndef BEENHERE
#include "SDT.h"
#endif

///////////////////////////////////////////////////////////////////////////////
// Variables that are only visible from within this file
///////////////////////////////////////////////////////////////////////////////

#ifndef TESTMODE
static Adafruit_MCP23X17 mcp;
#endif
//static bool failed;
static uint8_t GPB_state;
static uint8_t GPA_state;
static bool boardInitialized = false;
static errno_t error_state;

#define MAX_ATTENUATION_VAL_DBx2 63
#define MIN_ATTENUATION_VAL_DBx2 0

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
 * Set the attenuation of an attenuator to the provided value. The attenuation must
 * be specified in units of 2x dB. i.e., if you need 30 dB of attenuation, this value 
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
 * Initialize an attenuator.
 *
 * Initialize the I2C connection to an attenuator if not already done.
 * Then set the attenuation to the provided value.
 *
 * @param Attenuation_dB The attenuation in units of dB.
 * @param SetAtten The function for writing to the attenuator
 * 
 * @return Error code. ESUCCESS if no failure. ENOI2C if I2C connection failed. 
 *        EGPIOWRITEFAIL if unable to write to GPIO bank.
 * 
 */
static errno_t AttenuatorCreate(float Attenuation_dB, errno_t (*SetAtten)(float)){
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

///////////////////////////////////////////////////////////////////////////////
// Functions that are globally visible
///////////////////////////////////////////////////////////////////////////////

/**
 * Initialize the RX attenuator.
 *
 * Initialize the I2C connection to the receive attenuator if not already done.
 * Then set the attenuation to the provided value.
 *
 * @param rxAttenuation_dB The RX attenuation in units of dB. This value will be
 *                         rounded to the nearest 0.5 dB. Valid range: 0 to 31.5
 * 
 * @return Error code. ESUCCESS if no failure. ENOI2C if I2C connection failed. 
 *        EGPIOWRITEFAIL if unable to write to GPIO bank.
 * 
 */
errno_t RXAttenuatorCreate(float rxAttenuation_dB){
    return AttenuatorCreate(rxAttenuation_dB, SetRXAttenuator);
}

/**
 * Initialize the TX attenuator.
 *
 * Initialize the I2C connection to the transmit attenuator if not already done.
 * Then set the attenuation to the provided value.
 *
 * @param txAttenuation_dB The TX attenuation in units of dB. This value will be
 *                         rounded to the nearest 0.5 dB. Valid range: 0 to 31.5
 * 
 * @return Error code. ESUCCESS if no failure. ENOI2C if I2C connection failed. 
 *        EGPIOWRITEFAIL if unable to write to GPIO bank.
 * 
 */
errno_t TXAttenuatorCreate(float txAttenuation_dB){
    return AttenuatorCreate(txAttenuation_dB, SetTXAttenuator);
}

/**
 * Returns the attenuation setting of the RX attenuator. 
 * 
 *  @return rxAttenuation_dB The RX attenuation in units of dB.
 * 
 */
float GetRXAttenuator(){
    return ((float)GPA_state)/2.0;
}

/**
 * Returns the attenuation setting of the TX attenuator. 
 * 
 *  @return txAttenuation_dB The RX attenuation in units of dB.
 * 
 */
float GetTXAttenuator(){
    return ((float)GPB_state)/2.0;
}

/**
 * Sets the RX attenuation level.
 *
 * Set the attenuation of the RX attenuator to the provided value. The RX attenuation must
 * be specified in units of dB. 
 *
 * @param rxAttenuation_dB The RX attenuation in units of dB. Valid range: 0 to 31.5
 *
 * @return Error code. ESUCCESS if no failure. EGPIOWRITEFAIL if unable to write to GPIO bank.
 *  
 */
errno_t SetRXAttenuator(float rxAttenuation_dB){
    return SetAttenuator((int32_t)round(2*rxAttenuation_dB), &GPA_state, WriteGPIOARegister);
}

/**
 * Sets the TX attenuation level.
 *
 * Set the attenuation of the TX attenuator to the provided value. The TX attenuation must
 * be specified in units of dB. The attenuation is rounded to the nearest 0.5 dB
 *
 * @param txAttenuation_dB The TX attenuation in units of dB. Valid range: 0 to 31.5
 * 
 * @return Error code. ESUCCESS if no failure. EGPIOWRITEFAIL if unable to write to GPIO bank.
 *  
 */
errno_t SetTXAttenuator(float txAttenuation_dB){
    return SetAttenuator((int32_t)round(2*txAttenuation_dB), &GPB_state, WriteGPIOBRegister);
}

/**
 * Set the frequency of the Si5351
 * 
 * @param centerFreq_Hz THe desired frequency in Hz
 */
errno_t SetFreq(int64_t centerFreq_Hz){

}
