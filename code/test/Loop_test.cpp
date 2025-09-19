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

TEST(Loop, CATMicGainChangeViaRepeatedLoop){
    // Save initial microphone gain state
    int32_t initialMicGain = ED.currentMicGain;
    
    // Clear any existing data in the serial buffer and interrupts
    SerialUSB1.clearBuffer();
    ConsumeInterrupt();
    EXPECT_EQ(GetInterrupt(), iNONE);
    
    // Feed a CAT command to change microphone gain to 75% (which should be +12 dB)
    // MG075; -> 75 * 70 / 100 - 40 = 52.5 - 40 = 12.5 -> 12 dB (integer truncation)
    SerialUSB1.feedData("MG075;");
    
    // Execute loop() to process the CAT serial event
    // The loop() function calls CheckForCATSerialEvents() which processes the CAT command
    // Unlike frequency changes, MG commands don't set interrupts - they directly modify ED.currentMicGain
    loop();
    
    // After one loop() execution, the microphone gain change should be complete
    // Verify that no interrupt was set (MG commands don't generate interrupts)
    EXPECT_EQ(GetInterrupt(), iNONE);
    
    // Verify that the microphone gain was changed from the initial value
    EXPECT_NE(ED.currentMicGain, initialMicGain);
    
    // Verify the microphone gain was set correctly
    // 75 * 70 / 100 - 40 = 52.5 - 40 = 12.5 -> 12 dB (integer conversion)
    EXPECT_EQ(ED.currentMicGain, 12);
    
    // Test a second command to verify loop continues to process CAT commands correctly
    SerialUSB1.feedData("MG025;");
    loop();
    
    // Verify the second command was processed
    // 25 * 70 / 100 - 40 = 17.5 - 40 = -22.5 -> -22 dB (integer conversion)
    EXPECT_EQ(ED.currentMicGain, -22);
    
    // Execute loop() one more time to ensure system stability
    loop();
    EXPECT_EQ(GetInterrupt(), iNONE);
    
    // Clean up - clear buffer for next test
    SerialUSB1.clearBuffer();
}

TEST(Loop, CATTransmitCommandViaRepeatedLoop){
    // Test that TX commands work correctly via loop() execution
    // This verifies the complete chain: CAT serial -> command parser -> state machine

    // Clear any existing data in the serial buffer and interrupts
    SerialUSB1.clearBuffer();
    ConsumeInterrupt();
    EXPECT_EQ(GetInterrupt(), iNONE);

    // Test SSB mode transition
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Feed a CAT command to trigger transmit
    SerialUSB1.feedData("TX0;");

    // Execute loop() to process the CAT serial event
    // The loop() function calls CheckForCATSerialEvents() which processes the TX command
    // This should trigger the PTT_PRESSED event and change state to SSB_TRANSMIT
    loop();

    // After one loop() execution, the transmit state should be set
    // Verify that no interrupt was set (TX commands don't generate interrupts)
    EXPECT_EQ(GetInterrupt(), iNONE);

    // Verify that the state changed from SSB_RECEIVE to SSB_TRANSMIT
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);

    // Test CW mode transition
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);

    // Feed another TX command for CW mode
    SerialUSB1.feedData("TX1;");

    // Execute loop() to process the second CAT command
    loop();

    // Verify that the state changed from CW_RECEIVE to CW_TRANSMIT_MARK
    EXPECT_EQ(GetInterrupt(), iNONE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_MARK);

    // Test that TX command has no effect when already transmitting
    // Set to SSB transmit state first
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    ModeSm_StateId initial_transmit_state = modeSM.state_id;

    // Send TX command - should have no effect since already transmitting
    SerialUSB1.feedData("TX0;");
    loop();

    EXPECT_EQ(GetInterrupt(), iNONE);
    EXPECT_EQ(modeSM.state_id, initial_transmit_state); // State should remain unchanged

    // Execute loop() one more time to ensure system stability
    loop();
    EXPECT_EQ(GetInterrupt(), iNONE);

    // Clean up - clear buffer for next test
    SerialUSB1.clearBuffer();
}

// ================== MODE CHANGE TRANSITION TESTS ==================

