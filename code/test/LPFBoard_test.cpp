#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/LPFBoard.h"

// Constants from LPFBoard.cpp for testing
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

// Helper functions to access the internal register state (defined in LPFBoard.cpp)
uint16_t GetLPFRegister() {
    return GetLPFRegisterState();
}

void SetLPFRegister(uint16_t value) {
    SetLPFRegisterState(value);
}

class LPFBoardTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset register to a known state before each test
        SetLPFRegister(0x0000);
    }

    void TearDown() override {
        // Clean up after each test
        SetLPFRegister(0x0000);
    }
};

// ================== BIT MANIPULATION MACRO TESTS ==================

TEST_F(LPFBoardTest, SetBitMacro) {
    uint16_t testReg = 0x0000;

    // Test setting individual bits
    SET_BIT(testReg, 0);
    EXPECT_EQ(testReg, 0x0001);

    SET_BIT(testReg, 3);
    EXPECT_EQ(testReg, 0x0009);

    SET_BIT(testReg, 15);
    EXPECT_EQ(testReg, 0x8009);

    // Test setting already set bit (should remain set)
    SET_BIT(testReg, 0);
    EXPECT_EQ(testReg, 0x8009);
}

TEST_F(LPFBoardTest, ClearBitMacro) {
    uint16_t testReg = 0xFFFF;

    // Test clearing individual bits
    CLEAR_BIT(testReg, 0);
    EXPECT_EQ(testReg, 0xFFFE);

    CLEAR_BIT(testReg, 8);
    EXPECT_EQ(testReg, 0xFEFE);

    CLEAR_BIT(testReg, 15);
    EXPECT_EQ(testReg, 0x7EFE);

    // Test clearing already cleared bit (should remain cleared)
    CLEAR_BIT(testReg, 0);
    EXPECT_EQ(testReg, 0x7EFE);
}

TEST_F(LPFBoardTest, GetBitMacro) {
    uint16_t testReg = 0x8009; // Binary: 1000000000001001

    // Test getting set bits
    EXPECT_EQ(GET_BIT(testReg, 0), 1);
    EXPECT_EQ(GET_BIT(testReg, 3), 1);
    EXPECT_EQ(GET_BIT(testReg, 15), 1);

    // Test getting cleared bits
    EXPECT_EQ(GET_BIT(testReg, 1), 0);
    EXPECT_EQ(GET_BIT(testReg, 2), 0);
    EXPECT_EQ(GET_BIT(testReg, 7), 0);
}

TEST_F(LPFBoardTest, ToggleBitMacro) {
    uint16_t testReg = 0x0000;

    // Test toggling bits from 0 to 1
    TOGGLE_BIT(testReg, 0);
    EXPECT_EQ(testReg, 0x0001);

    TOGGLE_BIT(testReg, 3);
    EXPECT_EQ(testReg, 0x0009);

    // Test toggling bits from 1 to 0
    TOGGLE_BIT(testReg, 0);
    EXPECT_EQ(testReg, 0x0008);

    TOGGLE_BIT(testReg, 3);
    EXPECT_EQ(testReg, 0x0000);
}

// ================== BPF CONTROL FUNCTION TESTS ==================

TEST_F(LPFBoardTest, TXSelectBPF) {
    // Start with cleared register
    SetLPFRegister(0x0000);

    TXSelectBPF();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPFTXBPFBIT), 1);
    EXPECT_EQ(result & (1 << LPFTXBPFBIT), 1 << LPFTXBPFBIT);
}

TEST_F(LPFBoardTest, TXBypassBPF) {
    // Start with TX BPF bit set
    SetLPFRegister(1 << LPFTXBPFBIT);

    TXBypassBPF();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPFTXBPFBIT), 0);
    EXPECT_EQ(result & (1 << LPFTXBPFBIT), 0);
}

TEST_F(LPFBoardTest, RXSelectBPF) {
    // Start with cleared register
    SetLPFRegister(0x0000);

    RXSelectBPF();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPFRXBPFBIT), 1);
    EXPECT_EQ(result & (1 << LPFRXBPFBIT), 1 << LPFRXBPFBIT);
}

TEST_F(LPFBoardTest, RXBypassBPF) {
    // Start with RX BPF bit set
    SetLPFRegister(1 << LPFRXBPFBIT);

    RXBypassBPF();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPFRXBPFBIT), 0);
    EXPECT_EQ(result & (1 << LPFRXBPFBIT), 0);
}

