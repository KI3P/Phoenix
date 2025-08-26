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

extern struct config_t EEPROMData;
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
    EEPROMData.centerFreq_Hz = 7074000;
    EEPROMData.fineTuneFreq_Hz = 100;
    SampleRate = SAMPLE_RATE_48K;
    // Calculation is: 100 * (7074000 + 100 - 48000/4) = 100 * (7074100 - 12000) = 100 * 7062100 = 706210000
    EXPECT_EQ(GetTXRXFreq_dHz(), 706210000);
}

TEST(RFBoard, GetCWTXFreq_dHz_LSB_Test) {
    EEPROMData.centerFreq_Hz = 7074000;
    EEPROMData.fineTuneFreq_Hz = 100;
    SampleRate = SAMPLE_RATE_192K;
    EEPROMData.currentBand = BAND_40M; // LSB
    EEPROMData.CWToneIndex = 3; // 750 Hz
    // Calculation is: 100 * (7074000 + 100 - 192000/4) = 100 * (7074100 - 48000) = 702610000
    // GetTXRXFreq_dHz() - 100 * 750 = 702610000 - 75000 = 702535000
    EXPECT_EQ(GetCWTXFreq_dHz(), 702535000);
}

TEST(RFBoard, GetCWTXFreq_dHz_USB_Test) {
    EEPROMData.centerFreq_Hz = 14074000;
    EEPROMData.fineTuneFreq_Hz = 100;
    SampleRate = SAMPLE_RATE_192K;
    EEPROMData.currentBand = BAND_20M; // USB
    EEPROMData.CWToneIndex = 3; // 750 Hz
    // Calculation is: 100 * (14074000 + 100 - 48000) = 1402610000
    // GetTXRXFreq_dHz() + 100 * 750 = 1402610000 + 75000 = 1402685000
    EXPECT_EQ(GetCWTXFreq_dHz(), 1402685000);
}

TEST(RFBoard, SetFreq_Test) {
    si5351 = Si5351(); // Reset mock
    SetFreq(7074000);
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
    EEPROMData.centerFreq_Hz = 7074000;
    EEPROMData.fineTuneFreq_Hz = 100;
    SampleRate = SAMPLE_RATE_192K;
    EEPROMData.currentBand = BAND_40M; // LSB
    EEPROMData.CWToneIndex = 3; // 750 Hz
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
    pin_mode_pins.clear();
    pin_mode_values.clear();
    digital_write_pins.clear();
    digital_write_values.clear();
    InitCWVFO();
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK2], SI5351_DRIVE_2MA);
    EXPECT_EQ(si5351.pll_assignment[SI5351_CLK1], SI5351_PLLA);
    EXPECT_EQ(pin_mode_pins.back(), CW_ON_OFF);
    EXPECT_EQ(pin_mode_values.back(), OUTPUT);
    EXPECT_EQ(digital_write_pins.back(), CW_ON_OFF);
    EXPECT_EQ(digital_write_values.back(), 0);
}

TEST(RFBoard, InitVFOs_Test) {
    si5351 = Si5351(); // Reset mock
    EEPROMData.freqCorrectionFactor = 0;
    InitVFOs();
    // Not much to test here since the mock is simple
    // We can at least check that the VFOs were initialized
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK0], SI5351_DRIVE_2MA);
    EXPECT_EQ(si5351.drive_strength_values[SI5351_CLK2], SI5351_DRIVE_2MA);
}

TEST(RFBoard, InitTXModulation_Test) {
    pin_mode_pins.clear();
    pin_mode_values.clear();
    digital_write_pins.clear();
    digital_write_values.clear();
    InitTXModulation();
    EXPECT_EQ(pin_mode_pins.back(), XMIT_MODE);
    EXPECT_EQ(pin_mode_values.back(), OUTPUT);
    EXPECT_EQ(digital_write_pins.back(), XMIT_MODE);
    EXPECT_EQ(digital_write_values.back(), 1); // XMIT_SSB
}

TEST(RFBoard, SelectTXSSBModulation_Test) {
    digital_write_pins.clear();
    digital_write_values.clear();
    SelectTXCWModulation(); // Start in CW mode
    SelectTXSSBModulation();
    EXPECT_EQ(digital_write_pins.back(), XMIT_MODE);
    EXPECT_EQ(digital_write_values.back(), 1); // XMIT_SSB
}

TEST(RFBoard, SelectTXCWModulation_Test) {
    digital_write_pins.clear();
    digital_write_values.clear();
    SelectTXSSBModulation(); // Start in SSB mode
    SelectTXCWModulation();
    EXPECT_EQ(digital_write_pins.back(), XMIT_MODE);
    EXPECT_EQ(digital_write_values.back(), 0); // XMIT_CW
}

TEST(RFBoard, InitCalFeedbackControl_Test) {
    pin_mode_pins.clear();
    pin_mode_values.clear();
    digital_write_pins.clear();
    digital_write_values.clear();
    InitCalFeedbackControl();
    EXPECT_EQ(pin_mode_pins.back(), CAL);
    EXPECT_EQ(pin_mode_values.back(), OUTPUT);
    EXPECT_EQ(digital_write_pins.back(), CAL);
    EXPECT_EQ(digital_write_values.back(), 0); // CAL_OFF
}

TEST(RFBoard, EnableCalFeedback_Test) {
    digital_write_pins.clear();
    digital_write_values.clear();
    DisableCalFeedback();
    EnableCalFeedback();
    EXPECT_EQ(digital_write_pins.back(), CAL);
    EXPECT_EQ(digital_write_values.back(), 1); // CAL_ON
}

TEST(RFBoard, DisableCalFeedback_Test) {
    digital_write_pins.clear();
    digital_write_values.clear();
    InitCalFeedbackControl();
    EnableCalFeedback();
    DisableCalFeedback();
    EXPECT_EQ(digital_write_pins.back(), CAL);
    EXPECT_EQ(digital_write_values.back(), 0); // CAL_OFF
}

TEST(RFBoard, InitRXTX_Test) {
    pin_mode_pins.clear();
    pin_mode_values.clear();
    digital_write_pins.clear();
    digital_write_values.clear();
    InitRXTX();
    EXPECT_EQ(pin_mode_pins.back(), RXTX);
    EXPECT_EQ(pin_mode_values.back(), OUTPUT);
    EXPECT_EQ(digital_write_pins.back(), RXTX);
    EXPECT_EQ(digital_write_values.back(), 0); // RX
}

TEST(RFBoard, SelectTXMode_Test) {
    digital_write_pins.clear();
    digital_write_values.clear();
    SelectRXMode();
    SelectTXMode();
    EXPECT_EQ(digital_write_pins.back(), RXTX);
    EXPECT_EQ(digital_write_values.back(), 1); // TX
}

TEST(RFBoard, SelectRXMode_Test) {
    digital_write_pins.clear();
    digital_write_values.clear();
    SelectTXMode();
    SelectRXMode();
    EXPECT_EQ(digital_write_pins.back(), RXTX);
    EXPECT_EQ(digital_write_values.back(), 0); // RX
}

TEST(RFBoard, CWon_Test) {
    digital_write_pins.clear();
    digital_write_values.clear();
    CWon();
    EXPECT_EQ(digital_write_pins.back(), CW_ON_OFF);
    EXPECT_EQ(digital_write_values.back(), 1);
}

TEST(RFBoard, CWoff_Test) {
    digital_write_pins.clear();
    digital_write_values.clear();
    CWon();
    CWoff();
    EXPECT_EQ(digital_write_pins.back(), CW_ON_OFF);
    EXPECT_EQ(digital_write_values.back(), 0);
}