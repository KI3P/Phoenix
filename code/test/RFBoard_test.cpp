#include "gtest/gtest.h"

//extern "C" {
#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/si5351.h"
//}

extern int32_t EvenDivisor(int64_t freq2_Hz);
extern int64_t GetTXRXFreq_dHz(void);
extern int64_t GetCWTXFreq_dHz(void);

// Test list:
// Trying to pass a value if the I2C initialization failed fails

// After creation, the I2C connection is alive
TEST(RFBoard, RXAttenuatorCreate_InitializesI2C) {
    errno_t rv = RXAttenuatorCreate(20.0);
    EXPECT_EQ(rv, ESUCCESS);
}

// After creation, the GPIO register matches the defined startup state
TEST(RFBoard, RXAttenuatorCreate_SetsValue) {
    errno_t rv = RXAttenuatorCreate(20.0);
    float state = GetRXAttenuation();
    EXPECT_EQ(state, 20.0);
}

// After creation, the GPIO register pegs to max is the startup value is outside the allowed range
TEST(RFBoard, RXAttenuatorCreate_SetsInvalidValue) {
    errno_t rv = RXAttenuatorCreate(80.0);
    float state = GetRXAttenuation();
    EXPECT_EQ(state, 31.5);
}

// Setting the attenuation state to an allowed number is successful
TEST(RFBoard, RXAttenuator_SetValueInAllowedRangePasses) {
    errno_t rv = RXAttenuatorCreate(30.0);
    errno_t rv2 = SetRXAttenuation(20.0);
    float state = GetRXAttenuation();
    EXPECT_EQ(state, 20.0);
}

// Setting the attenuation state to a disallowed number outside allowed range pegs it to max
TEST(RFBoard, RXAttenuator_SetValueOutsideAllowedRangePegsToMax) {
    errno_t rv = RXAttenuatorCreate(30.0);
    errno_t rv2 = SetRXAttenuation(64.0);
    float state = GetRXAttenuation();
    EXPECT_EQ(state, 31.5);
}


// Setting the attenuation state to every allowed value works
TEST(RFBoard, RXAttenuator_EveryAllowedValueWorks) {
    errno_t rv = RXAttenuatorCreate(30.0);
    errno_t rv2;
    float state;
    int error = 0;
    for (int i=0;i<=63;i++){
        rv2 = SetRXAttenuation(0.0 + ((float)i)/2.0);
        state = GetRXAttenuation();
        if (abs((float)i/2 - state)>0.01) error += 1;
    }
    EXPECT_EQ(error, 0);
}


// After creation, the I2C connection is alive
TEST(RFBoard, TXAttenuatorCreate_InitializesI2C) {
    errno_t rv = TXAttenuatorCreate(60);
    EXPECT_EQ(rv, ESUCCESS);
}

// After creation, the GPIO register matches the defined startup state
TEST(RFBoard, TXAttenuatorCreate_SetsValue) {
    errno_t rv = TXAttenuatorCreate(30);
    float state = GetTXAttenuation();
    EXPECT_EQ(state, 30);
}

// After creation, the GPIO register pegs to max is the startup value is outside the allowed range
TEST(RFBoard, TXAttenuatorCreate_SetsInvalidValue) {
    errno_t rv = TXAttenuatorCreate(80.0);
    float state = GetTXAttenuation();
    EXPECT_EQ(state, 31.5);
}

// Setting the attenuation state to an allowed number is successful
TEST(RFBoard, TXAttenuator_SetValueInAllowedRangePasses) {
    errno_t rv = TXAttenuatorCreate(30);
    errno_t rv2 = SetTXAttenuation(20);
    float state = GetTXAttenuation();
    EXPECT_EQ(state, 20);
}

// Setting the attenuation state to a disallowed number outside allowed range pegs it to max
TEST(RFBoard, TXAttenuator_SetValueOutsideAllowedRangePegsToMax) {
    errno_t rv = TXAttenuatorCreate(30);
    errno_t rv2 = SetTXAttenuation(64);
    float state = GetTXAttenuation();
    EXPECT_EQ(state, 31.5);
}