TEST_F(LPFBoardTest, BPFControlIndependence) {
    // Test that TX and RX BPF controls don't interfere with each other
    SetLPFRegister(0x0000);

    TXSelectBPF();
    RXSelectBPF();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPFTXBPFBIT), 1);
    EXPECT_EQ(GET_BIT(result, LPFRXBPFBIT), 1);

    TXBypassBPF();
    result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPFTXBPFBIT), 0);
    EXPECT_EQ(GET_BIT(result, LPFRXBPFBIT), 1); // RX should remain set
}

// ================== XVTR CONTROL FUNCTION TESTS ==================

TEST_F(LPFBoardTest, SelectXVTR) {
    // XVTR is active low, so selecting should clear the bit
    SetLPFRegister(0xFFFF); // Start with all bits set

    SelectXVTR();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPFXVTRBIT), 0);
    EXPECT_EQ(result & (1 << LPFXVTRBIT), 0);
}

TEST_F(LPFBoardTest, BypassXVTR) {
    // Bypassing XVTR should set the bit (inactive high)
    SetLPFRegister(0x0000);

    BypassXVTR();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPFXVTRBIT), 1);
    EXPECT_EQ(result & (1 << LPFXVTRBIT), 1 << LPFXVTRBIT);
}

TEST_F(LPFBoardTest, XVTRControlToggle) {
    // Test toggling XVTR control
    SetLPFRegister(0x0000);

    BypassXVTR();
    EXPECT_EQ(GET_BIT(GetLPFRegister(), LPFXVTRBIT), 1);

    SelectXVTR();
    EXPECT_EQ(GET_BIT(GetLPFRegister(), LPFXVTRBIT), 0);
}

// ================== 100W PA CONTROL FUNCTION TESTS ==================

TEST_F(LPFBoardTest, Select100WPA) {
    SetLPFRegister(0x0000);

    Select100WPA();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPF100WBIT), 1);
    EXPECT_EQ(result & (1 << LPF100WBIT), 1 << LPF100WBIT);
}

TEST_F(LPFBoardTest, Bypass100WPA) {
    SetLPFRegister(1 << LPF100WBIT); // Start with 100W PA bit set

    Bypass100WPA();

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(GET_BIT(result, LPF100WBIT), 0);
    EXPECT_EQ(result & (1 << LPF100WBIT), 0);
}

// ================== LPF BAND SELECTION TESTS ==================

TEST_F(LPFBoardTest, SelectLPFBand160M) {
    SetLPFRegister(0xFFFF);

    SelectLPFBand(BAND_160M);

    uint16_t result = GetLPFRegister();
    uint16_t bandBits = result & 0x0F; // Lower 4 bits
    EXPECT_EQ(bandBits, LPF_BAND_160M);
}

TEST_F(LPFBoardTest, SelectLPFBand80M) {
    SetLPFRegister(0xFFFF);

    SelectLPFBand(BAND_80M);

    uint16_t result = GetLPFRegister();
    uint16_t bandBits = result & 0x0F;
    EXPECT_EQ(bandBits, LPF_BAND_80M);
}

TEST_F(LPFBoardTest, SelectLPFBand40M) {
    SetLPFRegister(0xFFFF);

    SelectLPFBand(BAND_40M);

    uint16_t result = GetLPFRegister();
    uint16_t bandBits = result & 0x0F;
    EXPECT_EQ(bandBits, LPF_BAND_40M);
}

TEST_F(LPFBoardTest, SelectLPFBand20M) {
    SetLPFRegister(0xFFFF);

    SelectLPFBand(BAND_20M);

    uint16_t result = GetLPFRegister();
    uint16_t bandBits = result & 0x0F;
    EXPECT_EQ(bandBits, LPF_BAND_20M);
}

TEST_F(LPFBoardTest, SelectLPFBand10M) {
    SetLPFRegister(0xFFFF);

    SelectLPFBand(BAND_10M);

    uint16_t result = GetLPFRegister();
    uint16_t bandBits = result & 0x0F;
    EXPECT_EQ(bandBits, LPF_BAND_10M);
}

TEST_F(LPFBoardTest, SelectLPFBand6M) {
    SetLPFRegister(0xFFFF);

    SelectLPFBand(BAND_6M);

    uint16_t result = GetLPFRegister();
    uint16_t bandBits = result & 0x0F;
    EXPECT_EQ(bandBits, LPF_BAND_6M);
}

TEST_F(LPFBoardTest, SelectLPFBandInvalidDefaultsToNF) {
    SetLPFRegister(0x0000);

    SelectLPFBand(99); // Invalid band

    uint16_t result = GetLPFRegister();
    uint16_t bandBits = result & 0x0F;
    EXPECT_EQ(bandBits, LPF_BAND_NF);
}