TEST(Loop, ModeChangeSSBToCW){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Trigger mode change from SSB to CW
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

TEST(Loop, ModeChangeCWToSSB){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;

    // Trigger mode change from CW to SSB
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

// ================== KEY RELEASE TRANSITION TESTS ==================

TEST(Loop, StraightKeyReleasedFromTransmitMark){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetKeyType(KeyTypeId_Straight);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;

    // Release straight key
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_SPACE);
}

TEST(Loop, KeyerDitMarkIgnoresKeyReleased){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetKeyType(KeyTypeId_Keyer);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DIT_MARK;

    // Keyer states ignore KEY_RELEASED - they use timers instead
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
}

TEST(Loop, KeyerDahMarkIgnoresKeyReleased){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetKeyType(KeyTypeId_Keyer);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DAH_MARK;

    // Keyer states ignore KEY_RELEASED - they use timers instead
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
}

// ================== TIMER-BASED CW KEYER TRANSITION TESTS ==================

TEST(Loop, DitMarkToKeyerSpaceOnTimer){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DIT_MARK;

    // Set timer variables
    modeSM.vars.ditDuration_ms = 100;
    modeSM.vars.markCount_ms = 0;

    // Simulate time passing by calling DO event repeatedly
    for(int i = 0; i < 100; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }

    // Should transition to keyer space when markCount_ms >= ditDuration_ms
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
}

TEST(Loop, DahMarkToKeyerSpaceOnTimer){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DAH_MARK;

    // Set timer variables - dah is 3x dit duration
    modeSM.vars.ditDuration_ms = 100;
    modeSM.vars.markCount_ms = 0;

    // Simulate time passing by calling DO event repeatedly (3x dit duration)
    for(int i = 0; i < 300; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }

    // Should transition to keyer space when markCount_ms >= 3*ditDuration_ms
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
}

TEST(Loop, KeyerSpaceToKeyerWaitOnTimer){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE;

    // Set timer variables
    modeSM.vars.ditDuration_ms = 100;
    modeSM.vars.spaceCount_ms = 0;

    // Simulate time passing by calling DO event repeatedly
    for(int i = 0; i < 100; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }

    // Should transition to keyer wait when spaceCount_ms >= ditDuration_ms
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
}

TEST(Loop, KeyerWaitToCWReceiveOnTimer){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT;

    // Set timer variables
    modeSM.vars.waitDuration_ms = 200;
    modeSM.vars.spaceCount_ms = 0;

    // Simulate time passing by calling DO event repeatedly
    for(int i = 0; i < 200; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }

    // Should transition to CW receive when spaceCount_ms >= waitDuration_ms
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

// ================== STRAIGHT KEY TIMER TRANSITION TESTS ==================

TEST(Loop, StraightKeySpaceToCWReceiveOnTimer){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;

    // Set timer variables
    modeSM.vars.waitDuration_ms = 300;
    modeSM.vars.spaceCount_ms = 0;

    // Simulate time passing by calling DO event repeatedly
    for(int i = 0; i < 300; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }

    // Should transition to CW receive when spaceCount_ms >= waitDuration_ms
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

// ================== CALIBRATION STATE TRANSITION TESTS ==================

TEST(Loop, CalibrationFrequencyTransition){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Trigger calibration frequency mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_FREQUENCY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_FREQUENCY);
}

TEST(Loop, CalibrationRXIQTransition){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Trigger calibration RX IQ mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_RX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_RX_IQ);
}

TEST(Loop, CalibrationTXIQTransition){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Trigger calibration TX IQ mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_TX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ);
}

TEST(Loop, CalibrationSSBPATransition){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Trigger calibration SSB PA mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_SSB_PA);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_SSB_PA);
}

TEST(Loop, CalibrationCWPATransition){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Trigger calibration CW PA mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_CW_PA);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_CW_PA);
}

