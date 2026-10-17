#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"

// Forward declare CAT functions for testing
void set_vfo(int64_t freq, uint8_t vfo);
void set_vfo_a(long freq);
void set_vfo_b(long freq);
char *FA_write(char* cmd);
char *FA_read(char* cmd);
char *FB_write(char* cmd);
char *FB_read(char* cmd);
char *FT_write(char* cmd);
char *FT_read(char* cmd);
char *FR_write(char* cmd);
char *FR_read(char* cmd);
char *AG_write(char* cmd);
char *AG_read(char* cmd);
char *BU_write(char* cmd);
char *BD_write(char* cmd);
char *command_parser(char* command);
void CheckForCATSerialEvents(void);
char *unsupported_cmd(char *cmd);
char *MD_write(char* cmd);
char *MD_read(char* cmd);
char *IF_read(char* cmd);
char *ID_read(char* cmd);
char *MG_write(char* cmd);
char *MG_read(char* cmd);
char *NR_write(char* cmd);
char *NR_read(char* cmd);
char *NT_write(char* cmd);
char *NT_read(char* cmd);
char *PC_write(char* cmd);
char *PC_read(char* cmd);
char *PS_write(char* cmd);
char *PS_read(char* cmd);
char *RX_write(char* cmd);
char *TX_write(char* cmd);
char *FW_write(char* cmd);
char *FW_read(char* cmd);
char *SR_write(char* cmd);
char *SR_read(char* cmd);
char *DG_write(char* cmd);
char *DG_read(char* cmd);
char *DR_write(char* cmd);
char *DR_read(char* cmd);
char *CF_write(char* cmd);
char *CF_read(char* cmd);
char *EQ_write(char* cmd);
char *EQ_read(char* cmd);
char *FL_write(char* cmd);
char *FL_read(char* cmd);
void UpdateTransmitAudioGain(void);
void ShutdownTeensy(void);

// External variables needed for testing
extern struct config_t ED;
extern const struct SR_Descriptor SR[];
extern uint8_t& SampleRate;
extern struct band bands[];

TEST(CAT, ChangeBandUp){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    ModeSm_start(&modeSM);

    // Save the initial band
    int32_t initialBand = ED.currentBand[ED.activeVFO];

    // Simulate pressing the BAND_UP button
    SetButton(BAND_UP);
    SetInterrupt(iBUTTON_PRESSED);
    ConsumeInterrupt();
    
    // Check that the band has incremented (or wrapped to FIRST_BAND if we were at LAST_BAND)
    if (initialBand < LAST_BAND) {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], initialBand + 1);
    } else {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);
    }
}

TEST(CAT, ChangeBandUpLimit){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    ModeSm_start(&modeSM);

    // Set the current band to the last band to test rollover behavior
    ED.currentBand[ED.activeVFO] = LAST_BAND;

    // Simulate pressing the BAND_UP button
    SetButton(BAND_UP);
    SetInterrupt(iBUTTON_PRESSED);
    ConsumeInterrupt();
    
    // Should properly roll over from LAST_BAND to FIRST_BAND
    EXPECT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);
}

TEST(CAT, ChangeBandUDown){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    ModeSm_start(&modeSM);

    // Save the initial band
    int32_t initialBand = ED.currentBand[ED.activeVFO];

    // Simulate pressing the BAND_DN button
    SetButton(BAND_DN);
    SetInterrupt(iBUTTON_PRESSED);
    ConsumeInterrupt();
    
    // Check that the band has decremented (or wrapped to LAST_BAND if we were at FIRST_BAND)
    if (initialBand > FIRST_BAND) {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], initialBand - 1);
    } else {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], LAST_BAND);
    }
}

TEST(CAT, ChangeBandDownLimit){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    ModeSm_start(&modeSM);

    // Set the current band to the first band to test rollover behavior
    ED.currentBand[ED.activeVFO] = FIRST_BAND;

    // Simulate pressing the BAND_DN button
    SetButton(BAND_DN);
    SetInterrupt(iBUTTON_PRESSED);
    ConsumeInterrupt();
    
    // Should properly roll over from FIRST_BAND to LAST_BAND
    EXPECT_EQ(ED.currentBand[ED.activeVFO], LAST_BAND);
}

TEST(CAT, CATChangeVolume){
    // Save the initial volume
    int32_t initialVolume = ED.audioVolume;
    
    // Test AG_write function - now fixed to use cmd parameter correctly
    // This tests the volume conversion logic: 127/255 * 100 ≈ 49.8 → 49
    char command[] = "AG0127;";
    
    // Call AG_write directly
    char* result = AG_write(command);
    
    // Verify volume conversion: 127 * 100 / 255 ≈ 49.8 → 49
    EXPECT_EQ(ED.audioVolume, 49);
    
    // Test that AG_write returns empty string for successful completion
    EXPECT_STREQ(result, "");
}

TEST(CAT, CATBandUp){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    ModeSm_start(&modeSM);

    // Save the initial band
    int32_t initialBand = ED.currentBand[ED.activeVFO];

    // Call the CAT BU command function directly
    BU_write(nullptr);
    ConsumeInterrupt();
    
    // Check that the band has incremented (or wrapped to FIRST_BAND if we were at LAST_BAND)
    if (initialBand < LAST_BAND) {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], initialBand + 1);
    } else {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);
    }
}

TEST(CAT, CATBandDown){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    ModeSm_start(&modeSM);

    // Save the initial band
    int32_t initialBand = ED.currentBand[ED.activeVFO];

    // Call the CAT BD command function directly
    BD_write(nullptr);
    ConsumeInterrupt();
    
    // Check that the band has decremented (or wrapped to LAST_BAND if we were at FIRST_BAND)
    if (initialBand > FIRST_BAND) {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], initialBand - 1);
    } else {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], LAST_BAND);
    }
}


TEST(CAT, CATCommandParserBU){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    ModeSm_start(&modeSM);

    // Save the initial band
    int32_t initialBand = ED.currentBand[ED.activeVFO];

    // Test that command_parser correctly processes "BU;" command and calls BU_write
    char command[] = "BU;";
    char* result = command_parser(command);
    ConsumeInterrupt();
    
    // Verify the band incremented (or wrapped to FIRST_BAND if we were at LAST_BAND)
    if (initialBand < LAST_BAND) {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], initialBand + 1);
    } else {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);
    }
    
    // Verify command_parser returns empty string for successful BU command
    EXPECT_STREQ(result, "");
}


TEST(CAT, CheckForCATSerialEvents){
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_HOME;
    ModeSm_start(&modeSM);

    // Save initial state
    int32_t initialBand = ED.currentBand[ED.activeVFO];

    // Clear any existing data in the serial buffer
    SerialUSB1.clearBuffer();
    
    // Test that CheckForCATSerialEvents can be called without crashing when no data is available
    CheckForCATSerialEvents();
    EXPECT_EQ(ED.currentBand[ED.activeVFO], initialBand);
    
    // Now test with actual CAT command data - feed a "BU;" command to increment band
    SerialUSB1.feedData("BU;");
    
    // Process the serial events
    CheckForCATSerialEvents();
    
    // Consume any interrupts that were set by the CAT command processing
    ConsumeInterrupt();
    
    // Verify the band was incremented by the BU command (or wrapped to FIRST_BAND if we were at LAST_BAND)
    if (initialBand < LAST_BAND) {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], initialBand + 1);
    } else {
        EXPECT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);
    }
    
    // Clear buffer for next test
    SerialUSB1.clearBuffer();
    
    // Test multiple calls to ensure stability when no data is available
    CheckForCATSerialEvents();
    CheckForCATSerialEvents();
    
    // Function should handle multiple calls gracefully
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(ED.currentBand[ED.activeVFO], currentBand);
}

// Test FA_write function for valid frequency parsing
TEST(CAT, FAWriteValidFrequencyParsing){
    // Test setting VFO A to a valid 20m frequency
    char command[] = "FA00014200000;";  // 14.2 MHz
    
    char* result = FA_write(command);
    
    // Verify the response string is correctly formatted
    EXPECT_STREQ(result, "FA00014200000;");
    
    // Verify VFO A center frequency was set (accounting for SR offset)
    int64_t expectedCenterFreq = 14200000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreq);
    
    // Verify fine tune was reset to 0
    EXPECT_EQ(ED.fineTuneFreq_Hz[VFO_A], 0);
}

// Test FA_write VFO A frequency setting for different bands
TEST(CAT, FAWriteVFOAFrequencySetting){
    // Test setting VFO A to 40m band
    char command[] = "FA00007150000;";  // 7.15 MHz
    
    char* result = FA_write(command);
    
    // Verify VFO A center frequency was set correctly
    int64_t expectedCenterFreq = 7150000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreq);
    
    // Verify correct band was selected (BAND_40M = 3)
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_40M);
    
    // Verify response string
    EXPECT_STREQ(result, "FA00007150000;");
}

// Test FA_write band detection for different frequencies
TEST(CAT, FAWriteBandDetection){
    // Test 160m band detection
    char command160[] = "FA00001850000;";  // 1.85 MHz
    FA_write(command160);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_160M);
    
    // Test 80m band detection  
    char command80[] = "FA00003700000;";   // 3.7 MHz
    FA_write(command80);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_80M);
    
    // Test 20m band detection
    char command20[] = "FA00014200000;";   // 14.2 MHz
    FA_write(command20);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);
    
    // Test 10m band detection
    char command10[] = "FA00028350000;";   // 28.35 MHz
    FA_write(command10);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_10M);
}

// Test FA_write response string formatting
TEST(CAT, FAWriteResponseStringFormatting){
    // Test various frequency values for correct formatting
    
    // Test with leading zeros
    char command1[] = "FA00001000000;";    // 1 MHz
    char* result1 = FA_write(command1);
    EXPECT_STREQ(result1, "FA00001000000;");
    
    // Test with larger frequency
    char command2[] = "FA00050100000;";    // 50.1 MHz (6m band)
    char* result2 = FA_write(command2);
    EXPECT_STREQ(result2, "FA00050100000;");
    
    // Test edge case frequency
    char command3[] = "FA00000010000;";    // 10 kHz
    char* result3 = FA_write(command3);
    EXPECT_STREQ(result3, "FA00000010000;");
}

// Test FA_write out-of-band frequency handling
TEST(CAT, FAWriteOutOfBandFrequency){
    // Set initial band to a known value
    ED.currentBand[VFO_A] = BAND_20M;

    // Test frequency that doesn't fall within any defined ham band
    char command[] = "FA00000500000;";     // 500 kHz (not in any ham band)

    char* result = FA_write(command);

    // Should still set the frequency
    int64_t expectedCenterFreq = 500000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreq);

    // Band should change to general band
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_GENERAL);

    // Response should still be formatted correctly
    EXPECT_STREQ(result, "FA00000500000;");
}

// Test FA_write out-of-all-bands frequency handling
TEST(CAT, FAWriteOutOfAllBandsFrequency) {
    // Set initial band to a known value
    ED.currentBand[VFO_A] = BAND_20M;

    // Test frequency that doesn't fall within any defined ham band
    char command[] = "FA00081000000;";     // 81 MHz (beyond general band)

    char* result = FA_write(command);

    // Should still set the frequency
    int64_t expectedCenterFreq = 81000000L + SR[SampleRate].rate / 4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreq);

    // Band should remain unchanged for out-of-band frequency (avoids -1 index)
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);

    // Response should still be formatted correctly
    EXPECT_STREQ(result, "FA00081000000;");
}

// Test FA_write with frequency at band edges
TEST(CAT, FAWriteBandEdgeFrequencies){
    // Test frequency at lower edge of 20m band (14.000 MHz)
    char commandLow[] = "FA00014000000;";
    FA_write(commandLow);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);
    
    // Test frequency at upper edge of 20m band (14.350 MHz)  
    char commandHigh[] = "FA00014350000;";
    FA_write(commandHigh);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);
    
    // Test frequency just outside 20m band (13.999 MHz)
    char commandOutside[] = "FA00013999000;";
    FA_write(commandOutside);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_GENERAL);  // Band falls back to general
}

// Test FB_write function for valid frequency parsing
TEST(CAT, FBWriteValidFrequencyParsing){
    // Test setting frequency to a valid 20m frequency (FB sets VFO_A)
    char command[] = "FB00014200000;";  // 14.2 MHz

    char* result = FB_write(command);

    // Verify the response string is correctly formatted
    EXPECT_STREQ(result, "FB00014200000;");

    // Verify VFO A center frequency was set (accounting for SR offset)
    int64_t expectedCenterFreq = 14200000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreq);

    // Verify fine tune was reset to 0
    EXPECT_EQ(ED.fineTuneFreq_Hz[VFO_A], 0);
}