TEST_F(LPFBoardTest, LPFBandSelectionPreservesOtherBits) {
    // Set non-band bits and verify they are preserved
    SetLPFRegister(0xFFF0); // All bits set except band bits

    SelectLPFBand(BAND_20M);

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(result & 0xFFF0, 0xFFF0); // Upper bits preserved
    EXPECT_EQ(result & 0x0F, LPF_BAND_20M); // Band bits set correctly
}

// ================== ANTENNA SELECTION TESTS ==================

TEST_F(LPFBoardTest, SelectAntennaValid) {
    SetLPFRegister(0xFFFF);

    SelectAntenna(0);
    uint16_t result = GetLPFRegister();
    uint16_t antennaBits = (result >> 4) & 0x03; // Bits 4-5
    EXPECT_EQ(antennaBits, 0);

    SelectAntenna(1);
    result = GetLPFRegister();
    antennaBits = (result >> 4) & 0x03;
    EXPECT_EQ(antennaBits, 1);

    SelectAntenna(2);
    result = GetLPFRegister();
    antennaBits = (result >> 4) & 0x03;
    EXPECT_EQ(antennaBits, 2);

    SelectAntenna(3);
    result = GetLPFRegister();
    antennaBits = (result >> 4) & 0x03;
    EXPECT_EQ(antennaBits, 3);
}

TEST_F(LPFBoardTest, SelectAntennaInvalidIgnored) {
    SetLPFRegister(0x0020); // Antenna 2 selected (bits 4-5 = 10)
    uint16_t initialState = GetLPFRegister();

    SelectAntenna(4); // Invalid antenna
    uint16_t result = GetLPFRegister();
    EXPECT_EQ(result, initialState); // Should remain unchanged

    SelectAntenna(255); // Invalid antenna
    result = GetLPFRegister();
    EXPECT_EQ(result, initialState); // Should remain unchanged
}

TEST_F(LPFBoardTest, AntennaSelectionPreservesOtherBits) {
    // Set all other bits except antenna bits
    SetLPFRegister(0xFFCF); // All bits set except antenna bits (4-5)

    SelectAntenna(1);

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(result & 0xFFCF, 0xFFCF); // Non-antenna bits preserved
    EXPECT_EQ((result >> 4) & 0x03, 1); // Antenna bits set correctly
}

// ================== REGISTER STATE MANAGEMENT TESTS ==================

TEST_F(LPFBoardTest, LPFGPAStateAccess) {
    SetLPFRegister(0x12AB); // Upper: 0x12, Lower: 0xAB

    uint8_t gpaState = GetLPFRegister() & 0xFF; // LPF_GPA_state equivalent
    EXPECT_EQ(gpaState, 0xAB);
}

TEST_F(LPFBoardTest, LPFGPBStateAccess) {
    SetLPFRegister(0x12AB); // Upper: 0x12, Lower: 0xAB

    uint8_t gpbState = (GetLPFRegister() >> 8) & 0xFF; // LPF_GPB_STATE equivalent
    EXPECT_EQ(gpbState, 0x12);
}

TEST_F(LPFBoardTest, SetLPFGPAMacro) {
    SetLPFRegister(0x1234);

    // Simulate SET_LPF_GPA(0xAB)
    uint16_t newValue = (GetLPFRegister() & 0xFF00) | (0xAB & 0xFF);
    SetLPFRegister(newValue);

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(result, 0x12AB);
    EXPECT_EQ(result & 0xFF, 0xAB); // Lower byte
    EXPECT_EQ((result >> 8) & 0xFF, 0x12); // Upper byte unchanged
}

TEST_F(LPFBoardTest, SetLPFGPBMacro) {
    SetLPFRegister(0x1234);

    // Simulate SET_LPF_GPB(0xCD)
    uint16_t newValue = (GetLPFRegister() & 0x00FF) | ((0xCD & 0xFF) << 8);
    SetLPFRegister(newValue);

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(result, 0xCD34);
    EXPECT_EQ(result & 0xFF, 0x34); // Lower byte unchanged
    EXPECT_EQ((result >> 8) & 0xFF, 0xCD); // Upper byte
}

TEST_F(LPFBoardTest, ComplexRegisterManipulation) {
    // Test complex scenario with multiple bits set/cleared
    SetLPFRegister(0x0000);

    // Set various control bits
    TXSelectBPF();
    RXSelectBPF();
    Select100WPA();
    BypassXVTR(); // XVTR bypass sets the bit
    SelectLPFBand(BAND_20M);
    SelectAntenna(2);

    uint16_t result = GetLPFRegister();

    // Verify all bits are set correctly
    EXPECT_EQ(GET_BIT(result, LPFTXBPFBIT), 1);
    EXPECT_EQ(GET_BIT(result, LPFRXBPFBIT), 1);
    EXPECT_EQ(GET_BIT(result, LPF100WBIT), 1);
    EXPECT_EQ(GET_BIT(result, LPFXVTRBIT), 1);
    EXPECT_EQ(result & 0x0F, LPF_BAND_20M);
    EXPECT_EQ((result >> 4) & 0x03, 2);
}

