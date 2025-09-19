#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/BPFBoard.h"

class BPFBoardTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset global state before each test
        hardwareRegister = 0;
        bit_results.BPF_I2C_present = false;
    }

    void TearDown() override {
        // Clean up after each test
        hardwareRegister = 0;
        bit_results.BPF_I2C_present = false;
    }
};

// ================== BPF_WORD MACRO TESTS ==================

TEST_F(BPFBoardTest, BPFWordMacroCalculation) {
    // Test BPF_WORD macro for different band values

    // Test band 0 (60M): should give 0x0100
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)0 & 0x0000000F) << BPFBAND0BIT);
    uint16_t result = BPF_WORD;
    EXPECT_EQ(result, 0x0100);

    // Test band 1 (160M): should give 0x0200
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)1 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x0200);

    // Test band 2 (80M): should give 0x0400
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)2 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x0400);

    // Test band 3 (40M): should give 0x0800
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)3 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x0800);

    // Test band 4 (30M): should give 0x1000
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)4 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x1000);

    // Test band 5 (20M): should give 0x2000
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)5 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x2000);

    // Test band 6 (17M): should give 0x4000
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)6 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x4000);

    // Test band 7 (15M): should give 0x8000
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)7 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x8000);

    // Test band 8 (12M): should give 0x0001
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)8 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x0001);

    // Test band 9 (10M): should give 0x0002
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)9 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x0002);

    // Test band 10 (6M): should give 0x0004
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)10 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x0004);

    // Test band 15 (BYPASS): should give 0x0008 (special case: 0x0080 >> 4)
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)15 & 0x0000000F) << BPFBAND0BIT);
    result = BPF_WORD;
    EXPECT_EQ(result, 0x0008);
}

// ================== SET_BPF_BAND MACRO TESTS ==================

TEST_F(BPFBoardTest, SetBPFBandMacro) {
    // Test setting BPF band in hardware register
    hardwareRegister = 0x12345678; // Start with some value

    // Set band 5 (20M)
    SET_BPF_BAND(5);
    uint32_t expectedRegister = (0x12345678 & 0x0FFFFFFF) | (((uint32_t)5 & 0x0000000F) << BPFBAND0BIT);
    EXPECT_EQ(hardwareRegister, expectedRegister);
    EXPECT_EQ(GET_BPF_BAND, 5);

    // Set band 10 (6M)
    SET_BPF_BAND(10);
    expectedRegister = (0x12345678 & 0x0FFFFFFF) | (((uint32_t)10 & 0x0000000F) << BPFBAND0BIT);
    EXPECT_EQ(hardwareRegister, expectedRegister);
    EXPECT_EQ(GET_BPF_BAND, 10);

    // Set band 0 (60M)
    SET_BPF_BAND(0);
    expectedRegister = (0x12345678 & 0x0FFFFFFF) | (((uint32_t)0 & 0x0000000F) << BPFBAND0BIT);
    EXPECT_EQ(hardwareRegister, expectedRegister);
    EXPECT_EQ(GET_BPF_BAND, 0);
}

// ================== INITIALIZATION TESTS ==================

TEST_F(BPFBoardTest, InitializeBPFBoardSuccess) {
    // Mock successful I2C initialization
    // The mock Adafruit_MCP23X17 always returns true for begin_I2C

    errno_t result = InitializeBPFBoard();

    EXPECT_EQ(result, ESUCCESS);
    EXPECT_TRUE(bit_results.BPF_I2C_present);
}

TEST_F(BPFBoardTest, InitializeBPFBoardSetsCorrectBand) {
    // Set up a known band before initialization
    ED.currentBand[ED.activeVFO] = BAND_20M;

    errno_t result = InitializeBPFBoard();

    EXPECT_EQ(result, ESUCCESS);
    // Verify that the band was set correctly in the hardware register
    uint8_t expectedBCD = BandToBCD(BAND_20M);
    EXPECT_EQ(GET_BPF_BAND, expectedBCD);
}

// ================== BAND SELECTION TESTS ==================

TEST_F(BPFBoardTest, SelectBPFBandValidBands) {
    // Initialize first
    InitializeBPFBoard();

    // Test selecting different valid bands
    SelectBPFBand(BAND_40M);
    uint8_t expectedBCD = BandToBCD(BAND_40M);
    EXPECT_EQ(GET_BPF_BAND, expectedBCD);

    SelectBPFBand(BAND_20M);
    expectedBCD = BandToBCD(BAND_20M);
    EXPECT_EQ(GET_BPF_BAND, expectedBCD);

    SelectBPFBand(BAND_10M);
    expectedBCD = BandToBCD(BAND_10M);
    EXPECT_EQ(GET_BPF_BAND, expectedBCD);
}