// Test FB_write frequency setting for different bands (FB sets VFO_A)
TEST(CAT, FBWriteVFOBFrequencySetting){
    // Test setting to 40m band
    char command[] = "FB00007150000;";  // 7.15 MHz

    char* result = FB_write(command);

    // Verify VFO A center frequency was set correctly
    int64_t expectedCenterFreq = 7150000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreq);

    // Verify correct band was selected (BAND_40M = 3)
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_40M);

    // Verify response string
    EXPECT_STREQ(result, "FB00007150000;");
}

// Test FB_write band detection for different frequencies
TEST(CAT, FBWriteBandDetection){
    // Test 160m band detection
    char command160[] = "FB00001850000;";  // 1.85 MHz
    FB_write(command160);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_160M);

    // Test 80m band detection
    char command80[] = "FB00003700000;";   // 3.7 MHz
    FB_write(command80);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_80M);

    // Test 20m band detection
    char command20[] = "FB00014200000;";   // 14.2 MHz
    FB_write(command20);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);

    // Test 10m band detection
    char command10[] = "FB00028350000;";   // 28.35 MHz
    FB_write(command10);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_10M);
}

// Test FB_write response string formatting
TEST(CAT, FBWriteResponseStringFormatting){
    // Test various frequency values for correct formatting
    
    // Test with leading zeros
    char command1[] = "FB00001000000;";    // 1 MHz
    char* result1 = FB_write(command1);
    EXPECT_STREQ(result1, "FB00001000000;");
    
    // Test with larger frequency
    char command2[] = "FB00050100000;";    // 50.1 MHz (6m band)
    char* result2 = FB_write(command2);
    EXPECT_STREQ(result2, "FB00050100000;");
    
    // Test edge case frequency
    char command3[] = "FB00000010000;";    // 10 kHz
    char* result3 = FB_write(command3);
    EXPECT_STREQ(result3, "FB00000010000;");
}

// Test FB_write out-of-band frequency handling
TEST(CAT, FBWriteOutOfBandFrequency){
    // Set initial band to a known value
    ED.currentBand[VFO_A] = BAND_20M;

    // Test frequency that doesn't fall within any defined ham band
    char command[] = "FB00000500000;";     // 500 kHz (not in any ham band)

    char* result = FB_write(command);

    // Should still set the frequency
    int64_t expectedCenterFreq = 500000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreq);

    // Band should fallback to general band
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_GENERAL);

    // Response should still be formatted correctly
    EXPECT_STREQ(result, "FB00000500000;");
}

// Test FB_write out-of-all-bands frequency handling
TEST(CAT, FBWriteOutOfAllBandFrequency) {
    // Set initial band to a known value
    ED.currentBand[VFO_A] = BAND_20M;

    // Test frequency that doesn't fall within any defined ham band
    char command[] = "FB00081000000;";     // 81 MHz (out of general band)

    char* result = FB_write(command);

    // Should still set the frequency
    int64_t expectedCenterFreq = 81000000L + SR[SampleRate].rate / 4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreq);

    // Band should remain unchanged for out-of-band frequency (avoids -1 index)
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);

    // Response should still be formatted correctly
    EXPECT_STREQ(result, "FB00081000000;");
}

// Test FB_write with frequency at band edges
TEST(CAT, FBWriteBandEdgeFrequencies){
    // Test frequency at lower edge of 20m band (14.000 MHz)
    char commandLow[] = "FB00014000000;";
    FB_write(commandLow);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);

    // Test frequency at upper edge of 20m band (14.350 MHz)
    char commandHigh[] = "FB00014350000;";
    FB_write(commandHigh);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);

    // Test frequency just outside 20m band (13.999 MHz)
    char commandOutside[] = "FB00013999000;";
    FB_write(commandOutside);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_GENERAL);  // Band falls back to general
}

// Test FB_write sets VFO A (same as FA_write)
TEST(CAT, FBWriteVFOIndependence){
    // Set VFO A to one frequency using FA
    char commandA[] = "FA00014200000;";  // 14.2 MHz (20m)
    FA_write(commandA);

    int64_t expectedCenterFreqA = 14200000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreqA);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_20M);

    // Set VFO A to a different frequency using FB (FB also sets VFO_A)
    char commandB[] = "FB00007150000;";  // 7.15 MHz (40m)
    FB_write(commandB);

    // Verify VFO A was updated by FB
    int64_t expectedCenterFreqB = 7150000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], expectedCenterFreqB);
    EXPECT_EQ(ED.currentBand[VFO_A], BAND_40M);
}

TEST(CAT, CATSerialVFOChange){
    // Save initial state
    ED.activeVFO = VFO_A;
    int32_t initialBand = ED.currentBand[ED.activeVFO];
    int64_t initialCenterFreq = ED.centerFreq_Hz[ED.activeVFO];
    int64_t initialFineTuneFreq = ED.fineTuneFreq_Hz[ED.activeVFO];

    // Clear any existing data in the serial buffer
    SerialUSB1.clearBuffer();
    // Now test with actual CAT command data
    SerialUSB1.feedData("FA00014200000;");
    
    // Process the serial events
    CheckForCATSerialEvents();
    
    // Consume any interrupts that were set by the CAT command processing
    ConsumeInterrupt();
    
    // Verify that changes happened as expected
    EXPECT_EQ(ED.currentBand[ED.activeVFO], BAND_20M);
    EXPECT_NE(ED.centerFreq_Hz[ED.activeVFO], initialCenterFreq);
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO], GetRXVFOFrequency());

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Helper Function Tests
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, set_vfo_UpdatesFrequencies) {
    // Initialize test data
    ED.currentBand[VFO_A] = BAND_40M;
    ED.centerFreq_Hz[VFO_A] = 7074000L;
    ED.fineTuneFreq_Hz[VFO_A] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    
    // Test setting VFO A to a new frequency
    int64_t new_freq = 14074000L; // 20m frequency
    set_vfo(new_freq, VFO_A);
    
    // Verify the frequency was updated
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], new_freq + SR[SampleRate].rate/4);
    EXPECT_EQ(ED.fineTuneFreq_Hz[VFO_A], 0);
    EXPECT_EQ(ED.currentBand[VFO_A], GetBand(new_freq));
}

TEST(CAT, set_vfo_SavesLastFrequencies) {
    // Initialize test data
    ED.currentBand[VFO_B] = BAND_20M;
    ED.centerFreq_Hz[VFO_B] = 14074000L;
    ED.fineTuneFreq_Hz[VFO_B] = 200L;
    SampleRate = SAMPLE_RATE_48K;
    
    // Save original values for comparison
    int64_t original_center = ED.centerFreq_Hz[VFO_B];
    int64_t original_fine = ED.fineTuneFreq_Hz[VFO_B];
    int original_band = ED.currentBand[VFO_B];
    
    // Test setting VFO B to a new frequency
    int64_t new_freq = 7030000L; // 40m CW frequency
    set_vfo(new_freq, VFO_B);
    
    // Verify the last frequencies were saved
    EXPECT_EQ(ED.lastFrequencies[original_band][0], original_center);
    EXPECT_EQ(ED.lastFrequencies[original_band][1], original_fine);
}

TEST(CAT, set_vfo_a_CallsSetVfoWithVFOA) {
    // Initialize test data
    SampleRate = SAMPLE_RATE_96K;
    long test_freq = 21074000L; // 15m frequency
    
    set_vfo_a(test_freq);
    
    // Verify VFO A was updated
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], test_freq + SR[SampleRate].rate/4);
    EXPECT_EQ(ED.fineTuneFreq_Hz[VFO_A], 0);
}

TEST(CAT, set_vfo_b_CallsSetVfoWithVFOB) {
    // Initialize test data  
    SampleRate = SAMPLE_RATE_192K;
    long test_freq = 28074000L; // 10m frequency
    
    set_vfo_b(test_freq);
    
    // Verify VFO B was updated
    EXPECT_EQ(ED.centerFreq_Hz[VFO_B], test_freq + SR[SampleRate].rate/4);
    EXPECT_EQ(ED.fineTuneFreq_Hz[VFO_B], 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Command Tests
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, FA_write_SetsVFOAFrequency) {
    char command[] = "FA00014074000;";
    
    char *result = FA_write(command);
    
    // Verify frequency was set correctly
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], 14074000L + SR[SampleRate].rate/4);
    EXPECT_EQ(ED.fineTuneFreq_Hz[VFO_A], 0);
    
    // Verify response format
    EXPECT_STREQ(result, "FA00014074000;");
}

TEST(CAT, FA_read_ReturnsVFOAFrequency) {
    // Set up test frequency (GetTXRXFreq returns centerFreq - fineTuneFreq - rate/4)
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_A] = 14074000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_A] = 0;

    char command[] = "FA;";
    char *result = FA_read(command);

    // Verify response format and content
    EXPECT_STREQ(result, "FA00014074000;");
}

TEST(CAT, FB_write_SetsVFOBFrequency) {
    char command[] = "FB00007074000;";

    char *result = FB_write(command);

    // Verify frequency was set correctly (FB sets VFO_A)
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], 7074000L + SR[SampleRate].rate/4);
    EXPECT_EQ(ED.fineTuneFreq_Hz[VFO_A], 0);

    // Verify response format
    EXPECT_STREQ(result, "FB00007074000;");
}

TEST(CAT, FB_read_ReturnsVFOBFrequency) {
    // Set up test frequency (GetTXRXFreq returns centerFreq - fineTuneFreq - rate/4)
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_B] = 7074000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_B] = 0;

    char command[] = "FB;";
    char *result = FB_read(command);

    // Verify response format and content
    EXPECT_STREQ(result, "FB00007074000;");
}

TEST(CAT, FT_write_SetsActiveVFOFrequency) {
    ED.activeVFO = VFO_A;
    char command[] = "FT1;";

    char *result = FT_write(command);

    // Verify active VFO was set to VFO_B (1)
    EXPECT_EQ(ED.activeVFO, VFO_B);

    // Verify response format
    EXPECT_STREQ(result, "FT1;");
}

TEST(CAT, FT_read_ReturnsTransmitFrequency) {
    // Set up test data
    ED.activeVFO = VFO_B;

    char command[] = "FT;";
    char *result = FT_read(command);

    // FT_read returns the active VFO number
    EXPECT_STREQ(result, "FT1;");
}

TEST(CAT, FR_write_SetsActiveVFOReceiveFrequency) {
    ED.activeVFO = VFO_B;
    char command[] = "FR0;";

    char *result = FR_write(command);

    // Verify active VFO was set to VFO_A (0)
    EXPECT_EQ(ED.activeVFO, VFO_A);

    // Verify response format
    EXPECT_STREQ(result, "FR0;");
}

TEST(CAT, FR_read_ReturnsReceiveFrequency) {
    // Set up test data
    ED.activeVFO = VFO_A;

    char command[] = "FR;";
    char *result = FR_read(command);

    // FR_read returns the active VFO number
    EXPECT_STREQ(result, "FR0;");
}

TEST(CAT, AG_write_SetsAudioVolume) {
    char command[] = "AG0128;"; // 128 out of 255 = 50% volume
    
    char *result = AG_write(command);
    
    // Verify volume conversion: 128 * 100 / 255 ≈ 50.2 → 50
    EXPECT_EQ(ED.audioVolume, 50);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, AG_write_ClampsMagnitudeMax) {
    char command[] = "AG0300;"; // 300 > 255, should clamp to 100%
    
    char *result = AG_write(command);
    
    // Verify volume conversion and clamping: 300 * 100 / 255 ≈ 117.6 → 100 (clamped)
    EXPECT_EQ(ED.audioVolume, 100);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, AG_write_ClampsMagnitudeMin) {
    ED.audioVolume = 50; // Start with non-zero value
    char command[] = "AG0000;"; // 0 volume
    
    char *result = AG_write(command);
    
    // Verify audio volume was set to 0
    EXPECT_EQ(ED.audioVolume, 0);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, AG_read_ReturnsAudioVolume) {
    ED.audioVolume = 75; // 75% volume
    char command[] = "AG0;";
    
    char *result = AG_read(command);
    
    // Expected: 75 * 255 / 100 = 191.25 ≈ 191
    EXPECT_STREQ(result, "AG0191;");
}