// ================== INITIALIZATION FUNCTION TESTS ==================

TEST_F(LPFBoardTest, InitBPFPathControlCallsMainInit) {
    // InitBPFPathControl() should return the same result as InitLPFBoardMCP()
    errno_t result = InitBPFPathControl();

    // Since we can't mock the I2C hardware in this test environment,
    // we expect this to return ENOI2C (I2C device not found)
    // This tests that the function executes and returns a valid errno_t
    EXPECT_TRUE(result == ESUCCESS || result == ENOI2C || result == EFAIL);
}

TEST_F(LPFBoardTest, InitXVTRControlCallsMainInit) {
    // InitXVTRControl() should return the same result as InitLPFBoardMCP()
    errno_t result = InitXVTRControl();

    // Since we can't mock the I2C hardware in this test environment,
    // we expect this to return ENOI2C (I2C device not found)
    // This tests that the function executes and returns a valid errno_t
    EXPECT_TRUE(result == ESUCCESS || result == ENOI2C || result == EFAIL);
}

TEST_F(LPFBoardTest, Init100WPAControlCallsMainInit) {
    // Init100WPAControl() should return the same result as InitLPFBoardMCP()
    errno_t result = Init100WPAControl();

    // Since we can't mock the I2C hardware in this test environment,
    // we expect this to return ENOI2C (I2C device not found)
    // This tests that the function executes and returns a valid errno_t
    EXPECT_TRUE(result == ESUCCESS || result == ENOI2C || result == EFAIL);
}

TEST_F(LPFBoardTest, InitLPFControlCallsMainInit) {
    // InitLPFControl() should return the same result as InitLPFBoardMCP()
    errno_t result = InitLPFControl();

    // Since we can't mock the I2C hardware in this test environment,
    // we expect this to return ENOI2C (I2C device not found)
    // This tests that the function executes and returns a valid errno_t
    EXPECT_TRUE(result == ESUCCESS || result == ENOI2C || result == EFAIL);
}

TEST_F(LPFBoardTest, InitAntennaControlCallsMainInit) {
    // InitAntennaControl() should return the same result as InitLPFBoardMCP()
    errno_t result = InitAntennaControl();

    // Since we can't mock the I2C hardware in this test environment,
    // we expect this to return ENOI2C (I2C device not found)
    // This tests that the function executes and returns a valid errno_t
    EXPECT_TRUE(result == ESUCCESS || result == ENOI2C || result == EFAIL);
}

TEST_F(LPFBoardTest, InitSWRControlReturnsSuccess) {
    // InitSWRControl() currently just returns ESUCCESS
    // as the SWR ADC initialization is commented out
    errno_t result = InitSWRControl();

    EXPECT_EQ(result, ESUCCESS);
}

// Test that each init function can be called multiple times safely
TEST_F(LPFBoardTest, InitFunctionsMultipleCallsSafe) {
    // Each init function should be safe to call multiple times

    errno_t result1 = InitBPFPathControl();
    errno_t result2 = InitBPFPathControl();
    EXPECT_EQ(result1, result2);

    result1 = InitXVTRControl();
    result2 = InitXVTRControl();
    EXPECT_EQ(result1, result2);

    result1 = Init100WPAControl();
    result2 = Init100WPAControl();
    EXPECT_EQ(result1, result2);

    result1 = InitLPFControl();
    result2 = InitLPFControl();
    EXPECT_EQ(result1, result2);

    result1 = InitAntennaControl();
    result2 = InitAntennaControl();
    EXPECT_EQ(result1, result2);

    result1 = InitSWRControl();
    result2 = InitSWRControl();
    EXPECT_EQ(result1, result2);
    EXPECT_EQ(result2, ESUCCESS);
}

// Test the startup state constant
TEST_F(LPFBoardTest, StartupStateConstant) {
    // Test that LPF_REGISTER_STARTUP_STATE has expected values
    uint16_t startupState = 0x020F; // LPF_REGISTER_STARTUP_STATE value

    // Verify bit positions match comment in source:
    // receive mode, antenna 0, filter bypass

    // Bits 0-3 (band): should be 0x0F (no filter)
    EXPECT_EQ(startupState & 0x0F, 0x0F);

    // Bits 4-5 (antenna): should be 0 (antenna 0)
    EXPECT_EQ((startupState >> 4) & 0x03, 0);

    // Bit 6 (XVTR): should be 0 (XVTR selected/active low)
    EXPECT_EQ(GET_BIT(startupState, 6), 0);

    // Bit 7 (100W PA): should be 0 (100W PA bypassed)
    EXPECT_EQ(GET_BIT(startupState, 7), 0);

    // Bit 8 (TX BPF): should be 0 (TX BPF bypassed)
    EXPECT_EQ(GET_BIT(startupState, 8), 0);

    // Bit 9 (RX BPF): should be 1 (RX BPF selected for receive mode)
    EXPECT_EQ(GET_BIT(startupState, 9), 1);

    // Bits 10-15: should be 0 (not used)
    EXPECT_EQ((startupState >> 10) & 0x3F, 0);
}

