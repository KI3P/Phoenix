#include "gtest/gtest.h"
#include "../src/PhoenixSketch/SDT.h"

void IterateOverAllEventsExceptOne(ModeSm *sm, ModeSm_EventId ignoredEvent, ModeSm_StateId expectedState);

// Helper functions

void IterateOverAllEventsExceptOne(ModeSm *sm, ModeSm_EventId ignoredEvent, ModeSm_StateId expectedState){
    FILE *file = fopen("tmp.txt", "w");
    for (int event = ModeSm_EventId_DO; event <= ModeSm_EventIdCount; event++) {
        ModeSm_EventId evt = (ModeSm_EventId)event;
        if (evt != ignoredEvent){
            ModeSm_dispatch_event(sm, evt);
            EXPECT_EQ(sm->state_id, expectedState);
            fprintf(file, "%d\n", (int)evt);
        } else {
            fprintf(file, "Ignored %d\n", (int)evt);             
        }
    }
}

void CheckDitTiming(ModeSm *sm){
    EXPECT_EQ(sm->state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
    for (size_t i = 0; i < DIT_DURATION_MS; i++) {
        ModeSm_dispatch_event(sm, ModeSm_EventId_DO);
    }
    EXPECT_EQ(sm->state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
    for (size_t i = 0; i < DIT_DURATION_MS; i++) {
        ModeSm_dispatch_event(sm, ModeSm_EventId_DO);
    }
    EXPECT_EQ(sm->state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
}

void CheckDahTiming(ModeSm *sm){
    EXPECT_EQ(sm->state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
    for (size_t i = 0; i < 3*DIT_DURATION_MS; i++) {
        ModeSm_dispatch_event(sm, ModeSm_EventId_DO);
    }
    EXPECT_EQ(sm->state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
    for (size_t i = 0; i < DIT_DURATION_MS; i++) {
        ModeSm_dispatch_event(sm, ModeSm_EventId_DO);
    }
    EXPECT_EQ(sm->state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
}

void FakeHamBandActive() {
    bands[ED.currentBand[ED.activeVFO]].band_type = HAM_BAND;
}

void FakeNonHamBandActive() {
    bands[ED.currentBand[ED.activeVFO]].band_type = MISC_BAND;
}

// Enter the SSB_Receive state upon initialization
TEST(ModeSm, EnterSSBReceiveUponInitialization){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
} 

// Enter SSB_Transmit state from SSB_Receive when a PTT_Pressed 
// event is recorded while a ham band is active
TEST(ModeSm, EnterSSBTransmitFromReceiveUponPTTPressedWhileHamBandIsActive){
    //ModeSM sm;
    FakeHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);
} 

// Ignore PTT_PRESSED while a non ham band is active
TEST(ModeSm, IgnorePTTPressedWhileNonHamBandIsActive) {
    //ModeSM sm;
    FakeNonHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

// Move between SSB_Receive and SSB_Transmit states
TEST(ModeSm, NavigateBetweeenSSBReceiveAndSSBTransmitStates){
    //ModeSM sm;
    FakeHamBandActive();
    ModeSm_start(&modeSM);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);    
}

// Move between SSB_Receive and CW_Receive states
TEST(ModeSm, NavigateBetweeenSSBReceiveAndCWReceiveStates){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);    
} 

// Move to CW_TRANSMIT_MARK from SSB_RECEIVE
TEST(ModeSm, StraightKeyNavigateToCWTransmitMark){
    //ModeSM sm;
    FakeHamBandActive();
    ModeSm_start(&modeSM);
    modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_MARK);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_SPACE);
    // Send CW_TRANSMIT_SPACE_TIMEOUT_MS DO events to trigger the timer exit 
    // back to CW_RECEIVE
    for (size_t i = 0; i< CW_TRANSMIT_SPACE_TIMEOUT_MS - 1; i++){
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    // We should still be in the CW_TRANSMIT_SPACE state
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_SPACE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    // Now we should have moved to CW_RECEIVE
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
        
} 

// Ignore KEY_PRESSED while a non ham band is active
TEST(ModeSm, IgnoreKeyPressedWhileNonHamBandIsActive) {
    FakeNonHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

// Get to the transmit dit state while a ham band is active
TEST(ModeSm, NavigateToCWTransmitDitWhileHamBandIsActive){
    FakeHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
}

// Ignore DIT_PRESSED while a non ham band is active
TEST(ModeSm, IgnoreDitPressedWhileNonHamBandIsActive) {
    FakeNonHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

// Get to the transmit dah state through the two viable logic flows
TEST(ModeSm, NavigateToCWTransmitDahWhileHamBandIsActive){
    //ModeSM sm;
    FakeHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
}

// Ignore DAH_PRESSED while a non ham band is active
TEST(ModeSm, IgnoreDahPressedWhileNonHamBandIsActive) {
    //ModeSM sm;
    FakeNonHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

// When in dit mark, move from mark to space in the correct amount of time
TEST(ModeSm, DitMarkToSpaceTransitionTiming){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DIT_MARK;
    modeSM.vars.markCount_ms = 0;
    // We are now in DIT_MARK. We expect to stay there for ditDuration_ms.
    for (size_t i = 0; i < DIT_DURATION_MS-1; i++){
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    // We should still be in the DIT_MARK state
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    // Now we should have moved to DIT_SPACE
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
}

// When in dah mark, move from mark to space in the correct amount of time
TEST(ModeSm, DahMarkToSpaceTransitionTiming){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DAH_MARK;
    modeSM.vars.markCount_ms = 0;
    // We are now in DAH_MARK. We expect to stay there for 3*ditDuration_ms.
    for (size_t i = 0; i < 3*DIT_DURATION_MS-1; i++){
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    }
    // We should still be in the DAH_MARK state
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    // Now we should have moved to DAH_SPACE
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
}

// When in keyer space, move to keyer wait in the correct amount of time
TEST(ModeSm, KeyerSpaceToWaitTransitionTiming){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE;
    modeSM.vars.spaceCount_ms = 0;
    // We are in KEYER_SPACE. We expect to stay here for ditDuration_ms
    for (size_t i = 0; i < DIT_DURATION_MS-1; i++){
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE);
    }
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
}

// When in keyer wait, move to dit mark
TEST(ModeSm, KeyerWaitToDitMarkFlow){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT;
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
}

// When in keyer wait, move to dah mark
TEST(ModeSm, KeyerWaitToDahMarkFlow){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT;
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
}

// When in keyer wait, exit to CW receive in the correct amount of time
TEST(ModeSm, DahWaitToCwReceiveTransitionTiming){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT;
    modeSM.vars.spaceCount_ms = 0;
    modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
    // We are in KEYER_WAIT. We expect to stay here no longer than 
    // CW_TRANSMIT_SPACE_TIMEOUT_MS
    for (size_t i = 0; i < CW_TRANSMIT_SPACE_TIMEOUT_MS-1; i++){
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
    }
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

// Traverse the full flow. Do two dits then a dah, then back to CW Receive
TEST(ModeSm, NavigateDitDitDah){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
    modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;

    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
    CheckDitTiming(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
    CheckDitTiming(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
    CheckDahTiming(&modeSM);
    for (size_t i = 0; i < CW_TRANSMIT_SPACE_TIMEOUT_MS-1; i++){
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT);
    }
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);

}

// CALIBRATION

// Enter frequency calibration mode
TEST(ModeSm, EnterFrequencyCalibrationFromReceiveStates){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_FREQUENCY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_FREQUENCY);
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_FREQUENCY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_FREQUENCY);
} 

// Exit frequency calibration mode
TEST(ModeSm, ExitFrequencyCalibrationToSSBReceive){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_FREQUENCY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_FREQUENCY);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_EXIT);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

// Exit RX IQ calibration mode
TEST(ModeSm, ExitRXIQyCalibrationToSSBReceive){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_RX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_RX_IQ);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_EXIT);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

TEST(ModeSm, EnterCalibrateReceiveIQFromReceiveStates){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_RX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_RX_IQ);
}

// Navigate from power calibration to offset mode
TEST(ModeSm, NavigateFromPowerCalToOffsetMode){
    ModeSm_start(&modeSM);
    // First enter power calibration mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
    // Dispatch OFFSET_START event to enter offset mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_OFFSET_START);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);
}

// Navigate from offset mode back to power calibration
TEST(ModeSm, NavigateFromOffsetModeToPowerCal){
    ModeSm_start(&modeSM);
    // First enter power calibration mode then offset mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_POWER);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_OFFSET_START);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);
    // Dispatch OFFSET_END event to return to power calibration
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_OFFSET_END);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
}

// Navigate between offset space and offset mark states
TEST(ModeSm, NavigateBetweenOffsetSpaceAndOffsetMark){
    ModeSm_start(&modeSM);
    // Enter offset mode via power calibration
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_POWER);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_OFFSET_START);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);
    // Press PTT to enter offset mark state
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_MARK);
    // Release PTT to return to offset space state
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);
}