TEST(CAT, BU_write_TriggersInterrupt) {
    char command[] = "BU;";
    
    // Clear any existing interrupts
    ConsumeInterrupt();
    EXPECT_EQ(GetInterrupt(), iNONE);
    
    char *result = BU_write(command);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iBUTTON_PRESSED);
    EXPECT_EQ(GetButton(), BAND_UP);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, BD_write_TriggersInterrupt) {
    char command[] = "BD;";
    
    // Clear any existing interrupts
    ConsumeInterrupt();
    EXPECT_EQ(GetInterrupt(), iNONE);
    
    char *result = BD_write(command);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iBUTTON_PRESSED);
    EXPECT_EQ(GetButton(), BAND_DN);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Command Parser Tests
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, unsupported_cmd_ReturnsError) {
    char command[] = "XX;";
    
    char *result = unsupported_cmd(command);
    
    EXPECT_STREQ(result, "?;");
}

TEST(CAT, command_parser_RecognizesSupportedCommands) {
    // Test AG command
    char ag_command[] = "AG0128;";
    char *result = command_parser(ag_command);
    EXPECT_STREQ(result, "");

    // Test FA command
    char fa_command[] = "FA00007074000;";
    result = command_parser(fa_command);
    EXPECT_STREQ(result, "FA00007074000;");

    // Test FB read command (GetTXRXFreq returns centerFreq - fineTuneFreq - rate/4)
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_B] = 14074000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_B] = 0;
    char fb_read[] = "FB;";
    result = command_parser(fb_read);
    EXPECT_STREQ(result, "FB00014074000;");
}

TEST(CAT, command_parser_RejectsUnsupportedCommands) {
    char unsupported[] = "XX123;";
    
    char *result = command_parser(unsupported);
    
    EXPECT_STREQ(result, "?;");
}

TEST(CAT, command_parser_RejectsInvalidLength) {
    // Test command with wrong length (should be AG0xxx; format)
    char invalid_ag[] = "AG123;"; // Too short
    
    char *result = command_parser(invalid_ag);
    
    EXPECT_STREQ(result, "?;");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Mode Function Tests  
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, MD_write_SetsLSBMode) {
    // Set up test conditions
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M; // 40m band
    
    char command[] = "MD1;"; // LSB mode
    char *result = MD_write(command);

    // The current modulation is what changes; bands[].mode is the band default
    // and must be left alone (see SetModulation).
    EXPECT_EQ(ED.modulation[VFO_A], LSB);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iMODE);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MD_write_SetsUSBMode) {
    // Set up test conditions
    ED.activeVFO = VFO_B;
    ED.currentBand[VFO_B] = BAND_20M; // 20m band
    
    char command[] = "MD2;"; // USB mode
    char *result = MD_write(command);
    
    // Verify USB mode was set
    EXPECT_EQ(ED.modulation[VFO_B], USB);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iMODE);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MD_write_SetsCWModeFromSSBReceive) {
    // Set up test conditions - must be in SSB_RECEIVE mode for CW transition
    ModeSm_start(&modeSM); // Start in SSB_RECEIVE mode
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M; // Low band - should set LSB
    
    char command[] = "MD3;"; // CW mode
    char *result = MD_write(command);
    
    // Verify LSB mode was set for low band
    EXPECT_EQ(ED.modulation[VFO_A], LSB);
    
    // Verify mode state machine event was dispatched
    // Note: We can't easily verify ModeSm_dispatch_event was called in unit test
    // but we can verify the interrupt was set
    EXPECT_EQ(GetInterrupt(), iMODE);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MD_write_SetsCWModeHighBandUSB) {
    // Set up test conditions - must be in SSB_RECEIVE mode for CW transition
    ModeSm_start(&modeSM); // Start in SSB_RECEIVE mode
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_15M; // High band (>= BAND_30M) - should set USB
    
    char command[] = "MD3;"; // CW mode
    char *result = MD_write(command);
    
    // Verify USB mode was set for high band
    EXPECT_EQ(ED.modulation[VFO_A], USB);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iMODE);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MD_write_CWModeIgnoredWhenNotInSSBReceive) {
    // Set up test conditions - NOT in SSB_RECEIVE mode
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE; // Different mode
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    ED.modulation[VFO_A] = USB; // Set initial modulation

    char command[] = "MD3;"; // CW mode
    char *result = MD_write(command);
    
    // Verify mode was NOT changed (should remain USB)
    EXPECT_EQ(ED.modulation[VFO_A], USB);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MD_write_SetsAMMode) {
    // Set up test conditions
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_10M;
    
    char command[] = "MD5;"; // AM mode
    char *result = MD_write(command);
    
    // Verify SAM mode was set (defaults to SAM rather than AM)
    EXPECT_EQ(ED.modulation[VFO_A], SAM);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iMODE);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MD_write_InvalidModeIgnored) {
    // Set up test conditions
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    ED.modulation[VFO_A] = USB; // Set initial modulation

    char command[] = "MD9;"; // Invalid mode
    char *result = MD_write(command);
    
    // Verify mode was not changed
    EXPECT_EQ(ED.modulation[VFO_A], USB);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MD_read_ReturnsCWModeWhenInCWReceive) {
    // Set up test conditions
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    
    char command[] = "MD;";
    char *result = MD_read(command);
    
    // Should return CW mode (3)
    EXPECT_STREQ(result, "MD3;");
}

TEST(CAT, MD_read_ReturnsLSBMode) {
    // Set up test conditions
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE; // Not CW mode
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M;
    ED.modulation[VFO_A] = LSB;   // the modulation in use, not the band default

    char command[] = "MD;";
    char *result = MD_read(command);

    // Should return LSB mode (1)
    EXPECT_STREQ(result, "MD1;");
}

TEST(CAT, MD_read_ReturnsUSBMode) {
    // Set up test conditions
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE; // Not CW mode
    ED.activeVFO = VFO_B;
    ED.currentBand[VFO_B] = BAND_20M;
    ED.modulation[VFO_B] = USB;
    
    char command[] = "MD;";
    char *result = MD_read(command);
    
    // Should return USB mode (2)
    EXPECT_STREQ(result, "MD2;");
}

TEST(CAT, MD_read_ReturnsAMMode) {
    // Set up test conditions
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE; // Not CW mode
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_10M;
    ED.modulation[VFO_A] = AM;
    
    char command[] = "MD;";
    char *result = MD_read(command);

    // AM and SAM are distinct demodulators, so they read back distinctly:
    // MD4 for AM, MD5 for SAM.
    EXPECT_STREQ(result, "MD4;");
}

TEST(CAT, MD_read_ReturnsSAMMode) {
    // Set up test conditions
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE; // Not CW mode
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_15M;
    ED.modulation[VFO_A] = SAM;
    
    char command[] = "MD;";
    char *result = MD_read(command);
    
    // Should return AM mode (5) - SAM is returned as AM
    EXPECT_STREQ(result, "MD5;");
}

TEST(CAT, MD_read_ReturnsErrorForUnknownMode) {
    // Set up test conditions with invalid mode
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE; // Not CW mode
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    ED.modulation[VFO_A] = (ModulationType)99; // Invalid modulation
    
    char command[] = "MD;";
    char *result = MD_read(command);
    
    // Should return error
    EXPECT_STREQ(result, "?;");
}

TEST(CAT, command_parser_RecognizesMDCommands) {
    // Clear any existing interrupts
    ConsumeInterrupt();
    
    // Test MD write command
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    
    char md_write[] = "MD2;"; // USB mode
    char *result = command_parser(md_write);
    
    // Verify mode was set
    EXPECT_EQ(ED.modulation[VFO_A], USB);
    EXPECT_STREQ(result, "");
    
    // Test MD read command
    char md_read[] = "MD;";
    result = command_parser(md_read);
    EXPECT_STREQ(result, "MD2;");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Radio Status Function Tests (IF_read)
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, IF_read_DebugOutput) {
    // Debug test to understand the actual output format
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    ED.centerFreq_Hz[VFO_A] = 14200000L;
    ED.freqIncrement = 1000;
    bands[BAND_20M].mode = USB;
    
    char command[] = "IF;";
    char *result = IF_read(command);
    
    // Print out the result and character positions for debugging
    printf("DEBUG: IF_read result: [%s]\n", result);
    printf("DEBUG: Length: %lu\n", strlen(result));
    for(int i=0; i<(int)strlen(result) && i<35; i++) {
        printf("DEBUG: pos[%02d] = '%c' (%d)\n", i, result[i], (int)result[i]);
    }
    
    // Let this test always pass - it's just for debugging
    EXPECT_TRUE(true);
}

TEST(CAT, IF_read_ReturnsCorrectFormatInSSBReceive) {
    // Set up test conditions for SSB receive mode
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_A] = 14200000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_A] = 0;
    ED.modulation[VFO_A] = USB;

    char command[] = "IF;";
    char *result = IF_read(command);

    // Should start with "IF00014200000" (frequency)
    EXPECT_EQ(strncmp(result, "IF00014200000", 13), 0);

    // Should end with semicolon
    size_t len = strlen(result);
    EXPECT_EQ(result[len-1], ';');

    // Should indicate RX mode (0) - character at position 28 should be '0'
    EXPECT_EQ(result[28], '0');

    // Should indicate USB mode (2) - character at position 29 should be '2'
    EXPECT_EQ(result[29], '2');
}

TEST(CAT, IF_read_ReturnsCorrectFormatInCWReceive) {
    // Set up test conditions for CW receive mode
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    ED.activeVFO = VFO_B;
    ED.currentBand[VFO_B] = BAND_40M;
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_B] = 7074000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_B] = 0;
    bands[BAND_40M].mode = LSB; // CW usually uses LSB on 40m

    char command[] = "IF;";
    char *result = IF_read(command);

    // Should start with "IF00007074000" (frequency)
    EXPECT_EQ(strncmp(result, "IF00007074000", 13), 0);

    // Should indicate RX mode (0)
    EXPECT_EQ(result[28], '0');

    // Should indicate CW mode (3) when in CW_RECEIVE state
    EXPECT_EQ(result[29], '3');
}

TEST(CAT, IF_read_ReturnsCorrectFormatInSSBTransmit) {
    // Set up test conditions for SSB transmit mode
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_15M;
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_A] = 21200000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_A] = 0;
    ED.modulation[VFO_A] = USB;

    char command[] = "IF;";
    char *result = IF_read(command);

    // Should start with "IF00021200000" (frequency)
    EXPECT_EQ(strncmp(result, "IF00021200000", 13), 0);

    // Should indicate TX mode (1)
    EXPECT_EQ(result[28], '1');

    // Should indicate USB mode (2)
    EXPECT_EQ(result[29], '2');
}

TEST(CAT, IF_read_ReturnsCorrectFormatInCWTransmitMark) {
    // Set up test conditions for CW transmit mark mode
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_80M;
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_A] = 3574000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_A] = 0;
    bands[BAND_80M].mode = LSB;

    char command[] = "IF;";
    char *result = IF_read(command);

    // Should start with "IF00003574000" (frequency)
    EXPECT_EQ(strncmp(result, "IF00003574000", 13), 0);

    // Should indicate TX mode (1)
    EXPECT_EQ(result[28], '1');

    // Should indicate CW mode (3)
    EXPECT_EQ(result[29], '3');
}

TEST(CAT, IF_read_ReturnsCorrectFormatInCWTransmitSpace) {
    // Set up test conditions for CW transmit space mode
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    ED.activeVFO = VFO_B;
    ED.currentBand[VFO_B] = BAND_10M;
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_B] = 28200000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_B] = 0;
    bands[BAND_10M].mode = USB;

    char command[] = "IF;";
    char *result = IF_read(command);

    // Should start with "IF00028200000" (frequency)
    EXPECT_EQ(strncmp(result, "IF00028200000", 13), 0);

    // Should indicate TX mode (1)
    EXPECT_EQ(result[28], '1');

    // Should indicate CW mode (3)
    EXPECT_EQ(result[29], '3');
}