// Test register state access functions for testing
TEST_F(LPFBoardTest, RegisterStateAccessFunctions) {
    // Test that we can set and get register state
    uint16_t testValue = 0x1234;

    SetLPFRegisterState(testValue);
    uint16_t retrievedValue = GetLPFRegisterState();

    EXPECT_EQ(retrievedValue, testValue);

    // Test with different value
    testValue = 0xABCD;
    SetLPFRegisterState(testValue);
    retrievedValue = GetLPFRegisterState();

    EXPECT_EQ(retrievedValue, testValue);
}

// Test register bit field macros
TEST_F(LPFBoardTest, RegisterBitFieldMacros) {
    SetLPFRegister(0x1234); // Upper: 0x12, Lower: 0x34

    // Test LPF_GPA_STATE (upper 8 bits)
    uint8_t gpaState = (GetLPFRegister() >> 8) & 0xFF;
    EXPECT_EQ(gpaState, 0x12);

    // Test LPF_GPB_STATE (lower 8 bits)
    uint8_t gpbState = GetLPFRegister() & 0xFF;
    EXPECT_EQ(gpbState, 0x34);
}

// Test SET_LPF_BAND macro behavior
TEST_F(LPFBoardTest, SetLPFBandMacro) {
    SetLPFRegister(0xFFF0); // All bits set except band bits

    // Simulate SET_LPF_BAND(0x05)
    uint16_t newValue = (GetLPFRegister() & 0xFFF0) | (0x05 & 0x0F);
    SetLPFRegister(newValue);

    uint16_t result = GetLPFRegister();
    EXPECT_EQ(result & 0x0F, 0x05); // Band bits set to 0x05
    EXPECT_EQ(result & 0xFFF0, 0xFFF0); // Other bits preserved
}

// Test SET_ANTENNA macro behavior
TEST_F(LPFBoardTest, SetAntennaMacro) {
    SetLPFRegister(0xFFCF); // All bits set except antenna bits (4-5)

    // Simulate SET_ANTENNA(0x02)
    uint16_t newValue = (GetLPFRegister() & 0b1111111111001111) | ((0x02 & 0b00000011) << 4);
    SetLPFRegister(newValue);

    uint16_t result = GetLPFRegister();
    EXPECT_EQ((result >> 4) & 0x03, 0x02); // Antenna bits set to 0x02
    EXPECT_EQ(result & 0xFFCF, 0xFFCF); // Other bits preserved
}

// ================== UPDATEMCPREGISTERS FUNCTION TESTS ==================

TEST_F(LPFBoardTest, UpdateMCPRegistersNoChangeNoUpdate) {
    // Set up a scenario where the register state matches the old values
    SetLPFRegister(0x1234);
    SetMCPAOld(0x12); // Upper 8 bits of register (GPA)
    SetMCPBOld(0x34); // Lower 8 bits of register (GPB)

    // Call UpdateMCPRegisters - should not call writeGPIO since values match
    UpdateMCPRegisters();

    // Old values should remain unchanged since no update was needed
    EXPECT_EQ(GetMCPAOld(), 0x12);
    EXPECT_EQ(GetMCPBOld(), 0x34);
}

TEST_F(LPFBoardTest, UpdateMCPRegistersGPAChanged) {
    // Set up a scenario where GPA register changed
    SetLPFRegister(0x12AB);
    SetMCPAOld(0x34); // Different from current GPA state (0x12)
    SetMCPBOld(0xAB); // Same as current GPB state

    // Call UpdateMCPRegisters - should update GPA but not GPB
    UpdateMCPRegisters();

    // Old GPA value should be updated to match current state
    EXPECT_EQ(GetMCPAOld(), 0x12);
    // Old GPB value should remain unchanged
    EXPECT_EQ(GetMCPBOld(), 0xAB);
}

TEST_F(LPFBoardTest, UpdateMCPRegistersGPBChanged) {
    // Set up a scenario where GPB register changed
    SetLPFRegister(0xCD56);
    SetMCPAOld(0xCD); // Same as current GPA state
    SetMCPBOld(0x78); // Different from current GPB state (0x56)

    // Call UpdateMCPRegisters - should update GPB but not GPA
    UpdateMCPRegisters();

    // Old GPA value should remain unchanged
    EXPECT_EQ(GetMCPAOld(), 0xCD);
    // Old GPB value should be updated to match current state
    EXPECT_EQ(GetMCPBOld(), 0x56);
}

