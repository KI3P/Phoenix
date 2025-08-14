#include "gtest/gtest.h"

//extern "C" {
#include "../src/PhoenixSketch/SDT.h"
//}

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