// Setting the attenuation state to every allowed values works
TEST(RFBoard, TXAttenuator_EveryAllowedValueWorks) {
    errno_t rv = TXAttenuatorCreate(30);
    errno_t rv2;
    float state;
    int error = 0;
    for (int i=0;i<=63;i++){
        rv2 = SetTXAttenuation(0.0 + ((float)i)/2.0);
        state = GetTXAttenuation();
        if (abs((float)i/2 - state)>0.01) error += 1;
    }
    EXPECT_EQ(error, 0);
}

// Setting RX attenuation doesn't affect TX attenuation, and vice verse
TEST(RFBoard, RXTXAttenuators_SettingTXDoesNotChangeRX) {
    errno_t rv = TXAttenuatorCreate(30);
    errno_t rv3 = RXAttenuatorCreate(20);
    float RX_pre = GetRXAttenuation();
    errno_t rv2 = SetTXAttenuation(10);
    float RX_post = GetRXAttenuation();
    float error = RX_pre - RX_post;
    EXPECT_EQ(error, 0);
}

TEST(RFBoard, RXTXAttenuators_SettingRXDoesNotChangeTX) {
    errno_t rv = TXAttenuatorCreate(30);
    errno_t rv3 = RXAttenuatorCreate(20);
    float TX_pre = GetTXAttenuation();
    errno_t rv2 = SetRXAttenuation(10);
    float TX_post = GetTXAttenuation();
    float error = TX_pre - TX_post;
    EXPECT_EQ(error, 0);
}

// Attenuation value is being rounded to the nearest 0.5 dB
TEST(RFBoard, AttenuatorRoundingToNearestHalfdB) {
    errno_t rv;
    rv = TXAttenuatorCreate(30);
    rv = SetTXAttenuation(10.1);
    float val;
    val = GetTXAttenuation();
    EXPECT_EQ(val, 10.0);

    SetTXAttenuation(9.9);
    val = GetTXAttenuation();
    EXPECT_EQ(val, 10.0);

    SetTXAttenuation(9.76);
    val = GetTXAttenuation();
    EXPECT_EQ(val, 10.0);

    SetTXAttenuation(9.74);
    val = GetTXAttenuation();
    EXPECT_EQ(val, 9.5);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// VFO Tests
///////////////////////////////////////////////////////////////////////////////////////////////////

extern struct config_t ED;
extern Si5351 si5351;
extern const struct SR_Descriptor SR[];
extern uint8_t SampleRate;
extern const float32_t CWToneOffsetsHz[];
extern struct band bands[];


// Test the EvenDivisor function with a set of values
TEST(RFBoard, EvenDivisorTest) {
    EXPECT_EQ(EvenDivisor(99999), 8192);
    EXPECT_EQ(EvenDivisor(100000), 4096);
    EXPECT_EQ(EvenDivisor(199999), 4096);
    EXPECT_EQ(EvenDivisor(200000), 2048);
    EXPECT_EQ(EvenDivisor(399999), 2048);
    EXPECT_EQ(EvenDivisor(400000), 1024);
    EXPECT_EQ(EvenDivisor(799999), 1024);
    EXPECT_EQ(EvenDivisor(800000), 512);
    EXPECT_EQ(EvenDivisor(1599999), 512);
    EXPECT_EQ(EvenDivisor(1600000), 256);
    EXPECT_EQ(EvenDivisor(3199999), 256);
    EXPECT_EQ(EvenDivisor(3200000), 126);
    EXPECT_EQ(EvenDivisor(6849999), 126);
    EXPECT_EQ(EvenDivisor(6850000), 88);
    EXPECT_EQ(EvenDivisor(9499999), 88);
    EXPECT_EQ(EvenDivisor(9500000), 64);
    EXPECT_EQ(EvenDivisor(13599999), 64);
    EXPECT_EQ(EvenDivisor(13600000), 44);
    EXPECT_EQ(EvenDivisor(17499999), 44);
    EXPECT_EQ(EvenDivisor(17500000), 34);
    EXPECT_EQ(EvenDivisor(24999999), 34);
    EXPECT_EQ(EvenDivisor(25000000), 24);
    EXPECT_EQ(EvenDivisor(35999999), 24);
    EXPECT_EQ(EvenDivisor(36000000), 18);
    EXPECT_EQ(EvenDivisor(44999999), 18);
    EXPECT_EQ(EvenDivisor(45000000), 14);
    EXPECT_EQ(EvenDivisor(59999999), 14);
    EXPECT_EQ(EvenDivisor(60000000), 10);
    EXPECT_EQ(EvenDivisor(79999999), 10);
    EXPECT_EQ(EvenDivisor(80000000), 8);
    EXPECT_EQ(EvenDivisor(99999999), 8);
    EXPECT_EQ(EvenDivisor(100000000), 6);
    EXPECT_EQ(EvenDivisor(149999999), 6);
    EXPECT_EQ(EvenDivisor(150000000), 4);
    EXPECT_EQ(EvenDivisor(219999999), 4);
    EXPECT_EQ(EvenDivisor(220000000), 2);
}

TEST(RFBoard, GetTXRXFreq_dHz_Test) {
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    // Calculation is: 100 * (7074000 + 100 - 48000/4) = 100 * (7074100 - 12000) = 100 * 7062100 = 706210000
    EXPECT_EQ(GetTXRXFreq_dHz(), 706210000);
}

TEST(RFBoard, GetCWTXFreq_dHz_LSB_Test) {
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_192K;
    ED.currentBand[ED.activeVFO] = BAND_40M; // LSB
    ED.CWToneIndex = 3; // 750 Hz
    // Calculation is: 100 * (7074000 + 100 - 192000/4) = 100 * (7074100 - 48000) = 702610000
    // GetTXRXFreq_dHz() - 100 * 750 = 702610000 - 75000 = 702535000
    EXPECT_EQ(GetCWTXFreq_dHz(), 702535000);
}

TEST(RFBoard, GetCWTXFreq_dHz_USB_Test) {
    ED.centerFreq_Hz[ED.activeVFO] = 14074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_192K;
    ED.currentBand[ED.activeVFO] = BAND_20M; // USB
    ED.CWToneIndex = 3; // 750 Hz
    // Calculation is: 100 * (14074000 + 100 - 48000) = 1402610000
    // GetTXRXFreq_dHz() + 100 * 750 = 1402610000 + 75000 = 1402685000
    EXPECT_EQ(GetCWTXFreq_dHz(), 1402685000);
}

TEST(RFBoard, SetFreq_Test) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    UpdateTuneState();
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 707400000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 707400000);
}