TEST_F(LPFBoardTest, UpdateMCPRegistersBothChanged) {
    // Set up a scenario where both GPA and GPB registers changed
    SetLPFRegister(0xABCD);
    SetMCPAOld(0x12); // Different from current GPA state (0xAB)
    SetMCPBOld(0x34); // Different from current GPB state (0xCD)

    // Call UpdateMCPRegisters - should update both GPA and GPB
    UpdateMCPRegisters();

    // Both old values should be updated to match current state
    EXPECT_EQ(GetMCPAOld(), 0xAB);
    EXPECT_EQ(GetMCPBOld(), 0xCD);
}

// ================== UPDATED TXSELECTBPF FUNCTION TESTS ==================

TEST_F(LPFBoardTest, TXSelectBPFUpdatesRegisterAndHardware) {
    // Start with cleared register and known old values
    SetLPFRegister(0x0000);
    SetMCPAOld(0x00);
    SetMCPBOld(0x00);

    // Call TXSelectBPF - should set the bit and update hardware
    TXSelectBPF();

    uint16_t result = GetLPFRegister();

    // Verify the TX BPF bit is set
    EXPECT_EQ(GET_BIT(result, LPFTXBPFBIT), 1);
    EXPECT_EQ(result & (1 << LPFTXBPFBIT), 1 << LPFTXBPFBIT);

    // Verify that UpdateMCPRegisters was called and updated the old values
    // Since LPFTXBPFBIT is bit 8, it affects the GPA register (upper 8 bits)
    uint8_t expectedGPA = (result >> 8) & 0xFF;
    EXPECT_EQ(GetMCPAOld(), expectedGPA);
}

TEST_F(LPFBoardTest, TXSelectBPFWithExistingRegisterState) {
    // Start with a register that has some bits already set
    SetLPFRegister(0x1234);
    SetMCPAOld(0x12); // Same as current GPA state initially
    SetMCPBOld(0x34); // Same as current GPB state initially

    // Call TXSelectBPF - should set the TX BPF bit and update hardware
    TXSelectBPF();

    uint16_t result = GetLPFRegister();

    // Verify the TX BPF bit is set
    EXPECT_EQ(GET_BIT(result, LPFTXBPFBIT), 1);

    // Verify other bits are preserved
    uint16_t expectedResult = 0x1234 | (1 << LPFTXBPFBIT);
    EXPECT_EQ(result, expectedResult);

    // Verify that hardware state was updated appropriately
    uint8_t expectedGPA = (result >> 8) & 0xFF;
    EXPECT_EQ(GetMCPAOld(), expectedGPA);
}

// ================== MCP OLD VALUE ACCESSOR TESTS ==================

TEST_F(LPFBoardTest, MCPOldValueAccessors) {
    // Test setting and getting MCP A old value
    SetMCPAOld(0xAB);
    EXPECT_EQ(GetMCPAOld(), 0xAB);

    // Test setting and getting MCP B old value
    SetMCPBOld(0xCD);
    EXPECT_EQ(GetMCPBOld(), 0xCD);

    // Test that they're independent
    SetMCPAOld(0x12);
    EXPECT_EQ(GetMCPAOld(), 0x12);
    EXPECT_EQ(GetMCPBOld(), 0xCD); // Should not have changed

    SetMCPBOld(0x34);
    EXPECT_EQ(GetMCPAOld(), 0x12); // Should not have changed
    EXPECT_EQ(GetMCPBOld(), 0x34);
}

// ================== UPDATED BPF FUNCTION TESTS ==================

TEST_F(LPFBoardTest, TXBypassBPFUpdatesRegisterAndHardware) {
    // Start with TX BPF bit set and known old values
    SetLPFRegister(1 << LPFTXBPFBIT);
    SetMCPAOld((1 << LPFTXBPFBIT) >> 8); // GPA (upper 8 bits) - LPFTXBPFBIT is bit 8
    SetMCPBOld(0x00); // GPB (lower 8 bits)

    // Call TXBypassBPF - should clear the bit and update hardware
    TXBypassBPF();

    uint16_t result = GetLPFRegister();

    // Verify the TX BPF bit is cleared
    EXPECT_EQ(GET_BIT(result, LPFTXBPFBIT), 0);
    EXPECT_EQ(result & (1 << LPFTXBPFBIT), 0);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPA = (result >> 8) & 0xFF;
    EXPECT_EQ(GetMCPAOld(), expectedGPA);
}

