#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/LPFBoard.h"

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

    uint8_t gpbState = (GetLPFRegister() >> 8) & 0xFF; // LPF_GPB_state equivalent
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