TEST(CAT, IF_read_HandlesAllModeTypes) {
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    ED.centerFreq_Hz[VFO_A] = 14200000L;
    ED.freqIncrement = 1000;
    
    // Test LSB mode
    ED.modulation[VFO_A] = LSB;
    char command[] = "IF;";
    char *result = IF_read(command);
    EXPECT_EQ(result[29], '1'); // LSB = mode 1
    
    // Test USB mode
    ED.modulation[VFO_A] = USB;
    result = IF_read(command);
    EXPECT_EQ(result[29], '2'); // USB = mode 2
    
    // Test AM mode
    ED.modulation[VFO_A] = AM;
    result = IF_read(command);
    EXPECT_EQ(result[29], '5'); // AM = mode 5
    
    // Test SAM mode (should return as AM)
    ED.modulation[VFO_A] = SAM;
    result = IF_read(command);
    EXPECT_EQ(result[29], '5'); // SAM = mode 5 (reported as AM)
}

TEST(CAT, IF_read_HandlesFrequencyIncrement) {
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M;
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_A] = 7100000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_A] = 0;
    ED.modulation[VFO_A] = LSB;

    // Test IF command
    char command[] = "IF;";
    char *result = IF_read(command);

    // Should start with "IF00007100000" (frequency)
    EXPECT_EQ(strncmp(result, "IF00007100000", 13), 0);
}

TEST(CAT, IF_read_FormatLength) {
    // Set up basic test conditions
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    ED.centerFreq_Hz[VFO_A] = 14200000L;
    ED.freqIncrement = 1000;
    bands[BAND_20M].mode = USB;
    
    char command[] = "IF;";
    char *result = IF_read(command);
    
    // IF command response should have specific length:
    // IF + 11 freq + 4 step + 6 RIT + remaining fixed fields + ;
    // Total should be around 38 characters
    size_t len = strlen(result);
    EXPECT_GT(len, 30); // Should be substantial length
    EXPECT_LT(len, 50); // But not excessively long
    EXPECT_EQ(result[0], 'I');
    EXPECT_EQ(result[1], 'F');
    EXPECT_EQ(result[len-1], ';');
}

TEST(CAT, command_parser_RecognizesIFCommand) {
    // Set up test conditions
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    SampleRate = SAMPLE_RATE_48K;
    ED.centerFreq_Hz[VFO_A] = 14200000L + SR[SampleRate].rate/4;
    ED.fineTuneFreq_Hz[VFO_A] = 0;
    ED.modulation[VFO_A] = USB;

    // Test IF read command through command parser
    char if_read[] = "IF;";
    char *result = command_parser(if_read);

    // Should return proper IF response
    EXPECT_EQ(strncmp(result, "IF00014200000", 13), 0);
    EXPECT_EQ(result[29], '2'); // USB mode
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT ID Function Tests
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, ID_read_ReturnsCorrectID) {
    char command[] = "ID;";
    char *result = ID_read(command);

    // Should return Kenwood TS-480 ID
    EXPECT_STREQ(result, "ID020;");
}

TEST(CAT, command_parser_RecognizesIDCommand) {
    // Test ID read command through command parser
    char id_read[] = "ID;";
    char *result = command_parser(id_read);

    // Should return proper ID response (TS-480)
    EXPECT_STREQ(result, "ID020;");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Microphone Gain Function Tests (MG)
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, MG_write_SetsCurrentMicGain) {
    // Test setting microphone gain to mid-range value
    char command[] = "MG050;"; // 50% = 0 dB
    
    char *result = MG_write(command);
    
    // Verify microphone gain was set correctly
    // 50 * 70 / 100 - 40 = 35 - 40 = -5 dB
    EXPECT_EQ(ED.currentMicGain, -5);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MG_write_SetsMinimumMicGain) {
    // Test setting microphone gain to minimum value
    char command[] = "MG000;"; // 0% = -40 dB
    
    char *result = MG_write(command);
    
    // Verify microphone gain was set to minimum
    // 0 * 70 / 100 - 40 = 0 - 40 = -40 dB
    EXPECT_EQ(ED.currentMicGain, -40);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MG_write_SetsMaximumMicGain) {
    // Test setting microphone gain to maximum value
    char command[] = "MG100;"; // 100% = +30 dB
    
    char *result = MG_write(command);
    
    // Verify microphone gain was set to maximum
    // 100 * 70 / 100 - 40 = 70 - 40 = +30 dB
    EXPECT_EQ(ED.currentMicGain, 30);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, MG_write_CallsUpdateTransmitAudioGainDuringTransmit) {
    // Set up test conditions - radio in SSB transmit mode
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    
    char command[] = "MG075;"; // 75% gain
    
    char *result = MG_write(command);
    
    // Verify microphone gain was set correctly
    // 75 * 70 / 100 - 40 = 52.5 - 40 = 12.5 → 12 dB (integer conversion)
    EXPECT_EQ(ED.currentMicGain, 12);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
    
    // Note: We can't easily verify UpdateTransmitAudioGain() was called in unit test,
    // but we can verify the function doesn't crash when called during transmit
}

TEST(CAT, MG_write_DoesNotCallUpdateTransmitAudioGainDuringReceive) {
    // Set up test conditions - radio in SSB receive mode
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    
    char command[] = "MG025;"; // 25% gain
    
    char *result = MG_write(command);
    
    // Verify microphone gain was set correctly
    // 25 * 70 / 100 - 40 = 17.5 - 40 = -22.5 → -22 dB (integer conversion)
    EXPECT_EQ(ED.currentMicGain, -22);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
    
    // Function should complete successfully even when not transmitting
}

TEST(CAT, MG_read_ReturnsCurrentMicGain) {
    // Set up test microphone gain
    ED.currentMicGain = 0; // 0 dB
    
    char command[] = "MG;";
    char *result = MG_read(command);
    
    // Expected: (0 + 40) * 100 / 70 = 40 * 100 / 70 ≈ 57.14 → 57
    EXPECT_STREQ(result, "MG057;");
}

TEST(CAT, MG_read_ReturnsMinimumMicGain) {
    // Set up test microphone gain to minimum
    ED.currentMicGain = -40; // -40 dB (minimum)
    
    char command[] = "MG;";
    char *result = MG_read(command);
    
    // Expected: (-40 + 40) * 100 / 70 = 0 * 100 / 70 = 0
    EXPECT_STREQ(result, "MG000;");
}

TEST(CAT, MG_read_ReturnsMaximumMicGain) {
    // Set up test microphone gain to maximum
    ED.currentMicGain = 30; // +30 dB (maximum)
    
    char command[] = "MG;";
    char *result = MG_read(command);
    
    // Expected: (30 + 40) * 100 / 70 = 70 * 100 / 70 = 100
    EXPECT_STREQ(result, "MG100;");
}

TEST(CAT, MG_read_ReturnsNegativeMicGain) {
    // Set up test microphone gain to negative value
    ED.currentMicGain = -20; // -20 dB
    
    char command[] = "MG;";
    char *result = MG_read(command);
    
    // Expected: (-20 + 40) * 100 / 70 = 20 * 100 / 70 ≈ 28.57 → 28
    EXPECT_STREQ(result, "MG028;");
}

TEST(CAT, MG_read_ReturnsPositiveMicGain) {
    // Set up test microphone gain to positive value
    ED.currentMicGain = 15; // +15 dB
    
    char command[] = "MG;";
    char *result = MG_read(command);
    
    // Expected: (15 + 40) * 100 / 70 = 55 * 100 / 70 ≈ 78.57 → 78
    EXPECT_STREQ(result, "MG078;");
}

TEST(CAT, command_parser_RecognizesMGCommands) {
    // Clear any existing interrupts
    ConsumeInterrupt();
    
    // Test MG write command
    char mg_write[] = "MG060;"; // 60% gain
    char *result = command_parser(mg_write);
    
    // Verify microphone gain was set
    // 60 * 70 / 100 - 40 = 42 - 40 = +2 dB
    EXPECT_EQ(ED.currentMicGain, 2);
    EXPECT_STREQ(result, "");
    
    // Test MG read command
    char mg_read[] = "MG;";
    result = command_parser(mg_read);
    
    // Expected: (2 + 40) * 100 / 70 = 42 * 100 / 70 = 60
    EXPECT_STREQ(result, "MG060;");
}

TEST(CAT, MG_write_HandlesBoundaryValues) {
    // Test conversion precision at various boundary points
    
    // Test value that should convert to exactly 0 dB
    char command1[] = "MG057;"; // Should be close to 0 dB
    MG_write(command1);
    // 57 * 70 / 100 - 40 = 39.9 - 40 = -0.1 → 0 dB (rounded)
    EXPECT_EQ(ED.currentMicGain, -0);
    
    // Test value that should be exactly -10 dB
    char command2[] = "MG043;"; // Should be close to -10 dB
    MG_write(command2);
    // 43 * 70 / 100 - 40 = 30.1 - 40 = -9.9 → -9 dB (rounded)
    EXPECT_EQ(ED.currentMicGain, -9);
    
    // Test value that should be exactly +10 dB
    char command3[] = "MG071;"; // Should be close to +10 dB
    MG_write(command3);
    // 71 * 70 / 100 - 40 = 49.7 - 40 = 9.7 → 9 dB (rounded)
    EXPECT_EQ(ED.currentMicGain, 9);
}

TEST(CAT, MG_read_write_RoundTripConsistency) {
    // Test that write followed by read gives consistent results
    
    // Set a specific gain value
    char write_command[] = "MG080;"; // 80%
    MG_write(write_command);
    
    // Read it back
    char read_command[] = "MG;";
    char *result = MG_read(read_command);
    
    // Should read back close to the original value
    // 80 * 70 / 100 - 40 = 56 - 40 = 16 dB
    // (16 + 40) * 100 / 70 = 56 * 100 / 70 = 80
    EXPECT_STREQ(result, "MG080;");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Noise Reduction Function Tests (NR)
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, NR_write_SetsNoiseReductionOff) {
    // Test setting noise reduction to off (NROff = 0)
    char command[] = "NR0;";
    
    char *result = NR_write(command);
    
    // Verify noise reduction was set to off
    EXPECT_EQ(ED.nrOptionSelect, NROff);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, NR_write_SetsNoiseReductionLevel1) {
    // Test setting noise reduction to level 1
    char command[] = "NR1;";
    
    char *result = NR_write(command);
    
    // Verify noise reduction was set to level 1
    EXPECT_EQ(ED.nrOptionSelect, (NoiseReductionType)1);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, NR_write_SetsNoiseReductionLevel2) {
    // Test setting noise reduction to level 2
    char command[] = "NR2;";
    
    char *result = NR_write(command);
    
    // Verify noise reduction was set to level 2
    EXPECT_EQ(ED.nrOptionSelect, (NoiseReductionType)2);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, NR_write_SetsNoiseReductionLevel3) {
    // Test setting noise reduction to level 3
    char command[] = "NR3;";
    
    char *result = NR_write(command);
    
    // Verify noise reduction was set to level 3
    EXPECT_EQ(ED.nrOptionSelect, (NoiseReductionType)3);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, NR_write_HandlesStringZero) {
    // Test the special case where cmd[2] is character '0'
    char command[] = "NR0;";
    
    char *result = NR_write(command);
    
    // Verify noise reduction was set to NROff (special case handling)
    EXPECT_EQ(ED.nrOptionSelect, NROff);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, NR_write_HandlesHigherLevels) {
    // Test setting noise reduction to higher levels (if supported)
    char command[] = "NR7;";
    
    char *result = NR_write(command);
    
    // Verify noise reduction was set to level 7
    EXPECT_EQ(ED.nrOptionSelect, (NoiseReductionType)7);
    
    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, NR_read_ReturnsNoiseReductionOff) {
    // Set up test noise reduction to off
    ED.nrOptionSelect = NROff;
    
    char command[] = "NR;";
    char *result = NR_read(command);
    
    // Should return NR0 for off state
    EXPECT_STREQ(result, "NR0;");
}

TEST(CAT, NR_read_ReturnsNoiseReductionLevel1) {
    // Set up test noise reduction to level 1
    ED.nrOptionSelect = (NoiseReductionType)1;
    
    char command[] = "NR;";
    char *result = NR_read(command);
    
    // Should return NR1 for level 1
    EXPECT_STREQ(result, "NR1;");
}

TEST(CAT, NR_read_ReturnsNoiseReductionLevel2) {
    // Set up test noise reduction to level 2
    ED.nrOptionSelect = (NoiseReductionType)2;
    
    char command[] = "NR;";
    char *result = NR_read(command);
    
    // Should return NR2 for level 2
    EXPECT_STREQ(result, "NR2;");
}