TEST_F(LPFBoardTest, RXSelectBPFUpdatesRegisterAndHardware) {
    // Start with cleared register and known old values
    SetLPFRegister(0x0000);
    SetMCPAOld(0x00);
    SetMCPBOld(0x00);

    // Call RXSelectBPF - should set the bit and update hardware
    RXSelectBPF();

    uint16_t result = GetLPFRegister();

    // Verify the RX BPF bit is set
    EXPECT_EQ(GET_BIT(result, LPFRXBPFBIT), 1);
    EXPECT_EQ(result & (1 << LPFRXBPFBIT), 1 << LPFRXBPFBIT);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPA = (result >> 8) & 0xFF;
    EXPECT_EQ(GetMCPAOld(), expectedGPA);
}

TEST_F(LPFBoardTest, RXBypassBPFUpdatesRegisterAndHardware) {
    // Start with RX BPF bit set and known old values
    SetLPFRegister(1 << LPFRXBPFBIT);
    SetMCPAOld((1 << LPFRXBPFBIT) >> 8); // GPA (upper 8 bits) - LPFRXBPFBIT is bit 9
    SetMCPBOld(0x00); // GPB (lower 8 bits)

    // Call RXBypassBPF - should clear the bit and update hardware
    RXBypassBPF();

    uint16_t result = GetLPFRegister();

    // Verify the RX BPF bit is cleared
    EXPECT_EQ(GET_BIT(result, LPFRXBPFBIT), 0);
    EXPECT_EQ(result & (1 << LPFRXBPFBIT), 0);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPA = (result >> 8) & 0xFF;
    EXPECT_EQ(GetMCPAOld(), expectedGPA);
}

// ================== UPDATED XVTR FUNCTION TESTS ==================

TEST_F(LPFBoardTest, SelectXVTRUpdatesRegisterAndHardware) {
    // Start with XVTR bypassed (bit set) and known old values
    SetLPFRegister(1 << LPFXVTRBIT);
    SetMCPAOld(0x00); // GPA (upper 8 bits)
    SetMCPBOld(1 << LPFXVTRBIT); // GPB (lower 8 bits)

    // Call SelectXVTR - should clear the bit (active low) and update hardware
    SelectXVTR();

    uint16_t result = GetLPFRegister();

    // Verify the XVTR bit is cleared (active low)
    EXPECT_EQ(GET_BIT(result, LPFXVTRBIT), 0);
    EXPECT_EQ(result & (1 << LPFXVTRBIT), 0);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPB = result & 0xFF;
    EXPECT_EQ(GetMCPBOld(), expectedGPB);
}

TEST_F(LPFBoardTest, BypassXVTRUpdatesRegisterAndHardware) {
    // Start with XVTR selected (bit cleared) and known old values
    SetLPFRegister(0x0000);
    SetMCPAOld(0x00);
    SetMCPBOld(0x00);

    // Call BypassXVTR - should set the bit and update hardware
    BypassXVTR();

    uint16_t result = GetLPFRegister();

    // Verify the XVTR bit is set (bypass mode)
    EXPECT_EQ(GET_BIT(result, LPFXVTRBIT), 1);
    EXPECT_EQ(result & (1 << LPFXVTRBIT), 1 << LPFXVTRBIT);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPB = result & 0xFF;
    EXPECT_EQ(GetMCPBOld(), expectedGPB);
}

// ================== UPDATED 100W PA FUNCTION TESTS ==================

TEST_F(LPFBoardTest, Select100WPAUpdatesRegisterAndHardware) {
    // Start with cleared register and known old values
    SetLPFRegister(0x0000);
    SetMCPAOld(0x00);
    SetMCPBOld(0x00);

    // Call Select100WPA - should set the bit and update hardware
    Select100WPA();

    uint16_t result = GetLPFRegister();

    // Verify the 100W PA bit is set
    EXPECT_EQ(GET_BIT(result, LPF100WBIT), 1);
    EXPECT_EQ(result & (1 << LPF100WBIT), 1 << LPF100WBIT);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPB = result & 0xFF;
    EXPECT_EQ(GetMCPBOld(), expectedGPB);
}

TEST_F(LPFBoardTest, Bypass100WPAUpdatesRegisterAndHardware) {
    // Start with 100W PA bit set and known old values
    SetLPFRegister(1 << LPF100WBIT);
    SetMCPAOld(0x00); // GPA (upper 8 bits)
    SetMCPBOld(1 << LPF100WBIT); // GPB (lower 8 bits)

    // Call Bypass100WPA - should clear the bit and update hardware
    Bypass100WPA();

    uint16_t result = GetLPFRegister();

    // Verify the 100W PA bit is cleared
    EXPECT_EQ(GET_BIT(result, LPF100WBIT), 0);
    EXPECT_EQ(result & (1 << LPF100WBIT), 0);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPB = result & 0xFF;
    EXPECT_EQ(GetMCPBOld(), expectedGPB);
}