// Exit from offset mode directly to SSB receive
TEST(ModeSm, ExitOffsetModeToSSBReceive){
    ModeSm_start(&modeSM);
    // Enter offset mode via power calibration
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_POWER);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_OFFSET_START);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);
    // Dispatch CALIBRATE_EXIT event to exit to SSB receive
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_EXIT);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}


// ============================================================================
// Digital (USB audio) mode
//
// DIGITAL_RECEIVE and DIGITAL_TRANSMIT live inside a DIGITAL_STATES composite
// whose enter/exit actions force the radio to 176.4 ksps and put the previous
// rate back. The composite matters: every way out of digital mode has to restore
// the sample rate, including the calibration events dispatched at the
// NORMAL_STATES level.
// ============================================================================

// Reach digital mode from SSB and get back again
TEST(ModeSm, NavigateBetweenSSBReceiveAndDigitalReceive){
    ModeSm_start(&modeSM);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

// Reach digital mode from CW and go back to CW, which is the front panel's
// SSB -> CW -> DIGITAL -> SSB cycle in both directions
TEST(ModeSm, NavigateBetweenCWReceiveAndDigitalReceive){
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_RECEIVE);
}

// PTT keys digital transmit, which is how WSJT-X keys the rig over CAT
TEST(ModeSm, NavigateBetweenDigitalReceiveAndDigitalTransmitStates){
    FakeHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_TRANSMIT);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
}