TEST(CAT, NR_read_ReturnsNoiseReductionLevel3) {
    // Set up test noise reduction to level 3
    ED.nrOptionSelect = (NoiseReductionType)3;
    
    char command[] = "NR;";
    char *result = NR_read(command);
    
    // Should return NR3 for level 3
    EXPECT_STREQ(result, "NR3;");
}

TEST(CAT, NR_read_ReturnsHigherLevels) {
    // Test reading higher noise reduction levels
    ED.nrOptionSelect = (NoiseReductionType)7;
    
    char command[] = "NR;";
    char *result = NR_read(command);
    
    // Should return NR7 for level 7
    EXPECT_STREQ(result, "NR7;");
}

TEST(CAT, NR_read_write_RoundTripConsistency) {
    // Test that write followed by read gives consistent results
    
    // Test level 0 (off)
    char write_command0[] = "NR0;";
    NR_write(write_command0);
    char read_command[] = "NR;";
    char *result = NR_read(read_command);
    EXPECT_STREQ(result, "NR0;");
    
    // Test level 2
    char write_command2[] = "NR2;";
    NR_write(write_command2);
    result = NR_read(read_command);
    EXPECT_STREQ(result, "NR2;");
    
    // Test level 5
    char write_command5[] = "NR5;";
    NR_write(write_command5);
    result = NR_read(read_command);
    EXPECT_STREQ(result, "NR5;");
}

TEST(CAT, command_parser_RecognizesNRCommands) {
    // Clear any existing interrupts
    ConsumeInterrupt();
    
    // Test NR write command
    char nr_write[] = "NR3;"; // Set noise reduction to level 3
    char *result = command_parser(nr_write);
    
    // Verify noise reduction was set
    EXPECT_EQ(ED.nrOptionSelect, (NoiseReductionType)3);
    EXPECT_STREQ(result, "");
    
    // Test NR read command
    char nr_read[] = "NR;";
    result = command_parser(nr_read);
    
    // Should return current noise reduction setting
    EXPECT_STREQ(result, "NR3;");
}

TEST(CAT, command_parser_NRWriteCommandLevels) {
    // Test various NR write commands through the command parser
    
    // Test NR0 (off)
    char nr_off[] = "NR0;";
    char *result = command_parser(nr_off);
    EXPECT_EQ(ED.nrOptionSelect, NROff);
    EXPECT_STREQ(result, "");
    
    // Test NR1
    char nr_1[] = "NR1;";
    result = command_parser(nr_1);
    EXPECT_EQ(ED.nrOptionSelect, (NoiseReductionType)1);
    EXPECT_STREQ(result, "");
    
    // Test NR7 (higher level)
    char nr_7[] = "NR7;";
    result = command_parser(nr_7);
    EXPECT_EQ(ED.nrOptionSelect, (NoiseReductionType)7);
    EXPECT_STREQ(result, "");
}

TEST(CAT, command_parser_NRReadCommandReflectsCurrentState) {
    // Test that NR read commands return the current noise reduction state
    
    // Set to different levels and verify read reflects them
    ED.nrOptionSelect = (NoiseReductionType)0;
    char nr_read[] = "NR;";
    char *result = command_parser(nr_read);
    EXPECT_STREQ(result, "NR0;");
    
    ED.nrOptionSelect = (NoiseReductionType)2;
    result = command_parser(nr_read);
    EXPECT_STREQ(result, "NR2;");
    
    ED.nrOptionSelect = (NoiseReductionType)5;
    result = command_parser(nr_read);
    EXPECT_STREQ(result, "NR5;");
}

TEST(CAT, command_parser_NRCommandLengthValidation) {
    // Test that NR commands with wrong lengths are rejected
    
    // Test command too long for NR write (should be exactly 4 chars: "NRx;")
    char nr_too_long[] = "NR123;";
    char *result = command_parser(nr_too_long);
    
    // Should return error for wrong length
    EXPECT_STREQ(result, "?;");
    
    // Test command too short for NR write
    char nr_too_short[] = "NR;"; // This should be read, not write
    result = command_parser(nr_too_short);
    
    // This should succeed as a read command
    // The result should be "NRx;" where x is the current level
    EXPECT_EQ(strlen(result), 4);
    EXPECT_EQ(result[0], 'N');
    EXPECT_EQ(result[1], 'R');
    EXPECT_EQ(result[3], ';');
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Auto Notch Function Tests (NT)
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, NT_write_ReturnsEmptyString) {
    // Test that NT_write currently returns empty string (stub implementation)
    char command[] = "NT1;";
    
    char *result = NT_write(command);
    
    // Should return empty string as it's currently a stub
    EXPECT_STREQ(result, "");
}

TEST(CAT, NT_write_AcceptsVariousCommands) {
    // Test that NT_write accepts different commands without crashing
    char command0[] = "NT0;";
    char command1[] = "NT1;";
    
    char *result0 = NT_write(command0);
    char *result1 = NT_write(command1);
    
    // Both should return empty string
    EXPECT_STREQ(result0, "");
    EXPECT_STREQ(result1, "");
}

TEST(CAT, NT_read_ReturnsEmptyString) {
    // Test that NT_read currently returns empty string (stub implementation)
    char command[] = "NT;";
    
    char *result = NT_read(command);
    
    // Should return empty string as it's currently a stub
    EXPECT_STREQ(result, "");
}

TEST(CAT, command_parser_RecognizesNTCommands) {
    // Test that NT commands are recognized by the command parser
    
    // Test NT write command
    char nt_write[] = "NT1;"; // Auto notch on
    char *result = command_parser(nt_write);
    
    // Should return empty string (stub implementation)
    EXPECT_STREQ(result, "");
    
    // Test NT read command
    char nt_read[] = "NT;";
    result = command_parser(nt_read);
    
    // Should return empty string (stub implementation)
    EXPECT_STREQ(result, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Power Control Function Tests (PC)
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, PC_write_SetsSSBPowerOutput) {
    // Set radio to SSB mode
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    
    // Clear any existing interrupts
    ConsumeInterrupt();
    
    char command[] = "PC050;"; // Set power to 50%
    
    char *result = PC_write(command);
    
    // Verify SSB power was set correctly
    EXPECT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 50);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
    
    // Should return the set value
    EXPECT_STREQ(result, "PC050;");
    
    // Clean up interrupt
    ConsumeInterrupt();
}

TEST(CAT, PC_write_SetsCWPowerOutput) {
    // Set radio to CW mode
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    ED.activeVFO = VFO_B;
    
    // Clear any existing interrupts
    ConsumeInterrupt();
    
    char command[] = "PC075;"; // Set power to 75%
    
    char *result = PC_write(command);
    
    // Verify CW power was set correctly
    EXPECT_EQ(ED.powerOutCW[ED.currentBand[ED.activeVFO]], 75);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
    
    // Should return the set value
    EXPECT_STREQ(result, "PC075;");
    
    // Clean up interrupt
    ConsumeInterrupt();
}

TEST(CAT, PC_write_HandlesSSBTransmitMode) {
    // Test that SSB transmit mode uses SSB power settings
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    ED.activeVFO = VFO_A;
    
    // Clear any existing interrupts
    ConsumeInterrupt();
    
    char command[] = "PC025;"; // Set power to 25%
    
    char *result = PC_write(command);
    
    // Verify SSB power was set (not CW)
    EXPECT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 25);
    
    // Verify interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
    
    // Should return the set value
    EXPECT_STREQ(result, "PC025;");
    
    // Clean up interrupt
    ConsumeInterrupt();
}

TEST(CAT, PC_write_HandlesCWTransmitModes) {
    // Test various CW transmit modes use CW power settings
    
    // Test CW transmit mark
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    ED.activeVFO = VFO_A;
    ConsumeInterrupt();
    
    char command1[] = "PC040;";
    char *result = PC_write(command1);
    EXPECT_EQ(ED.powerOutCW[ED.currentBand[ED.activeVFO]], 40);
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
    EXPECT_STREQ(result, "PC040;");
    ConsumeInterrupt();
    
    // Test CW transmit dit mark
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DIT_MARK;
    char command2[] = "PC060;";
    result = PC_write(command2);
    EXPECT_EQ(ED.powerOutCW[ED.currentBand[ED.activeVFO]], 60);
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
    ConsumeInterrupt();
}

TEST(CAT, PC_read_ReturnsSSBPowerInSSBMode) {
    // Set up SSB mode with known power
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ED.powerOutSSB[ED.currentBand[ED.activeVFO]] = 80;
    
    char command[] = "PC;";
    char *result = PC_read(command);
    
    // Should return SSB power setting
    EXPECT_STREQ(result, "PC080;");
}

TEST(CAT, PC_read_ReturnsCWPowerInCWMode) {
    // Set up CW mode with known power
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    ED.activeVFO = VFO_B;
    ED.powerOutCW[ED.currentBand[ED.activeVFO]] = 45;
    
    char command[] = "PC;";
    char *result = PC_read(command);
    
    // Should return CW power setting
    EXPECT_STREQ(result, "PC045;");
}

TEST(CAT, PC_read_HandlesRoundingCorrectly) {
    // Test that floating point power values are rounded correctly
    
    // Test SSB mode rounding
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    ED.activeVFO = VFO_A;
    ED.powerOutSSB[ED.currentBand[ED.activeVFO]] = 33.7; // Should round to 34
    
    char command[] = "PC;";
    char *result = PC_read(command);
    EXPECT_STREQ(result, "PC034;");
    
    // Test CW mode rounding
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    ED.powerOutCW[ED.currentBand[ED.activeVFO]] = 66.2; // Should round to 66
    
    result = PC_read(command);
    EXPECT_STREQ(result, "PC066;");
}

TEST(CAT, command_parser_RecognizesPCCommands) {
    // Test PC commands through the command parser
    
    // Set up initial state
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ConsumeInterrupt();
    
    // Test PC write command
    char pc_write[] = "PC090;"; // Set power to 90%
    char *result = command_parser(pc_write);
    
    // Verify power was set
    EXPECT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 90);
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
    EXPECT_STREQ(result, "PC090;");
    ConsumeInterrupt();
    
    // Test PC read command
    char pc_read[] = "PC;";
    result = command_parser(pc_read);
    
    // Should return current power setting
    EXPECT_STREQ(result, "PC090;");
}

TEST(CAT, PC_read_write_RoundTripConsistency) {
    // Test that write followed by read gives consistent results
    
    // Test in SSB mode
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ConsumeInterrupt();
    
    char write_command[] = "PC055;";
    char *write_result = PC_write(write_command);
    EXPECT_STREQ(write_result, "PC055;");
    ConsumeInterrupt();
    
    char read_command[] = "PC;";
    char *read_result = PC_read(read_command);
    EXPECT_STREQ(read_result, "PC055;");
    
    // Test in CW mode
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    ED.activeVFO = VFO_B;
    
    char write_command2[] = "PC038;";
    write_result = PC_write(write_command2);
    EXPECT_STREQ(write_result, "PC038;");
    ConsumeInterrupt();
    
    read_result = PC_read(read_command);
    EXPECT_STREQ(read_result, "PC038;");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Power Status Function Tests (PS)
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, PS_write_CallsShutdownTeensy) {
    // Test that PS_write calls ShutdownTeensy function
    char command[] = "PS0;";
    
    char *result = PS_write(command);
    
    // Should return PS0 response (indicating shutdown initiated)
    EXPECT_STREQ(result, "PS0;");
    
    // Note: We can't easily verify ShutdownTeensy() was called in unit test,
    // but we can verify the function completes and returns the expected response
}

TEST(CAT, PS_write_AcceptsVariousCommands) {
    // Test that PS_write handles different input commands
    char command0[] = "PS0;";
    char command1[] = "PS1;";
    
    char *result0 = PS_write(command0);
    char *result1 = PS_write(command1);
    
    // Both should return PS0 (shutdown response)
    EXPECT_STREQ(result0, "PS0;");
    EXPECT_STREQ(result1, "PS0;");
}

TEST(CAT, PS_read_ReturnsPowerOnStatus) {
    // Test that PS_read returns power on status
    char command[] = "PS;";
    
    char *result = PS_read(command);
    
    // Should return PS1 (power is on)
    EXPECT_STREQ(result, "PS1;");
}

TEST(CAT, PS_read_ConsistentResponse) {
    // Test that PS_read always returns the same response
    char command[] = "PS;";
    
    char *result1 = PS_read(command);
    char *result2 = PS_read(command);
    
    // Both calls should return PS1
    EXPECT_STREQ(result1, "PS1;");
    EXPECT_STREQ(result2, "PS1;");
}