TEST(Loop, CalibrationExitTransition){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CALIBRATE_FREQUENCY;

    // Exit calibration mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_EXIT);
    // Should return to normal operation (SSB_RECEIVE by default)
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

// ================== COMPLEX MULTI-STEP CW SEQUENCE TESTS ==================

TEST(Loop, CompleteCWDitSequence){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetKeyType(KeyTypeId_Keyer);
    SetKey1Dit();
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;

    // Set timing parameters
    modeSM.vars.ditDuration_ms = 50;
    modeSM.vars.waitDuration_ms = 100;

    // Start dit transmission
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);

    // Simulate dit mark duration (50ms)
    for(int i = 0; i < 50; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);

    // Simulate space duration (50ms)
    for(int i = 0; i < 50; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);

    // Simulate wait duration (100ms)
    for(int i = 0; i < 100; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

TEST(Loop, CompleteCWDahSequence){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetKeyType(KeyTypeId_Keyer);
    SetKey1Dah();
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;

    // Set timing parameters
    modeSM.vars.ditDuration_ms = 50;
    modeSM.vars.waitDuration_ms = 100;

    // Start dah transmission
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);

    // Simulate dah mark duration (3 * 50ms = 150ms)
    for(int i = 0; i < 150; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);

    // Simulate space duration (50ms)
    for(int i = 0; i < 50; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);

    // Simulate wait duration (100ms)
    for(int i = 0; i < 100; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

TEST(Loop, CompleteStraightKeySequence){
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);
    SetKeyType(KeyTypeId_Straight);
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;

    // Set timing parameters
    modeSM.vars.waitDuration_ms = 200;

    // Start straight key transmission
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_MARK);

    // Release straight key
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_SPACE);

    // Simulate wait duration (200ms)
    for(int i = 0; i < 200; i++) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

// ================== HARDWARE STATE MACHINE TIMING DELAY TESTS ==================

TEST(Loop, HardwareStateMachineRFReceiveTimingDelays) {
    // Test that the new timing delays in RFReceive state occur as expected
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Initialize RF hardware and set up for state transition
    InitializeRFHardware();
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Set up initial conditions - start from transmit state to trigger full receive sequence
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateRFHardwareState(); // This sets the previous state properly

    // Clear buffer to track the receive state sequence
    buffer.head = 0;
    buffer.count = 0;

    // Transition to receive state to trigger the timing sequence
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateRFHardwareState(); // This should trigger the receive sequence with delays

    // The sequence should have multiple buffer entries with time gaps
    // RFReceive sequence: CWoff, DisableCWVFOOutput, SetTXAttenuation(31.5), TXBypassBPF,
    // SelectXVTR, Bypass100WPA, **50ms delay**, RXSelectBPF, UpdateTuneState, SetRXAttenuation,
    // EnableSSBVFOOutput, DisableCalFeedback, **50ms delay**, SelectRXMode, **50ms delay**, SetTXAttenuation

    // Verify we have multiple buffer entries (should be 10+ hardware operations)
    EXPECT_GE(buffer.count, 10);

    // Find the time gaps between groups of operations (where delays occur)
    std::vector<size_t> delay_indices;
    for (size_t i = 1; i < buffer.count; i++) {
        uint32_t time_gap = buffer.entries[i].timestamp - buffer.entries[i-1].timestamp;
        // Look for gaps > 45ms (allowing some tolerance for the 50ms delays)
        if (time_gap > 45000) { // 45ms in microseconds
            delay_indices.push_back(i);
        }
    }

    // Should have 3 delay points in the RFReceive sequence
    EXPECT_EQ(delay_indices.size(), 3);

    // Verify the delays are approximately 50ms each
    for (size_t idx : delay_indices) {
        uint32_t time_gap = buffer.entries[idx].timestamp - buffer.entries[idx-1].timestamp;
        EXPECT_GE(time_gap, 45000); // At least 45ms
        EXPECT_LE(time_gap, 55000); // At most 55ms (allowing some tolerance)
    }
}

TEST(Loop, HardwareStateMachineRFTransmitTimingDelays) {
    // Test that the timing delays in RFTransmit state occur as expected
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Initialize RF hardware and set up for state transition
    InitializeRFHardware();
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Set up initial conditions - start from receive state
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateRFHardwareState();

    // Clear buffer to track the transmit state sequence
    buffer.head = 0;
    buffer.count = 0;

    // Transition to transmit state to trigger the timing sequence
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateRFHardwareState(); // This should trigger the transmit sequence with delays

    // The sequence should have multiple buffer entries
    // RFTransmit sequence: RXBypassBPF, DisableCalFeedback, **50ms delay**, SetTXAttenuation,
    // DisableCWVFOOutput, CWoff, UpdateTuneState, EnableSSBVFOOutput, SelectTXSSBModulation,
    // TXSelectBPF, BypassXVTR, Bypass100WPA, **50ms delay**, SelectTXMode

    // Verify we have multiple buffer entries (should be 10+ hardware operations)
    EXPECT_GE(buffer.count, 10);

    // Find the time gaps between groups of operations (where delays occur)
    std::vector<size_t> delay_indices;
    for (size_t i = 1; i < buffer.count; i++) {
        uint32_t time_gap = buffer.entries[i].timestamp - buffer.entries[i-1].timestamp;
        // Look for gaps > 45ms (allowing some tolerance for the 50ms delays)
        if (time_gap > 45000) { // 45ms in microseconds
            delay_indices.push_back(i);
        }
    }

    // Should have 2 delay points in the RFTransmit sequence
    EXPECT_EQ(delay_indices.size(), 2);

    // Verify the delays are approximately 50ms each
    for (size_t idx : delay_indices) {
        uint32_t time_gap = buffer.entries[idx].timestamp - buffer.entries[idx-1].timestamp;
        EXPECT_GE(time_gap, 45000); // At least 45ms
        EXPECT_LE(time_gap, 55000); // At most 55ms (allowing some tolerance)
    }
}

TEST(Loop, HardwareStateMachineRFCWMarkTimingDelays) {
    // Test that the timing delays in RFCWMark state occur as expected (from non-CWSpace state)
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Initialize RF hardware and set up for state transition
    InitializeRFHardware();
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Set up initial conditions - start from receive state to trigger full CW mark sequence
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateRFHardwareState();

    // Clear buffer to track the CW mark state sequence
    buffer.head = 0;
    buffer.count = 0;

    // Transition to CW mark state to trigger the timing sequence
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateRFHardwareState(); // This should trigger the CW mark sequence with delays

    // The sequence should have multiple buffer entries
    // RFCWMark sequence (from non-CWSpace): RXBypassBPF, DisableCalFeedback, SetTXAttenuation,
    // DisableSSBVFOOutput, UpdateTuneState, EnableCWVFOOutput, SelectTXCWModulation,
    // TXSelectBPF, BypassXVTR, Bypass100WPA, SelectTXMode, **50ms delay**, CWon

    // Verify we have multiple buffer entries (should be 10+ hardware operations)
    EXPECT_GE(buffer.count, 10);

    // Find the time gaps between groups of operations (where delays occur)
    std::vector<size_t> delay_indices;
    for (size_t i = 1; i < buffer.count; i++) {
        uint32_t time_gap = buffer.entries[i].timestamp - buffer.entries[i-1].timestamp;
        // Look for gaps > 45ms (allowing some tolerance for the 50ms delays)
        if (time_gap > 45000) { // 45ms in microseconds
            delay_indices.push_back(i);
        }
    }

    // Should have 1 delay point in the RFCWMark sequence (before CWon)
    EXPECT_EQ(delay_indices.size(), 1);

    // Verify the delay is approximately 50ms
    if (delay_indices.size() >= 1) {
        size_t idx = delay_indices[0];
        uint32_t time_gap = buffer.entries[idx].timestamp - buffer.entries[idx-1].timestamp;
        EXPECT_GE(time_gap, 45000); // At least 45ms
        EXPECT_LE(time_gap, 55000); // At most 55ms (allowing some tolerance)
    }
}

TEST(Loop, HardwareStateMachineRFCWMarkFromCWSpaceNoDelay) {
    // Test that RFCWMark state from RFCWSpace state has no delays (optimization)
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Initialize RF hardware and set up for state transition
    InitializeRFHardware();
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Set up initial conditions - start from CW space state
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    UpdateRFHardwareState();

    // Clear buffer to track the CW mark state sequence
    buffer.head = 0;
    buffer.count = 0;

    // Transition to CW mark state (should only call CWon, no other setup)
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateRFHardwareState();

    // Should have minimal buffer entries (just CWon operation)
    EXPECT_LE(buffer.count, 2); // Should be 1-2 entries max

    // Check that there are no significant time gaps (no delays)
    for (size_t i = 1; i < buffer.count; i++) {
        uint32_t time_gap = buffer.entries[i].timestamp - buffer.entries[i-1].timestamp;
        // Should be no delays > 10ms
        EXPECT_LT(time_gap, 10000); // Less than 10ms
    }
}

TEST(Loop, HardwareStateMachineRFCWSpaceFromCWMarkNoDelay) {
    // Test that RFCWSpace state from RFCWMark state has no delays (optimization)
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Initialize RF hardware and set up for state transition
    InitializeRFHardware();
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Set up initial conditions - start from CW mark state
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateRFHardwareState();

    // Clear buffer to track the CW space state sequence
    buffer.head = 0;
    buffer.count = 0;

    // Transition to CW space state (should only call CWoff, no other setup)
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    UpdateRFHardwareState();

    // Should have minimal buffer entries (just CWoff operation)
    EXPECT_LE(buffer.count, 2); // Should be 1-2 entries max

    // Check that there are no significant time gaps (no delays)
    for (size_t i = 1; i < buffer.count; i++) {
        uint32_t time_gap = buffer.entries[i].timestamp - buffer.entries[i-1].timestamp;
        // Should be no delays > 10ms
        EXPECT_LT(time_gap, 10000); // Less than 10ms
    }
}

TEST(Loop, HardwareStateMachineTimingSequenceVerification) {
    // Comprehensive test to verify the complete timing sequence during state transitions
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Initialize RF hardware
    InitializeRFHardware();
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Test a complete cycle: Receive -> Transmit -> Receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateRFHardwareState();

    // Clear buffer and start tracking transitions
    buffer.head = 0;
    buffer.count = 0;

    // Transition 1: Receive to Transmit
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    uint32_t start_time = micros();
    UpdateRFHardwareState();
    uint32_t end_time = micros();

    // The total time should include the delays (should be ~100ms)
    uint32_t total_time = end_time - start_time;
    EXPECT_GE(total_time, 90000);  // At least 90ms (2 x 50ms delays - some tolerance)
    EXPECT_LE(total_time, 150000); // At most 150ms (allowing for processing overhead)

    size_t transmit_entries = buffer.count;
    EXPECT_GE(transmit_entries, 10); // Should have multiple hardware operations

    // Transition 2: Transmit back to Receive
    buffer.head = 0;
    buffer.count = 0;

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    start_time = micros();
    UpdateRFHardwareState();
    end_time = micros();

    // The total time should include the delays (should be ~150ms)
    total_time = end_time - start_time;
    EXPECT_GE(total_time, 135000); // At least 135ms (3 x 50ms delays - some tolerance)
    EXPECT_LE(total_time, 200000); // At most 200ms (allowing for processing overhead)

    size_t receive_entries = buffer.count;
    EXPECT_GE(receive_entries, 12); // Should have more hardware operations than transmit
}

TEST(Loop, HardwareStateMachineUpdateTuneStateAlwaysCalled) {
    // Test that UpdateTuneState is called even when there's no hardware state change
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Initialize RF hardware
    InitializeRFHardware();
    UISm_start(&uiSM);
    ModeSm_start(&modeSM);

    // Set to a known state
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateRFHardwareState();

    // Clear buffer
    buffer.head = 0;
    buffer.count = 0;

    // Call UpdateRFHardwareState again with same state (should only call UpdateTuneState)
    UpdateRFHardwareState();

    // Should have at least one buffer entry from UpdateTuneState -> HandleTuneState -> SelectLPFBand
    EXPECT_GE(buffer.count, 1);

    // Verify there are no significant delays (no MyDelay calls)
    for (size_t i = 1; i < buffer.count; i++) {
        uint32_t time_gap = buffer.entries[i].timestamp - buffer.entries[i-1].timestamp;
        EXPECT_LT(time_gap, 10000); // Less than 10ms (no delays expected)
    }
}

TEST(Loop, HardwareStateMachineDelayOrderingVerification) {
    // Test that delays occur in the correct order within the state sequences
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Initialize RF hardware
    ModeSm_start(&modeSM);
    UISm_start(&uiSM);
    InitializeRFHardware();

    // Start from transmit to trigger receive sequence with all delays
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateRFHardwareState();

    // Clear buffer for receive sequence
    buffer.head = 0;
    buffer.count = 0;

    // Transition to receive to get the full delay sequence
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateRFHardwareState();

    // Analyze the timing pattern to verify delay ordering
    std::vector<uint32_t> operation_times;
    for (size_t i = 0; i < buffer.count; i++) {
        operation_times.push_back(buffer.entries[i].timestamp);
    }

    // Find delay boundaries (large time gaps)
    std::vector<size_t> delay_boundaries;
    for (size_t i = 1; i < operation_times.size(); i++) {
        if (operation_times[i] - operation_times[i-1] > 45000) { // 45ms threshold
            delay_boundaries.push_back(i);
        }
    }

    // Verify we have the expected 3 delays for receive sequence
    EXPECT_EQ(delay_boundaries.size(), 3);

    if (delay_boundaries.size() >= 3) {
        // First delay should occur after initial power-down operations
        EXPECT_GE(delay_boundaries[0], 5); // At least 5 operations before first delay

        // Second delay should occur after receive path setup
        EXPECT_GT(delay_boundaries[1], delay_boundaries[0] + 3); // At least 3 ops between delays

        // Third delay should occur before final TX attenuation setting
        EXPECT_GE(delay_boundaries[2], delay_boundaries[1] + 1); // At least 1 op between delays
    }
}