TEST(RFBoard, SetSSBVFOPower_Test) {
    si5351 = Si5351(); // Reset mock
    SetSSBVFOPower(SI5351_DRIVE_4MA);
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK0], SI5351_DRIVE_4MA);
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK1], SI5351_DRIVE_4MA);
}

TEST(RFBoard, InitSSBVFO_Test) {
    si5351 = Si5351(); // Reset mock
    InitSSBVFO();
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK0], SI5351_DRIVE_2MA);
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK1], SI5351_DRIVE_2MA);
    EXPECT_EQ(si5351.pll_assignment[SI5351_CLK0], SI5351_PLLA);
    EXPECT_EQ(si5351.pll_assignment[SI5351_CLK1], SI5351_PLLA);
}

TEST(RFBoard, SetCWVFOFrequency_Test) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_192K;
    ED.currentBand[ED.activeVFO] = BAND_40M; // LSB
    ED.CWToneIndex = 3; // 750 Hz
    SetCWVFOFrequency(GetCWTXFreq_dHz());
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 702535000);
}

TEST(RFBoard, EnableCWVFOOutput_Test) {
    si5351 = Si5351(); // Reset mock
    EnableCWVFOOutput();
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 1);
}

TEST(RFBoard, DisableCWVFOOutput_Test) {
    si5351 = Si5351(); // Reset mock
    DisableCWVFOOutput();
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 0);
}

TEST(RFBoard, SetCWVFOPower_Test) {
    si5351 = Si5351(); // Reset mock
    SetCWVFOPower(SI5351_DRIVE_6MA);
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK2], SI5351_DRIVE_6MA);
    EXPECT_EQ(si5351.pll_assignment[SI5351_CLK2], SI5351_PLLA);
}