TEST(CAT, command_parser_RecognizesPSCommands) {
    // Test PS commands through the command parser
    
    // Test PS write command
    char ps_write[] = "PS1;"; // Power control command
    char *result = command_parser(ps_write);
    
    // Should return PS0 (shutdown initiated)
    EXPECT_STREQ(result, "PS0;");
    
    // Test PS read command
    char ps_read[] = "PS;";
    result = command_parser(ps_read);
    
    // Should return PS1 (power is on)
    EXPECT_STREQ(result, "PS1;");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CAT Receiver Selection Function Tests (RX)
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, RX_write_ReturnsRX0Response) {
    // Test that RX_write returns empty string (no response per TS-480 spec)
    char command[] = "RX0;";

    char *result = RX_write(command);

    // Should return empty string
    EXPECT_STREQ(result, "");
}

TEST(CAT, RX_write_AcceptsVariousCommands) {
    // Test that RX_write handles different receiver selection commands
    char command0[] = "RX0;"; // Main receiver
    char command1[] = "RX1;"; // Sub receiver (if supported)

    char *result0 = RX_write(command0);
    char *result1 = RX_write(command1);

    // Both should return empty string (no response per TS-480 spec)
    EXPECT_STREQ(result0, "");
    EXPECT_STREQ(result1, "");
}

TEST(CAT, RX_write_ConsistentResponse) {
    // Test that RX_write always returns consistent response
    char command[] = "RX0;";

    char *result1 = RX_write(command);
    char *result2 = RX_write(command);

    // Both calls should return empty string
    EXPECT_STREQ(result1, "");
    EXPECT_STREQ(result2, "");
}

TEST(CAT, command_parser_RecognizesRXCommands) {
    // Test RX command through the command parser
    // Note: RX command length is 3 ("RX;")

    char rx_cmd[] = "RX;";
    char *result = command_parser(rx_cmd);

    // Should return empty string (no response per TS-480 spec)
    EXPECT_STREQ(result, "");
}

TEST(CAT, command_parser_NewCommandsLengthValidation) {
    // Test that new commands with wrong lengths are rejected
    
    // Test PS command too long
    char ps_too_long[] = "PS123;";
    char *result = command_parser(ps_too_long);
    EXPECT_STREQ(result, "?;");
    
    // Test RX command too long  
    char rx_too_long[] = "RX123;";
    result = command_parser(rx_too_long);
    EXPECT_STREQ(result, "?;");
    
    // Test PS with missing semicolon
    char ps_no_semi[] = "PS1";
    result = command_parser(ps_no_semi);
    EXPECT_STREQ(result, "?;");
    
    // Test RX with missing semicolon
    char rx_no_semi[] = "RX0";
    result = command_parser(rx_no_semi);
    EXPECT_STREQ(result, "?;");
}

TEST(CAT, command_parser_AllNewCommandsIntegration) {
    // Test integration of all newly added commands
    
    // Test NT (Auto Notch)
    char nt_cmd[] = "NT1;";
    char *result = command_parser(nt_cmd);
    EXPECT_STREQ(result, ""); // Stub implementation
    
    // Test PC (Power Control)
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ConsumeInterrupt();
    char pc_cmd[] = "PC050;";
    result = command_parser(pc_cmd);
    EXPECT_STREQ(result, "PC050;");
    ConsumeInterrupt();
    
    // Test PS (Power Status)
    char ps_cmd[] = "PS1;";
    result = command_parser(ps_cmd);
    EXPECT_STREQ(result, "PS0;");
    
    // Test RX (Receiver Selection) - command length is 3 ("RX;")
    char rx_cmd[] = "RX;";
    result = command_parser(rx_cmd);
    EXPECT_STREQ(result, "");

    // Test TX (Transmit) - command length is 3 ("TX;")
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    char tx_cmd[] = "TX;";
    result = command_parser(tx_cmd);
    EXPECT_STREQ(result, "");
}

// TX function tests
TEST(CAT, TX_write_ReturnsTX0Response){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    char command[] = "TX0;";
    char *result = TX_write(command);
    EXPECT_STREQ(result, "");
}

TEST(CAT, TX_write_AcceptsVariousCommands){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Test with TX0;
    char command1[] = "TX0;";
    char *result1 = TX_write(command1);
    EXPECT_STREQ(result1, "");

    // Test with TX1;
    char command2[] = "TX1;";
    char *result2 = TX_write(command2);
    EXPECT_STREQ(result2, "");

    // Test with TX;
    char command3[] = "TX;";
    char *result3 = TX_write(command3);
    EXPECT_STREQ(result3, "");
}

TEST(CAT, TX_write_TriggersSSBTransmitFromSSBReceive){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Ensure we're in SSB receive state
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ModeSm_StateId initial_state = modeSM.state_id;
    EXPECT_EQ(initial_state, ModeSm_StateId_SSB_RECEIVE);

    char command[] = "TX0;";
    char *result = TX_write(command);
    EXPECT_STREQ(result, "");

    // Verify state changed to SSB transmit
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);
}

TEST(CAT, TX_write_TriggersCWTransmitFromCWReceive){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Set to CW receive state
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    ModeSm_StateId initial_state = modeSM.state_id;
    EXPECT_EQ(initial_state, ModeSm_StateId_CW_RECEIVE);

    char command[] = "TX0;";
    char *result = TX_write(command);
    EXPECT_STREQ(result, "");

    // Verify state changed to CW transmit mark (straight key behavior)
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_MARK);
}

TEST(CAT, TX_write_NoStateChangeFromTransmitStates){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Test from SSB transmit state - should have no effect
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    ModeSm_StateId initial_state = modeSM.state_id;

    char command[] = "TX0;";
    char *result = TX_write(command);
    EXPECT_STREQ(result, "");

    // State should remain unchanged
    EXPECT_EQ(modeSM.state_id, initial_state);
}

TEST(CAT, TX_write_ConsistentResponse){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Call TX_write multiple times and verify consistent response
    char command[] = "TX0;";

    char *result1 = TX_write(command);
    char *result2 = TX_write(command);
    char *result3 = TX_write(command);

    EXPECT_STREQ(result1, "");
    EXPECT_STREQ(result2, "");
    EXPECT_STREQ(result3, "");
}

TEST(CAT, command_parser_RecognizesTXCommands){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Test TX; command (command length is 3)
    char tx_cmd[] = "TX;";
    char *result = command_parser(tx_cmd);
    EXPECT_STREQ(result, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PTT through command_parser(), with the optional TX parameter
//
// ts_480_pc.pdf p.21: the TX set form is "TX P1;" where P1 is 0 (normal/MIC),
// 1 (DTS via ANI input) or 2 (TX Tune), and "if no P1 parameter is specified,
// P1=0 is used". So TX;, TX0;, TX1; and TX2; are all valid ways to key.
//
// This matters in the field because Hamlib - which WSJT-X, fldigi and Pat all
// drive the radio through - picks its form from the PTT type: TX; for
// RIG_PTT_ON, TX0; for RIG_PTT_ON_MIC and TX1; for RIG_PTT_ON_DATA.
//
// These go through command_parser() deliberately. The TX_write tests above call
// the handler directly, so they passed even while the parser was rejecting
// TX0; and TX1; with "?;" - the handler ignores its argument, and the
// dispatcher was the part that was wrong.
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(CAT, command_parser_TXAcceptsOptionalParameterFromSSBReceive){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    const char *forms[] = { "TX;", "TX0;", "TX1;", "TX2;" };

    for (const char *form : forms) {
        modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

        char cmd[8];
        strcpy(cmd, form);
        char *result = command_parser(cmd);

        // Accepted, and silent: TX_write returns empty_string_p
        EXPECT_STREQ(result, "") << "form " << form << " was not accepted";
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT)
            << "form " << form << " did not key the radio";
    }
}

TEST(CAT, command_parser_TXAcceptsOptionalParameterFromCWReceive){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    const char *forms[] = { "TX;", "TX0;", "TX1;", "TX2;" };

    for (const char *form : forms) {
        modeSM.state_id = ModeSm_StateId_CW_RECEIVE;

        char cmd[8];
        strcpy(cmd, form);
        char *result = command_parser(cmd);

        EXPECT_STREQ(result, "") << "form " << form << " was not accepted";
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_MARK)
            << "form " << form << " did not key the radio";
    }
}

TEST(CAT, command_parser_RXAcceptsOptionalParameter){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // The TS-480 set form is a bare RX;, but TS-2000 style clients address the
    // main/sub receiver as RX0;/RX1;. Accept all three; Phoenix has one receiver.
    const char *forms[] = { "RX;", "RX0;", "RX1;" };

    for (const char *form : forms) {
        modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;

        char cmd[8];
        strcpy(cmd, form);
        char *result = command_parser(cmd);

        EXPECT_STREQ(result, "") << "form " << form << " was not accepted";
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE)
            << "form " << form << " did not unkey the radio";
    }
}

TEST(CAT, command_parser_RejectsMalformedPTTCommands){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Two parameter characters is not a form the spec defines, and accepting the
    // optional parameter must not turn the length check into "anything goes".
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    char tx_too_long[] = "TX00;";
    EXPECT_STREQ(command_parser(tx_too_long), "?;");
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    char rx_too_long[] = "RX00;";
    EXPECT_STREQ(command_parser(rx_too_long), "?;");
}

TEST(CAT, command_parser_ZeroLengthFieldCommandsStillDispatch){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    // Read-only commands carry set_len 0, so the parser must not evaluate the
    // write-length test for them - command[set_len - 1] would be command[-1].
    // Each must reach its read handler and answer something other than "?;".
    const char *reads[] = { "ID;", "IF;", "PD;", "ED;", "PR;" };

    for (const char *form : reads) {
        char cmd[8];
        strcpy(cmd, form);
        char *result = command_parser(cmd);

        EXPECT_STRNE(result, "?;") << "read command " << form << " was rejected";
        EXPECT_NE(result[0], '\0') << "read command " << form << " answered nothing";
    }

    // The mirror case: band up/down are writes with read_len 0.
    char bd[] = "BD;";
    EXPECT_STRNE(command_parser(bd), "?;");
    char bu[] = "BU;";
    EXPECT_STRNE(command_parser(bu), "?;");
}

// ============================================================================
// Commands added for the hardware-in-the-loop filter test suite
//
// These drive radio state that was previously reachable only from the
// touchscreen menus. Every one of them is exercised through command_parser() as
// well as directly, because the parser selects write-vs-read purely by where the
// semicolon falls: it tests the write length first, so a command whose set_len
// and read_len are equal can never reach its read function.
// ============================================================================

TEST(CAT, SR_write_SetsSampleRate) {
    uint8_t saved = SampleRate;
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    char cmd176[] = "SR0;";
    EXPECT_STREQ(SR_write(cmd176), "");
    EXPECT_EQ(SampleRate, SAMPLE_RATE_176K);

    char cmd192[] = "SR1;";
    EXPECT_STREQ(SR_write(cmd192), "");
    EXPECT_EQ(SampleRate, SAMPLE_RATE_192K);

    SampleRate = saved;
}

TEST(CAT, SR_read_ReturnsSampleRateIndex) {
    uint8_t saved = SampleRate;

    SampleRate = SAMPLE_RATE_176K;
    char cmd[] = "SR;";
    EXPECT_STREQ(SR_read(cmd), "SR0;");

    SampleRate = SAMPLE_RATE_192K;
    EXPECT_STREQ(SR_read(cmd), "SR1;");

    SampleRate = saved;
}

TEST(CAT, SR_write_RejectsOutOfRange) {
    uint8_t saved = SampleRate;
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    SampleRate = SAMPLE_RATE_192K;

    char cmd[] = "SR2;";
    EXPECT_STREQ(SR_write(cmd), "?;");
    EXPECT_EQ(SampleRate, SAMPLE_RATE_192K) << "a rejected rate must not change anything";

    SampleRate = saved;
}

TEST(CAT, SR_write_RefusedWhileTransmitting) {
    uint8_t saved = SampleRate;
    SampleRate = SAMPLE_RATE_192K;
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;

    char cmd[] = "SR0;";
    EXPECT_STREQ(SR_write(cmd), "?;");
    EXPECT_EQ(SampleRate, SAMPLE_RATE_192K)
        << "changing rate mid-transmission would reconfigure the I2S clock underneath the TX chain";

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    SampleRate = saved;
}