// The out-of-band interlock guards digital transmit exactly as it guards SSB
TEST(ModeSm, IgnorePTTPressedInDigitalModeWhileNonHamBandIsActive){
    FakeNonHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    FakeHamBandActive();
}

// Digital receive must not respond to the CW keying events
TEST(ModeSm, DigitalReceiveIgnoresKeyEvents){
    FakeHamBandActive();
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_KEY_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
}

// A calibration event dispatched at the NORMAL_STATES level exits the composite,
// so ExitDigitalMode() runs and the sample rate is restored
TEST(ModeSm, EnterCalibrationFromDigitalReceive){
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_FREQUENCY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_FREQUENCY);
    // Calibration always returns to SSB receive, never back into digital mode
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_EXIT);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

// Entering digital mode forces 176.4 ksps; leaving puts the old rate back. This
// is the whole reason the mode exists at that rate: Fs/4 is then exactly the
// 44.1 kHz the USB audio endpoint runs at, so nothing has to be resampled.
TEST(ModeSm, DigitalModeForcesAndRestoresSampleRate){
    uint8_t saved = SampleRate;

    SampleRate = SAMPLE_RATE_192K;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_RECEIVE);
    EXPECT_EQ(SampleRate, SAMPLE_RATE_176K)
        << "digital mode only works where Fs/4 == 44100 Hz";

    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
    EXPECT_EQ(SampleRate, SAMPLE_RATE_192K) << "the previous rate must come back";

    SampleRate = saved;
}

// Transmitting and returning must not disturb the saved rate: the composite is
// not exited on a PTT press, so EnterDigitalMode() must not run again and
// overwrite the saved rate with 176.4 ksps.
TEST(ModeSm, DigitalTransmitDoesNotClobberSavedSampleRate){
    uint8_t saved = SampleRate;
    FakeHamBandActive();

    SampleRate = SAMPLE_RATE_192K;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_DIGITAL_TRANSMIT);
    EXPECT_EQ(SampleRate, SAMPLE_RATE_176K);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);

    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
    EXPECT_EQ(SampleRate, SAMPLE_RATE_192K)
        << "a transmit round trip must not lose the pre-digital sample rate";

    SampleRate = saved;
}

// Entering digital mode when already at 176.4 ksps must still restore correctly
TEST(ModeSm, DigitalModeFromNativeRateRestoresSameRate){
    uint8_t saved = SampleRate;

    SampleRate = SAMPLE_RATE_176K;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    EXPECT_EQ(SampleRate, SAMPLE_RATE_176K);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);
    EXPECT_EQ(SampleRate, SAMPLE_RATE_176K);

    SampleRate = saved;
}

// Regression: EnterDigitalMode() and ExitDigitalMode() are the DIGITAL_STATES
// composite's entry and exit actions, so they run while state_id is still the
// composite rather than a leaf. Both call ChangeSampleRate(), which ends in
// UpdateTuneState(). If the composite is not handled there, UpdateTuneState()
// falls through to its default branch and calls HandleTuneState() with whatever
// tune state was left over from before - reprogramming the VFO from stale state
// on every entry to and exit from digital mode.
TEST(ModeSm, DigitalModeTransitionsHandleTheCompositeState){
    uint8_t saved = SampleRate;
    SampleRate = SAMPLE_RATE_192K;
    ModeSm_start(&modeSM);

    Serial.lines.clear();
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_DIGITAL_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_SSB_MODE);

    for (const std::string &line : Serial.lines) {
        EXPECT_EQ(line.find("Unhandled modeSM.state_id"), std::string::npos)
            << "unhandled state during a digital mode transition: " << line;
    }

    SampleRate = saved;
}