TEST(RFBoard, InitCWVFO_Test) {
    si5351 = Si5351(); // Reset mock
    InitCWVFO();
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK2], SI5351_DRIVE_2MA);
    EXPECT_EQ(si5351.pll_assignment[SI5351_CLK1], SI5351_PLLA);
    EXPECT_EQ(getPinMode(CW_ON_OFF), OUTPUT);
    EXPECT_EQ(digitalRead(CW_ON_OFF),0);
}

TEST(RFBoard, InitVFOs_Test) {
    si5351 = Si5351(); // Reset mock
    ED.freqCorrectionFactor = 0;
    InitVFOs();
    // Not much to test here since the mock is simple
    // We can at least check that the VFOs were initialized
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK0], SI5351_DRIVE_2MA);
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK2], SI5351_DRIVE_2MA);
}

TEST(RFBoard, InitTXModulation_Test) {
    InitTXModulation();
    EXPECT_EQ(getPinMode(XMIT_MODE), OUTPUT);
    EXPECT_EQ(digitalRead(XMIT_MODE),1); // XMIT_SSB
}

TEST(RFBoard, SelectTXSSBModulation_Test) {
    InitTXModulation();
    SelectTXCWModulation(); // Start in CW mode
    SelectTXSSBModulation();
    EXPECT_EQ(getPinMode(XMIT_MODE), OUTPUT);
    EXPECT_EQ(digitalRead(XMIT_MODE),1); // XMIT_SSB
}

TEST(RFBoard, SelectTXCWModulation_Test) {
    InitTXModulation();
    SelectTXSSBModulation(); // Start in SSB mode
    SelectTXCWModulation();
    EXPECT_EQ(getPinMode(XMIT_MODE), OUTPUT);
    EXPECT_EQ(digitalRead(XMIT_MODE),0); // XMIT_CW
}

TEST(RFBoard, InitCalFeedbackControl_Test) {
    InitCalFeedbackControl();
    EXPECT_EQ(getPinMode(CAL), OUTPUT);
    EXPECT_EQ(digitalRead(CAL),0); // CAL_OFF
}

TEST(RFBoard, EnableCalFeedback_Test) {
    InitCalFeedbackControl();
    DisableCalFeedback();
    EnableCalFeedback();
    EXPECT_EQ(getPinMode(CAL), OUTPUT);
    EXPECT_EQ(digitalRead(CAL),1); // CAL_ON
}

TEST(RFBoard, DisableCalFeedback_Test) {
    InitCalFeedbackControl();
    EnableCalFeedback();
    DisableCalFeedback();
    EXPECT_EQ(getPinMode(CAL), OUTPUT);
    EXPECT_EQ(digitalRead(CAL),0); // CAL_OFF
}

TEST(RFBoard, InitRXTX_Test) {
    InitRXTX();
    EXPECT_EQ(getPinMode(RXTX), OUTPUT);
    EXPECT_EQ(digitalRead(RXTX),0); // RX
}

TEST(RFBoard, SelectTXMode_Test) {
    InitRXTX();
    SelectRXMode();
    SelectTXMode();
    EXPECT_EQ(getPinMode(RXTX), OUTPUT);
    EXPECT_EQ(digitalRead(RXTX),1); // TX
}

TEST(RFBoard, SelectRXMode_Test) {
    InitRXTX();
    SelectTXMode();
    SelectRXMode();
    EXPECT_EQ(getPinMode(RXTX), OUTPUT);
    EXPECT_EQ(digitalRead(RXTX),0); // RX
}

TEST(RFBoard, CWon_Test) {
    InitCWVFO();
    CWon();
    EXPECT_EQ(getPinMode(CW_ON_OFF), OUTPUT);
    EXPECT_EQ(digitalRead(CW_ON_OFF),1);
}

TEST(RFBoard, CWoff_Test) {
    InitCWVFO();
    CWon();
    CWoff();
    EXPECT_EQ(getPinMode(CW_ON_OFF), OUTPUT);
    EXPECT_EQ(digitalRead(CW_ON_OFF),0);
}