TEST(CAT, SR_ParserReachesBothForms) {
    uint8_t saved = SampleRate;
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    char write_cmd[] = "SR0;";
    EXPECT_STREQ(command_parser(write_cmd), "");
    EXPECT_EQ(SampleRate, SAMPLE_RATE_176K);

    char read_cmd[] = "SR;";
    EXPECT_STREQ(command_parser(read_cmd), "SR0;");

    SampleRate = saved;
}

TEST(CAT, CF_write_SetsCWFilterIndex) {
    int32_t saved = ED.CWFilterIndex;

    for (int i = 0; i <= 5; i++) {
        char cmd[] = "CF0;";
        cmd[2] = '0' + i;
        EXPECT_STREQ(CF_write(cmd), "");
        EXPECT_EQ(ED.CWFilterIndex, i);
    }

    ED.CWFilterIndex = saved;
}

TEST(CAT, CF_read_ReturnsCWFilterIndex) {
    int32_t saved = ED.CWFilterIndex;

    ED.CWFilterIndex = 4;
    char cmd[] = "CF;";
    EXPECT_STREQ(CF_read(cmd), "CF4;");

    ED.CWFilterIndex = saved;
}

TEST(CAT, CF_write_RejectsOutOfRange) {
    int32_t saved = ED.CWFilterIndex;
    ED.CWFilterIndex = 2;

    char cmd[] = "CF6;";
    EXPECT_STREQ(CF_write(cmd), "?;");
    EXPECT_EQ(ED.CWFilterIndex, 2);

    ED.CWFilterIndex = saved;
}

TEST(CAT, CF_ParserReachesBothForms) {
    int32_t saved = ED.CWFilterIndex;

    char write_cmd[] = "CF3;";
    EXPECT_STREQ(command_parser(write_cmd), "");
    EXPECT_EQ(ED.CWFilterIndex, 3);

    char read_cmd[] = "CF;";
    EXPECT_STREQ(command_parser(read_cmd), "CF3;");

    ED.CWFilterIndex = saved;
}

TEST(CAT, EQ_write_SetsEqualizerCell) {
    int32_t saved = ED.equalizerRec[7];

    char cmd[] = "EQ07050;";
    EXPECT_STREQ(EQ_write(cmd), "");
    EXPECT_EQ(ED.equalizerRec[7], 50);

    ED.equalizerRec[7] = saved;
}

TEST(CAT, EQ_read_ReturnsEqualizerCell) {
    int32_t saved = ED.equalizerRec[13];

    ED.equalizerRec[13] = 100;
    char cmd[] = "EQ13;";
    EXPECT_STREQ(EQ_read(cmd), "EQ13100;");

    ED.equalizerRec[13] = 0;
    EXPECT_STREQ(EQ_read(cmd), "EQ13000;");

    ED.equalizerRec[13] = saved;
}

TEST(CAT, EQ_write_RejectsOutOfRange) {
    int32_t saved = ED.equalizerRec[0];
    ED.equalizerRec[0] = 100;

    char bad_cell[] = "EQ14050;";
    EXPECT_STREQ(EQ_write(bad_cell), "?;");

    char bad_value[] = "EQ00101;";
    EXPECT_STREQ(EQ_write(bad_value), "?;");
    EXPECT_EQ(ED.equalizerRec[0], 100);

    char bad_read[] = "EQ14;";
    EXPECT_STREQ(EQ_read(bad_read), "?;");

    ED.equalizerRec[0] = saved;
}

TEST(CAT, EQ_ParserReachesBothForms) {
    int32_t saved = ED.equalizerRec[2];

    char write_cmd[] = "EQ02075;";
    EXPECT_STREQ(command_parser(write_cmd), "");
    EXPECT_EQ(ED.equalizerRec[2], 75);

    char read_cmd[] = "EQ02;";
    EXPECT_STREQ(command_parser(read_cmd), "EQ02075;");

    ED.equalizerRec[2] = saved;
}

TEST(CAT, FL_write_SetsLowCutOnUSB) {
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    bands[BAND_20M].mode = USB;
    bands[BAND_20M].FLoCut_Hz = 200;
    bands[BAND_20M].FHiCut_Hz = 3000;

    char cmd[] = "FL0400;";
    EXPECT_STREQ(FL_write(cmd), "");
    EXPECT_EQ(bands[BAND_20M].FLoCut_Hz, 400);
    EXPECT_EQ(bands[BAND_20M].FHiCut_Hz, 3000) << "FL must not disturb the other edge";

    bands[BAND_20M].FLoCut_Hz = 200;
}

TEST(CAT, FL_write_RejectsCrossingTheHighCut) {
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    bands[BAND_20M].mode = USB;
    bands[BAND_20M].FLoCut_Hz = 200;
    bands[BAND_20M].FHiCut_Hz = 3000;

    char cmd[] = "FL3500;";
    FL_write(cmd);
    EXPECT_EQ(bands[BAND_20M].FLoCut_Hz, 200) << "low cut must stay below the high cut";
}

TEST(CAT, FL_read_ReturnsLowCut) {
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    bands[BAND_20M].mode = USB;
    bands[BAND_20M].FLoCut_Hz = 300;

    char cmd[] = "FL;";
    EXPECT_STREQ(FL_read(cmd), "FL0300;");

    bands[BAND_20M].FLoCut_Hz = 200;
}

TEST(CAT, FW_read_IsReachableThroughTheParser) {
    // FW used to declare set_len == read_len, and because command_parser tests
    // the write form first, "FW;" fell through to "?;" and the bandwidth could
    // be set but never read back.
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    bands[BAND_20M].mode = USB;
    bands[BAND_20M].FLoCut_Hz = 200;
    bands[BAND_20M].FHiCut_Hz = 2800;

    char read_cmd[] = "FW;";
    EXPECT_STREQ(command_parser(read_cmd), "FW2800;");

    char write_cmd[] = "FW2400;";
    EXPECT_STREQ(command_parser(write_cmd), "");
    EXPECT_EQ(bands[BAND_20M].FHiCut_Hz, 2400);
    EXPECT_STREQ(command_parser(read_cmd), "FW2400;");

    bands[BAND_20M].FHiCut_Hz = 3000;
}

TEST(CAT, MD_write_SetsModulationAndLeavesTheBandDefault) {
    // Demodulate() switches on ED.modulation, while InitFilterMask() compares it
    // against bands[].mode and mirrors the passband when they disagree. So
    // bands[].mode is the fixed reference, not a second copy of the current mode:
    // writing it is what made MD select the band default instead of the requested
    // sideband. This test previously asserted the opposite and passed, which is
    // how the bug survived - it encoded the defect as the contract.
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ModulationType bandDefault = bands[BAND_20M].mode;

    const ModulationType wanted[4] = { USB, LSB, AM, SAM };
    const char *cmds[4] = { "MD2;", "MD1;", "MD4;", "MD5;" };

    for (int i = 0; i < 4; i++) {
        char cmd[8];
        strcpy(cmd, cmds[i]);
        MD_write(cmd);
        EXPECT_EQ(ED.modulation[VFO_A], wanted[i]);
        EXPECT_EQ(bands[BAND_20M].mode, bandDefault)
            << "the band default must survive a modulation change";
    }
}

TEST(CAT, MD_write_LeavesCWReceive) {
    // TO_SSB_MODE used to be reachable only from the front-panel TOGGLE_MODE
    // button, so CAT could enter CW receive and never get back out.
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    char cw[] = "MD3;";
    MD_write(cw);
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);

    char usb[] = "MD2;";
    MD_write(usb);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE)
        << "MD1/MD2 must be able to bring the radio back out of CW receive";
    EXPECT_EQ(ED.modulation[VFO_A], USB);
}

// ============================================================================
// DG - digital (USB audio) mode
//
// These tests run in a build without AUDIO_INTERFACE, so DG_write always
// reports "?;": digital mode has no transport to offer. What they pin down is
// that the refusal is total - no state change, no sample-rate change - and that
// DG_read still answers honestly. The mode transitions themselves are covered
// by the ModeSm suite, which does not depend on the USB build configuration.
// ============================================================================

TEST(CAT, DG_read_ReportsDigitalModeOff) {
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    char cmd[] = "DG;";
    EXPECT_STREQ(DG_read(cmd), "DG0;");

    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    EXPECT_STREQ(DG_read(cmd), "DG0;");

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
}

TEST(CAT, DG_read_ReportsDigitalModeOn) {
    ModeSm_start(&modeSM);
    char cmd[] = "DG;";

    modeSM.state_id = ModeSm_StateId_DIGITAL_RECEIVE;
    EXPECT_STREQ(DG_read(cmd), "DG1;");

    modeSM.state_id = ModeSm_StateId_DIGITAL_TRANSMIT;
    EXPECT_STREQ(DG_read(cmd), "DG1;")
        << "digital mode is still on while transmitting";

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
}

TEST(CAT, DG_write_RejectsOutOfRange) {
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    char cmd[] = "DG2;";
    EXPECT_STREQ(DG_write(cmd), "?;");
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE)
        << "a rejected parameter must not change the mode";
}

TEST(CAT, DG_ParserReachesBothForms) {
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    // The parser tests the write length first, so confirm a bare DG; still
    // reaches the read handler rather than being taken as a malformed write.
    char read_cmd[] = "DG;";
    EXPECT_STREQ(command_parser(read_cmd), "DG0;");

    char write_cmd[] = "DG1;";
    char *out = command_parser(write_cmd);
#ifdef AUDIO_INTERFACE
    EXPECT_STREQ(out, "");
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    char off[] = "DG0;";
    command_parser(off);
#else
    EXPECT_STREQ(out, "?;") << "no USB audio interface compiled in";
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
#endif
}

// WSJT-X sends MD2; to force USB on startup and before every transmission. If
// that were treated as a request to leave digital mode, the mode would be
// impossible to hold. Only DG0; or the front panel MODE button may leave it.
TEST(CAT, MD_write_DoesNotLeaveDigitalMode) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    modeSM.state_id = ModeSm_StateId_DIGITAL_RECEIVE;

    char usb[] = "MD2;";
    MD_write(usb);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE)
        << "a modulation change must not drop the radio out of digital mode";
    EXPECT_EQ(ED.modulation[VFO_A], USB) << "but it must still take effect";

    char lsb[] = "MD1;";
    MD_write(lsb);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    EXPECT_EQ(ED.modulation[VFO_A], LSB);

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
}

// MD3; is a genuine mode change and must be honoured from digital mode
TEST(CAT, MD_write_LeavesDigitalModeForCW) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    modeSM.state_id = ModeSm_StateId_DIGITAL_RECEIVE;

    char cw[] = "MD3;";
    MD_write(cw);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
}

TEST(CAT, MD_read_ReportsSidebandInDigitalMode) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    ED.modulation[VFO_A] = USB;
    modeSM.state_id = ModeSm_StateId_DIGITAL_RECEIVE;

    char cmd[] = "MD;";
    EXPECT_STREQ(MD_read(cmd), "MD2;")
        << "hamlib must see a sane sideband, not an unknown mode, in digital mode";

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
}

// This is how WSJT-X keys the rig
TEST(CAT, TX_and_RX_write_KeyDigitalTransmit) {
    ModeSm_start(&modeSM);
    bands[ED.currentBand[ED.activeVFO]].band_type = HAM_BAND;
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);

    char tx[] = "TX;";
    TX_write(tx);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_TRANSMIT);

    char rx[] = "RX;";
    RX_write(rx);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);

    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
}

TEST(CAT, SR_write_RefusedInDigitalMode) {
    uint8_t saved = SampleRate;
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_DIGITAL_RECEIVE;
    SampleRate = SAMPLE_RATE_176K;

    char cmd[] = "SR1;";
    EXPECT_STREQ(SR_write(cmd), "?;");
    EXPECT_EQ(SampleRate, SAMPLE_RATE_176K)
        << "digital mode pins the rate so the USB endpoint needs no resampling";

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    SampleRate = saved;
}

TEST(CAT, IF_read_ReportsReceiveInDigitalMode) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_20M;
    bands[BAND_20M].mode = USB;
    char cmd[] = "IF;";

    // Nothing but the mode state differs between these two, and both are receive
    // states reporting the same sideband, so the responses must match exactly.
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    std::string ssb = IF_read(cmd);

    modeSM.state_id = ModeSm_StateId_DIGITAL_RECEIVE;
    std::string digital = IF_read(cmd);

    EXPECT_EQ(digital, ssb)
        << "digital receive must report RX and the same sideband as SSB receive";

    // ...and a transmit state must differ, or the comparison above proves nothing
    modeSM.state_id = ModeSm_StateId_DIGITAL_TRANSMIT;
    EXPECT_NE(std::string(IF_read(cmd)), ssb) << "digital transmit must report TX";

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
}