TEST_F(BPFBoardTest, SelectBPFBandInvalidBand) {
    // Initialize first
    InitializeBPFBoard();

    // Test selecting band -1 (outside ham band)
    SelectBPFBand(-1);

    // Should set to bypass (LAST_BAND + 10)
    uint8_t expectedBCD = BandToBCD(LAST_BAND + 10);
    EXPECT_EQ(GET_BPF_BAND, expectedBCD);
}

TEST_F(BPFBoardTest, SelectBPFBandSameBandTwice) {
    // Initialize first
    InitializeBPFBoard();

    // Select a band
    SelectBPFBand(BAND_20M);
    uint8_t expectedBCD = BandToBCD(BAND_20M);
    EXPECT_EQ(GET_BPF_BAND, expectedBCD);

    // Select the same band again - should not cause issues
    SelectBPFBand(BAND_20M);
    EXPECT_EQ(GET_BPF_BAND, expectedBCD);
}

// ================== EDGE CASE TESTS ==================

TEST_F(BPFBoardTest, BPFWordBypassSpecialCase) {
    // Test the special case for bypass band (value 15)
    // According to the comments, bypass should give 0x0008 (0x0080 >> 4)
    hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)15 & 0x0000000F) << BPFBAND0BIT);
    uint16_t result = BPF_WORD;
    EXPECT_EQ(result, 0x0008);
}

TEST_F(BPFBoardTest, BandRangeTests) {
    // Test boundary conditions for band values
    for (int band = 0; band <= 15; band++) {
        hardwareRegister = (hardwareRegister & 0x0FFFFFFF) | (((uint32_t)band & 0x0000000F) << BPFBAND0BIT);
        uint16_t result = BPF_WORD;

        // Verify that the result is always a valid 16-bit value
        EXPECT_LE(result, 0xFFFF);
        EXPECT_GE(result, 0x0000);

        // Verify that GET_BPF_BAND returns the correct band
        EXPECT_EQ(GET_BPF_BAND, band);
    }
}

// ================== INTEGRATION TESTS ==================

TEST_F(BPFBoardTest, FullWorkflowTest) {
    // Test complete workflow: Initialize -> Select bands -> Verify state

    // Step 1: Initialize
    errno_t initResult = InitializeBPFBoard();
    EXPECT_EQ(initResult, ESUCCESS);
    EXPECT_TRUE(bit_results.BPF_I2C_present);

    // Step 2: Select various bands and verify
    const int32_t testBands[] = {BAND_160M, BAND_80M, BAND_40M, BAND_20M, BAND_15M, BAND_10M, BAND_6M};
    const int numBands = sizeof(testBands) / sizeof(testBands[0]);

    for (int i = 0; i < numBands; i++) {
        SelectBPFBand(testBands[i]);
        uint8_t expectedBCD = BandToBCD(testBands[i]);
        EXPECT_EQ(GET_BPF_BAND, expectedBCD) << "Failed for band " << testBands[i];

        // Verify BPF_WORD produces expected values
        uint16_t bpfWord = BPF_WORD;
        EXPECT_GT(bpfWord, 0) << "BPF_WORD should be non-zero for band " << testBands[i];
    }

    // Step 3: Test out-of-band frequency
    SelectBPFBand(-1);
    uint8_t bypassBCD = BandToBCD(LAST_BAND + 10);
    EXPECT_EQ(GET_BPF_BAND, bypassBCD);
}

TEST_F(BPFBoardTest, HardwareRegisterPreservation) {
    // Test that BPF band setting preserves other bits in hardwareRegister
    uint32_t originalValue = 0x0ABCDEF0; // Set lower 28 bits to a known pattern
    hardwareRegister = originalValue;

    // Set a band
    SET_BPF_BAND(7);

    // Verify that lower 28 bits are preserved
    uint32_t lowerBits = hardwareRegister & 0x0FFFFFFF;
    uint32_t originalLowerBits = originalValue & 0x0FFFFFFF;
    EXPECT_EQ(lowerBits, originalLowerBits);

    // Verify that the band was set correctly in upper 4 bits
    EXPECT_EQ(GET_BPF_BAND, 7);
}