TEST(RFBoard, StateStartInReceive){
    InitializeRFBoard();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateRFBoardState();
    
    // RECEIVE STATE
    // We expect CLK0 and CLK1 to be enabled, and CLK2 to be disabled
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK0], 1);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK1], 1);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 0);
    // CW is off
    EXPECT_EQ(getCWState(), 0); // CW off
    // RX mode
    EXPECT_EQ(getRXTXState(), 0); // RX
    // Cal feedback off
    EXPECT_EQ(getCalFeedbackState(), 0); // CAL_OFF
    // SSB mode
    EXPECT_EQ(getModulationState(), 1); // XMIT_SSB
}

TEST(RFBoard, StateTransitionToSSBTransmit){
    InitializeRFBoard();
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateRFBoardState();
    
    // SSB TRANSMIT STATE
    // We expect CLK0 and CLK1 to be enabled, and CLK2 to be disabled
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK0], 1);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK1], 1);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 0);
    // CW is off
    EXPECT_EQ(getCWState(), 0); // CW off
    // TX mode
    EXPECT_EQ(getRXTXState(), 1); // TX
    // Cal feedback off
    EXPECT_EQ(getCalFeedbackState(), 0); // CAL_OFF
    // SSB mode
    EXPECT_EQ(getModulationState(), 1); // XMIT_SSB
}

TEST(RFBoard, StateTransitionToCWSpace){
    InitializeRFBoard();
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    UpdateRFBoardState();
    
    // CW TRANSMIT SPACE STATE
    // We expect CLK0 and CLK1 to be disabled, and CLK2 to be enabled
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK0], 0);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK1], 0);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 1);
    // CW is off
    EXPECT_EQ(getCWState(), 0); // CW off
    // TX mode
    EXPECT_EQ(getRXTXState(), 1); // TX
    // Cal feedback off
    EXPECT_EQ(getCalFeedbackState(), 0); // CAL_OFF
    // CW mode
    EXPECT_EQ(getModulationState(), 0); // XMIT_CW
}

TEST(RFBoard, StateTransitionToCWMark){
    InitializeRFBoard();
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateRFBoardState();
    
    // CW TRANSMIT MARK STATE
    // We expect CLK0 and CLK1 to be disabled, and CLK2 to be enabled
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK0], 0);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK1], 0);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 1);
    // CW is on
    EXPECT_EQ(getCWState(), 1); // CW on
    // TX mode
    EXPECT_EQ(getRXTXState(), 1); // TX
    // Cal feedback off
    EXPECT_EQ(getCalFeedbackState(), 0); // CAL_OFF
    // CW mode
    EXPECT_EQ(getModulationState(), 0); // XMIT_CW
}