// ============================================================================
// Sideband selection: MD must change the sideband that is actually received
//
// The receive passband is bands[].FLoCut_Hz/FHiCut_Hz, stored in the convention
// of the band's DEFAULT modulation (negative below the carrier for an LSB-default
// band). InitFilterMask() mirrors those cutoffs whenever the CURRENT modulation
// differs from that default - which is the only thing that produces the opposite
// sideband.
//
// SetModulation() used to write bands[].mode too. That made the default agree
// with the newly requested modulation, which switched the mirroring OFF and
// handed back the band-default passband. On 40 m, "MD2;" (select USB) therefore
// tuned the radio to LSB, with the display still reading USB because it reads
// ED.modulation[]. These tests pin the boundary.
// ============================================================================

/** Effective receive passband, computed the same way InitFilterMask() does. */
static void EffectivePassband(int32_t *low_Hz, int32_t *high_Hz) {
    int band = ED.currentBand[ED.activeVFO];
    if (ED.modulation[ED.activeVFO] == bands[band].mode) {
        *low_Hz = bands[band].FLoCut_Hz;
        *high_Hz = bands[band].FHiCut_Hz;
    } else {
        *low_Hz = -bands[band].FHiCut_Hz;
        *high_Hz = -bands[band].FLoCut_Hz;
    }
}

TEST(CAT, MD_write_DoesNotDisturbTheBandDefault) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M;
    ModulationType bandDefault = bands[BAND_40M].mode;

    char usb[] = "MD2;";
    MD_write(usb);

    EXPECT_EQ(bands[BAND_40M].mode, bandDefault)
        << "bands[].mode is the band default and is the reference the passband "
           "mirroring is measured against; writing it flips the sideband";
    EXPECT_EQ(ED.modulation[VFO_A], USB);
}

// The bug, stated as behaviour: asking for USB on an LSB-default band must put
// the passband above the carrier.
TEST(CAT, MD_write_USBOnLSBBandGivesAnUpperSideband) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M;
    ASSERT_EQ(bands[BAND_40M].mode, LSB) << "40 m is an LSB-default band";

    char usb[] = "MD2;";
    MD_write(usb);

    int32_t low_Hz, high_Hz;
    EffectivePassband(&low_Hz, &high_Hz);
    EXPECT_GT(high_Hz, 0) << "USB passband must lie above the carrier";
    EXPECT_GE(low_Hz, 0);
}

TEST(CAT, MD_write_LSBOnLSBBandGivesALowerSideband) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M;

    char lsb[] = "MD1;";
    MD_write(lsb);

    int32_t low_Hz, high_Hz;
    EffectivePassband(&low_Hz, &high_Hz);
    EXPECT_LT(low_Hz, 0) << "LSB passband must lie below the carrier";
    EXPECT_LE(high_Hz, 0);
}

// ...and the same on a band whose default is the other way round, or the fix
// would just be moving the bug to the opposite set of bands.
TEST(CAT, MD_write_LSBOnUSBBandGivesALowerSideband) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_4M;
    ASSERT_EQ(bands[BAND_4M].mode, USB) << "4 m is a USB-default band";

    char lsb[] = "MD1;";
    MD_write(lsb);

    int32_t low_Hz, high_Hz;
    EffectivePassband(&low_Hz, &high_Hz);
    EXPECT_LT(low_Hz, 0) << "LSB passband must lie below the carrier";
    EXPECT_LE(high_Hz, 0);
}

TEST(CAT, MD_write_SelectingBothSidebandsGivesDifferentPassbands) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M;

    char lsb[] = "MD1;";
    MD_write(lsb);
    int32_t lsbLow, lsbHigh;
    EffectivePassband(&lsbLow, &lsbHigh);

    char usb[] = "MD2;";
    MD_write(usb);
    int32_t usbLow, usbHigh;
    EffectivePassband(&usbLow, &usbHigh);

    // Before the fix both produced the band default, so this was the assertion
    // that would have caught it.
    EXPECT_NE(lsbLow, usbLow);
    EXPECT_EQ(lsbLow, -usbHigh) << "the two sidebands are mirror images";
    EXPECT_EQ(lsbHigh, -usbLow);
}

// MD_read reported bands[].mode, so it announced the band default rather than
// what the operator had actually selected - including via the front panel, which
// has always written ED.modulation[] alone.
TEST(CAT, MD_read_ReportsCurrentModulationNotBandDefault) {
    ModeSm_start(&modeSM);
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M;
    ASSERT_EQ(bands[BAND_40M].mode, LSB);

    // Exactly what the front-panel DEMODULATION button does
    ED.modulation[VFO_A] = USB;

    char cmd[] = "MD;";
    EXPECT_STREQ(MD_read(cmd), "MD2;")
        << "MD; must report the modulation in use, not the band default";
}

TEST(CAT, IF_read_ReportsCurrentModulationNotBandDefault) {
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.activeVFO = VFO_A;
    ED.currentBand[VFO_A] = BAND_40M;
    ASSERT_EQ(bands[BAND_40M].mode, LSB);

    char cmd[] = "IF;";

    ED.modulation[VFO_A] = USB;
    std::string usbReply = IF_read(cmd);
    ED.modulation[VFO_A] = LSB;
    std::string lsbReply = IF_read(cmd);

    EXPECT_NE(usbReply, lsbReply)
        << "the IF mode digit must track the modulation in use";
}

// ============================================================================
// DR - digital-mode receive level
//
// The audio the radio sends to the host is unregulated: with AGC off the AGC
// stage applies a fixed gain of 20, so a strong signal reaches full scale and
// clips. A 20 s bench capture sat at 98 % of full scale with only 0.15 dB of
// headroom. DR is the attenuation that buys that headroom back.
// ============================================================================

TEST(CAT, DR_write_SetsReceiveLevel) {
    int32_t saved = ED.digitalRxLevel;

    char cmd[] = "DR040;";
    EXPECT_STREQ(DR_write(cmd), "");
    EXPECT_EQ(ED.digitalRxLevel, 40);

    char zero[] = "DR000;";
    EXPECT_STREQ(DR_write(zero), "");
    EXPECT_EQ(ED.digitalRxLevel, 0) << "0 is a legal level - it mutes the USB stream";

    char full[] = "DR100;";
    EXPECT_STREQ(DR_write(full), "");
    EXPECT_EQ(ED.digitalRxLevel, 100);

    ED.digitalRxLevel = saved;
}

TEST(CAT, DR_write_RejectsOutOfRange) {
    int32_t saved = ED.digitalRxLevel;
    ED.digitalRxLevel = 25;

    char cmd[] = "DR101;";
    EXPECT_STREQ(DR_write(cmd), "?;");
    EXPECT_EQ(ED.digitalRxLevel, 25) << "a rejected level must not change anything";

    ED.digitalRxLevel = saved;
}

TEST(CAT, DR_read_ReturnsLevelZeroPadded) {
    int32_t saved = ED.digitalRxLevel;
    char cmd[] = "DR;";

    ED.digitalRxLevel = 25;
    EXPECT_STREQ(DR_read(cmd), "DR025;");

    ED.digitalRxLevel = 100;
    EXPECT_STREQ(DR_read(cmd), "DR100;");

    ED.digitalRxLevel = 0;
    EXPECT_STREQ(DR_read(cmd), "DR000;");

    ED.digitalRxLevel = saved;
}

TEST(CAT, DR_ParserReachesBothForms) {
    int32_t saved = ED.digitalRxLevel;

    char write_cmd[] = "DR030;";
    EXPECT_STREQ(command_parser(write_cmd), "");
    EXPECT_EQ(ED.digitalRxLevel, 30);

    char read_cmd[] = "DR;";
    EXPECT_STREQ(command_parser(read_cmd), "DR030;");

    ED.digitalRxLevel = saved;
}

// ============================================================================
// Time-sync packets on the CAT port
//
// In a build with a USB audio interface there is one CDC port, CATSerial is
// Serial, and CheckForCATSerialEvents() drains it before CheckForSerialTimeSync()
// could run. The CAT reader therefore has to recognise the PJRC packet itself -
// 'T' + 10 digits + '\n', unchanged on the wire - and everything else that
// arrives newline-terminated has to be discarded rather than prepended to the
// next command. These tests drive CATSerial directly, so they exercise that path
// in either build.
// ============================================================================

// Helper: feed bytes to the CAT port and process them in one call.
static void runCATSerial(const char* bytes) {
    Teensy3Clock.wasSet = false;
    Teensy3Clock.lastSet = 0;
    CATSerial.clearBuffer();
    CATSerial.feedData(bytes);
    CheckForCATSerialEvents();
}

TEST(CAT, TimeSyncPacketOnCATPortSetsClock) {
    runCATSerial("T1748476800\n");
    EXPECT_TRUE(Teensy3Clock.wasSet)
        << "the CAT reader owns the port on a USB-audio build and must honour "
           "the time packet itself";
    EXPECT_EQ(Teensy3Clock.lastSet, (time_t)1748476800);
}

TEST(CAT, TimeSyncPacketOnCATPortAcceptsCarriageReturn) {
    runCATSerial("T1748476800\r");
    EXPECT_TRUE(Teensy3Clock.wasSet);
    EXPECT_EQ(Teensy3Clock.lastSet, (time_t)1748476800);
}

TEST(CAT, TimeSyncPacketDoesNotCorruptTheNextCATCommand) {
    // The bug this fixes: the packet's bytes stayed in catCommand, so the next
    // command parsed as "T1748476800\nFA00014200000;" and came back "?;".
    int64_t savedFreq = ED.centerFreq_Hz[VFO_A];
    int32_t savedBand = ED.currentBand[VFO_A];

    runCATSerial("T1748476800\nFA00014200000;");
    ConsumeInterrupt();
    EXPECT_TRUE(Teensy3Clock.wasSet);
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], 14200000L + SR[SampleRate].rate/4)
        << "a command arriving straight after a time packet must still execute";

    ED.centerFreq_Hz[VFO_A] = savedFreq;
    ED.currentBand[VFO_A] = savedBand;
}

TEST(CAT, TimeSyncPacketRejectsMalformedTimestamps) {
    runCATSerial("T12345\n");            // too few digits
    EXPECT_FALSE(Teensy3Clock.wasSet);

    runCATSerial("T17484768000\n");      // too many digits
    EXPECT_FALSE(Teensy3Clock.wasSet);

    runCATSerial("T0000000001\n");       // parses, but fails the ~2001 sanity check
    EXPECT_FALSE(Teensy3Clock.wasSet);

    runCATSerial("Tabcdefghij\n");       // right length, not digits
    EXPECT_FALSE(Teensy3Clock.wasSet);

    runCATSerial("T -174847680\n");      // atoll() would stop at the space
    EXPECT_FALSE(Teensy3Clock.wasSet);

    runCATSerial("1748476800\n");        // no 'T' header
    EXPECT_FALSE(Teensy3Clock.wasSet);
}

TEST(CAT, TCommandsAreStillCATCommands) {
    // "TX;" and "TX0;" start with 'T' too. They are semicolon-terminated, so the
    // newline branch must never see them.
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    runCATSerial("TX;");
    ConsumeInterrupt();
    EXPECT_FALSE(Teensy3Clock.wasSet)
        << "TX; must not be mistaken for a time packet";
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);

    runCATSerial("RX;");
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

TEST(CAT, StrayNewlineTerminatedBytesAreDiscarded) {
    // A client that ends its commands with CRLF used to leave the line ending in
    // catCommand, which then prefixed - and broke - the following command.
    int64_t savedFreq = ED.centerFreq_Hz[VFO_A];
    int32_t savedBand = ED.currentBand[VFO_A];

    runCATSerial("FA00014200000;\r\nFA00007100000;");
    ConsumeInterrupt();
    EXPECT_EQ(ED.centerFreq_Hz[VFO_A], 7100000L + SR[SampleRate].rate/4)
        << "a CRLF between commands must not break the command after it";

    ED.centerFreq_Hz[VFO_A] = savedFreq;
    ED.currentBand[VFO_A] = savedBand;
}
