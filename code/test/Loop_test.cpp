#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"

// Forward declare CAT functions for testing
char *BU_write(char* cmd);
char *BD_write(char* cmd);
char *AG_write(char* cmd);
char *FA_write(char* cmd);
char *FB_write(char* cmd);
char *command_parser(char* command);
void CheckForCATSerialEvents(void);

TEST(Loop, InterruptInitializes){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    InterruptType i = GetInterrupt();
    EXPECT_EQ(i, iNONE);
} 

TEST(Loop, InterruptSet){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetInterrupt(iPTT_PRESSED);
    InterruptType i = GetInterrupt();
    EXPECT_EQ(i, iPTT_PRESSED);
} 

TEST(Loop, InterruptCleared){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetInterrupt(iPTT_PRESSED);
    ConsumeInterrupt();
    InterruptType i = GetInterrupt();
    EXPECT_EQ(i, iNONE);
}

TEST(Loop, PTTPressedTriggersModeStateChange){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_SSB_RECEIVE);
    SetInterrupt(iPTT_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_SSB_TRANSMIT);
}

TEST(Loop, PTTReleasedTriggersModeStateChange){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    SetInterrupt(iPTT_RELEASED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_SSB_RECEIVE);
}

// Key1 pressed is interpreted as straight key when keyType is straight
TEST(Loop, KeyPressedInterpretedAsStraight){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    SetKeyType(KeyTypeId_Straight);
    SetInterrupt(iKEY1_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_CW_TRANSMIT_MARK);
}

// Key1 pressed is interpreted as dit key when keyType is keyer and flip = false
// and key2 pressed is interpreted as dah
TEST(Loop, KeyPressesInterpretedWhenKeyerAndFlipFalse){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetKeyType(KeyTypeId_Keyer);
    SetKey1Dit();
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    SetInterrupt(iKEY1_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    SetInterrupt(iKEY2_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
}


// Key1 pressed is interpreted as dah key when keyType is keyer and flip = true
// and key2 pressed is interpreted as dit
TEST(Loop, KeyPressesInterpretedWhenKeyerAndFlipTrue){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetKeyType(KeyTypeId_Keyer);
    SetKey1Dah();
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    SetInterrupt(iKEY1_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    SetInterrupt(iKEY2_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
}

TEST(Loop, UpdateAudioIOState){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateAudioIOState();
    ModeSm_StateId prev = GetAudioPreviousState();
    EXPECT_EQ(prev, ModeSm_StateId_CW_RECEIVE);

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateAudioIOState();
    prev = GetAudioPreviousState();
    EXPECT_EQ(prev, ModeSm_StateId_SSB_RECEIVE);
}

TEST(Loop, ChangeVFO){
    uint8_t vfo = ED.activeVFO;
    SetInterrupt(iVFO_CHANGE);
    ConsumeInterrupt();
    // check ED.activeVFO
    EXPECT_NE(ED.activeVFO,vfo);
    // check frequency
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO],GetSSBVFOFrequency());
}

TEST(Loop, CATFrequencyChangeViaRepeatedLoop){
    // Save initial state
    ED.activeVFO = VFO_A;
    int64_t initialCenterFreq = ED.centerFreq_Hz[ED.activeVFO];
    int64_t initialFineTuneFreq = ED.fineTuneFreq_Hz[ED.activeVFO];
    int32_t initialBand = ED.currentBand[ED.activeVFO];
    
    // Clear any existing data in the serial buffer and interrupts
    SerialUSB1.clearBuffer();
    ConsumeInterrupt();
    EXPECT_EQ(GetInterrupt(), iNONE);
    
    // Feed a CAT command to change VFO A frequency to 20m band (14.200 MHz)
    SerialUSB1.feedData("FA00014200000;");
    
    // Execute loop() to process the CAT serial event and frequency change
    // The loop() function calls CheckForCATSerialEvents() which processes the CAT command,
    // then calls ConsumeInterrupt() which handles the iUPDATE_TUNE interrupt set by the CAT command
    loop();
    
    // After one loop() execution, the frequency change should be complete
    // Verify that the interrupt has been consumed
    EXPECT_EQ(GetInterrupt(), iNONE);
    
    // Verify that the frequency change was applied correctly
    EXPECT_NE(ED.centerFreq_Hz[ED.activeVFO], initialCenterFreq);
    EXPECT_EQ(ED.currentBand[ED.activeVFO], BAND_20M);
    
    // Verify the frequency was set correctly (accounting for SR offset)
    int64_t expectedCenterFreq = 14200000L + SR[SampleRate].rate/4;
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO], expectedCenterFreq);
    
    // Verify fine tune was reset to 0
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO], 0);
    
    // Verify that the tuning system has been updated with the new frequency
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO], GetSSBVFOFrequency());
    
    // Execute loop() one more time to ensure system stability
    loop();
    EXPECT_EQ(GetInterrupt(), iNONE);
    
    // Clean up - clear buffer for next test
    SerialUSB1.clearBuffer();
}