TEST(RFBoard, FrequenciesSetUponStateChange){
    // Set up the arrays
    ED.centerFreq_Hz[ED.activeVFO] = 7100000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 500L;
    int64_t rxtx = 7100000L + 500L - 192000L/4;

/*think about frequency control. How will this change as I switch between SSB mode and CW mode?
and between SSB receive and SSB transmit mode? it changes from state to state.
* CW/SSB Receive:  RXfreq = centerFreq_Hz + fineTuneFreq_Hz - SampleRate/4
* SSB Transmit:    TXfreq = centerFreq_Hz
* CW Transmit:     TXfreq = centerFreq_Hz + fineTuneFreq_Hz - SampleRate/4 -/+ CWToneOffset
*/

    InitializeRFBoard();
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateRFBoardState();    
    EXPECT_EQ(GetSSBVFOFrequency(), 7100000L);
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO], 500L);
    EXPECT_EQ(GetTXRXFreq_dHz(),rxtx*100);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Tune State Machine Tests
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromSSBReceive) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateTuneState();
    
    // Should set SSB VFO frequency to centerFreq_Hz * 100
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 707400000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 707400000);
}

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromCWReceive) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateTuneState();
    
    // Should set SSB VFO frequency to centerFreq_Hz * 100 (same as SSB receive)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 707400000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 707400000);
}

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromSSBTransmit) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateTuneState();
    
    // Should set SSB VFO frequency to GetTXRXFreq_dHz()
    // Calculation: 7074000 + 100 - 48000/4 = 7062100 Hz * 100 = 706210000
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 706210000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 706210000);
}

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromCWTransmitMark) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_40M; // LSB
    ED.CWToneIndex = 3; // 750 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (7074000 + 100 - 12000) * 100 = 706210000
    // GetCWTXFreq_dHz() = 706210000 - 75000 = 706135000 (LSB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 706135000);
}

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromCWTransmitSpace) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 14074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_20M; // USB
    ED.CWToneIndex = 3; // 750 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (14074000 + 100 - 12000) * 100 = 1406210000
    // GetCWTXFreq_dHz() = 1406210000 + 75000 = 1406285000 (USB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 1406285000);
}

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromCWTransmitDitMark) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_40M; // LSB
    ED.CWToneIndex = 2; // 656.5 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DIT_MARK;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (7074000 + 100 - 12000) * 100 = 706210000
    // GetCWTXFreq_dHz() = 706210000 - 65650 = 706144350 (LSB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 706144350);
}

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromCWTransmitDahMark) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 14074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 200L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_20M; // USB
    ED.CWToneIndex = 1; // 562.5 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DAH_MARK;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (14074000 + 200 - 12000) * 100 = 1406220000
    // GetCWTXFreq_dHz() = 1406220000 + 56250 = 1406276250 (USB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 1406276250);
}

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromCWTransmitKeyerSpace) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 3574000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 50L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_80M; // LSB
    ED.CWToneIndex = 0; // 400 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (3574000 + 50 - 12000) * 100 = 356205000
    // GetCWTXFreq_dHz() = 356205000 - 40000 = 356165000 (LSB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 356165000);
}

TEST(RFBoard, TuneStateMachine_UpdateTuneStateFromCWTransmitKeyerWait) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 21074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = -50L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_15M; // USB
    ED.CWToneIndex = 4; // 843.75 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (21074000 + (-50) - 12000) * 100 = 2106195000
    // GetCWTXFreq_dHz() = 2106195000 + 84375 = 2106275000 (USB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 2106279375);
}

TEST(RFBoard, TuneStateMachine_StateTransitionSequenceSSBToReceive) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 14230000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    
    // Start in SSB receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateTuneState();
    EXPECT_EQ(GetSSBVFOFrequency(), 14230000);
    
    // Transition to SSB transmit
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (14230000 + 100 - 12000) * 100 = 1421810000
    EXPECT_EQ(GetSSBVFOFrequency(), 14218100);
    
    // Back to SSB receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateTuneState();
    EXPECT_EQ(GetSSBVFOFrequency(), 14230000);
}

TEST(RFBoard, TuneStateMachine_StateTransitionSequenceCWReceiveToTransmit) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7030000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 200L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_40M; // LSB
    ED.CWToneIndex = 3; // 750 Hz
    
    // Start in CW receive
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateTuneState();
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 703000000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 703000000);
    
    // Transition to CW transmit mark
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (7030000 + 200 - 12000) * 100 = 701820000
    // GetCWTXFreq_dHz() = 701820000 - 75000 = 701745000 (LSB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 701745000);
    
    // Transition to CW transmit space
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    UpdateTuneState();
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 701745000);
    
    // Back to CW receive
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateTuneState();
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 703000000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 703000000);
}

TEST(RFBoard, TuneStateMachine_DifferentSampleRates) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 14074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    
    // Test with 192kHz sample rate
    SampleRate = SAMPLE_RATE_192K;
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (14074000 + 100 - 192000/4) * 100 = (14074100 - 48000) * 100 = 1402610000
    EXPECT_EQ(GetSSBVFOFrequency(), 14026100);
    
    // Test with 96kHz sample rate  
    SampleRate = SAMPLE_RATE_96K;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (14074000 + 100 - 96000/4) * 100 = (14074100 - 24000) * 100 = 1405010000
    EXPECT_EQ(GetSSBVFOFrequency(), 14050100);
    
    // Test with 48kHz sample rate
    SampleRate = SAMPLE_RATE_48K;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (14074000 + 100 - 48000/4) * 100 = (14074100 - 12000) * 100 = 1406210000
    EXPECT_EQ(GetSSBVFOFrequency(), 14062100);
}