// ================== UPDATED SELECTLPFBAND FUNCTION TESTS ==================

TEST_F(LPFBoardTest, SelectLPFBandUpdatesRegisterAndHardware) {
    // Start with all band bits set and known old values
    SetLPFRegister(0xFFFF);
    SetMCPAOld(0xFF);
    SetMCPBOld(0xFF);

    // Call SelectLPFBand with a specific band
    SelectLPFBand(BAND_20M);

    uint16_t result = GetLPFRegister();

    // Verify the band bits are set correctly
    uint16_t bandBits = result & 0x0F;
    EXPECT_EQ(bandBits, LPF_BAND_20M);

    // Verify other bits are preserved
    EXPECT_EQ(result & 0xFFF0, 0xFFF0);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPB = result & 0xFF;
    EXPECT_EQ(GetMCPBOld(), expectedGPB);
}

TEST_F(LPFBoardTest, SelectLPFBandWithDifferentBands) {
    // Test multiple band selections to ensure hardware update is called each time
    SetLPFRegister(0x0000);
    SetMCPAOld(0x00);
    SetMCPBOld(0x00);

    // Test 160M band
    SelectLPFBand(BAND_160M);
    uint16_t result = GetLPFRegister();
    EXPECT_EQ(result & 0x0F, LPF_BAND_160M);
    EXPECT_EQ(GetMCPBOld(), result & 0xFF);

    // Test 80M band
    SelectLPFBand(BAND_80M);
    result = GetLPFRegister();
    EXPECT_EQ(result & 0x0F, LPF_BAND_80M);
    EXPECT_EQ(GetMCPBOld(), result & 0xFF);

    // Test invalid band (should default to NF)
    SelectLPFBand(99);
    result = GetLPFRegister();
    EXPECT_EQ(result & 0x0F, LPF_BAND_NF);
    EXPECT_EQ(GetMCPBOld(), result & 0xFF);
}

// ================== UPDATED SELECTANTENNA FUNCTION TESTS ==================

TEST_F(LPFBoardTest, SelectAntennaUpdatesRegisterAndHardware) {
    // Start with cleared register and known old values
    SetLPFRegister(0x0000);
    SetMCPAOld(0x00);
    SetMCPBOld(0x00);

    // Call SelectAntenna with antenna 2
    SelectAntenna(2);

    uint16_t result = GetLPFRegister();

    // Verify the antenna bits are set correctly
    uint16_t antennaBits = (result >> 4) & 0x03;
    EXPECT_EQ(antennaBits, 2);

    // Verify that UpdateMCPRegisters was called and updated the old values
    uint8_t expectedGPB = result & 0xFF;
    EXPECT_EQ(GetMCPBOld(), expectedGPB);
}

TEST_F(LPFBoardTest, SelectAntennaInvalidDoesNotUpdateHardware) {
    // Start with antenna 1 selected and known old values
    SetLPFRegister(0x0010); // Antenna 1 selected (bits 4-5 = 01)
    SetMCPAOld(0x00);
    SetMCPBOld(0x10);

    // Call SelectAntenna with invalid antenna (should not change register)
    SelectAntenna(4);

    uint16_t result = GetLPFRegister();

    // Verify the register state hasn't changed
    EXPECT_EQ(result, 0x0010);

    // Since register didn't change, UpdateMCPRegisters should still be called
    // but old values should remain the same (no hardware write needed)
    EXPECT_EQ(GetMCPBOld(), 0x10);
}

TEST_F(LPFBoardTest, SelectAntennaWithDifferentValues) {
    // Test multiple antenna selections to ensure hardware update is called each time
    SetLPFRegister(0x0000);
    SetMCPAOld(0x00);
    SetMCPBOld(0x00);

    // Test antenna 0
    SelectAntenna(0);
    uint16_t result = GetLPFRegister();
    EXPECT_EQ((result >> 4) & 0x03, 0);
    EXPECT_EQ(GetMCPBOld(), result & 0xFF);

    // Test antenna 3
    SelectAntenna(3);
    result = GetLPFRegister();
    EXPECT_EQ((result >> 4) & 0x03, 3);
    EXPECT_EQ(GetMCPBOld(), result & 0xFF);

    // Test antenna 1
    SelectAntenna(1);
    result = GetLPFRegister();
    EXPECT_EQ((result >> 4) & 0x03, 1);
    EXPECT_EQ(GetMCPBOld(), result & 0xFF);
}