// ================== BUFFER LOGGING TESTS ==================

TEST(RFBoard, BufferLogsSSBVFOStateChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test EnableSSBVFOOutput - should call SET_BIT which includes buffer_add()
    EnableSSBVFOOutput();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);
    EXPECT_EQ(buffer.head, 1);

    // Verify the register value was logged
    EXPECT_EQ(buffer.entries[0].register_value, hardwareRegister);

    // Test DisableSSBVFOOutput - should call CLEAR_BIT which includes buffer_add()
    DisableSSBVFOOutput();

    // Verify buffer has two entries
    EXPECT_EQ(buffer.count, 2);
    EXPECT_EQ(buffer.head, 2);

    // Verify the register values are different
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);

    // Verify timestamps are increasing
    EXPECT_LE(buffer.entries[0].timestamp, buffer.entries[1].timestamp);
}

TEST(RFBoard, BufferLogsCWVFOStateChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test EnableCWVFOOutput
    EnableCWVFOOutput();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    // Test DisableCWVFOOutput
    DisableCWVFOOutput();

    // Verify buffer has two entries with different register values
    EXPECT_EQ(buffer.count, 2);
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);
}

TEST(RFBoard, BufferLogsCWOnOffChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test CWon
    CWon();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    uint32_t register_after_on = buffer.entries[0].register_value;

    // Test CWoff
    CWoff();

    // Verify buffer has two entries
    EXPECT_EQ(buffer.count, 2);

    // Verify the register values are different
    EXPECT_NE(register_after_on, buffer.entries[1].register_value);

    // Verify timestamps are increasing
    EXPECT_LE(buffer.entries[0].timestamp, buffer.entries[1].timestamp);
}

TEST(RFBoard, BufferLogsModulationChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test SelectTXSSBModulation
    SelectTXSSBModulation();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    // Test SelectTXCWModulation
    SelectTXCWModulation();

    // Verify buffer has two entries with different register values
    EXPECT_EQ(buffer.count, 2);
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);
}

TEST(RFBoard, BufferLogsCalFeedbackChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test EnableCalFeedback
    EnableCalFeedback();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    // Test DisableCalFeedback
    DisableCalFeedback();

    // Verify buffer has two entries with different register values
    EXPECT_EQ(buffer.count, 2);
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);
}

TEST(RFBoard, BufferLogsRXTXModeChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test SelectTXMode
    SelectTXMode();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    // Test SelectRXMode
    SelectRXMode();

    // Verify buffer has two entries with different register values
    EXPECT_EQ(buffer.count, 2);
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);
}

TEST(RFBoard, BufferLogsAttenuatorChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Create RX attenuator (this should log register changes via SET_RF_GPA_RXATT macro)
    RXAttenuatorCreate(10.0);

    // Verify buffer has entries (creation may involve multiple register operations)
    EXPECT_GT(buffer.count, 0);

    size_t initial_count = buffer.count;

    // Change RX attenuation value
    SetRXAttenuation(20.0);

    // Verify buffer count increased
    EXPECT_GT(buffer.count, initial_count);

    // Create TX attenuator (this should log register changes via SET_RF_GPB_TXATT macro)
    size_t count_before_tx = buffer.count;
    TXAttenuatorCreate(15.0);

    // Verify buffer count increased
    EXPECT_GT(buffer.count, count_before_tx);

    // Change TX attenuation value
    size_t count_before_set_tx = buffer.count;
    SetTXAttenuation(25.0);

    // Verify buffer count increased
    EXPECT_GT(buffer.count, count_before_set_tx);
}

TEST(RFBoard, BufferLogsSequentialOperations) {
    // Initialize timing and buffer
    StartMillis();

    // Record the initial buffer count
    size_t initial_count = buffer.count;

    // Perform a simple sequence of operations
    EnableSSBVFOOutput();
    DisableSSBVFOOutput();

    // Verify we have at least 2 new entries
    EXPECT_GE(buffer.count, initial_count + 2);

    // Verify that buffer functionality is working by checking the count increased
    // This is a simple test that doesn't rely on complex buffer indexing
    EXPECT_GT(buffer.count, initial_count);
}