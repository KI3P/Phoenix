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

// Enter the SSB_Receive state upon initialization
TEST(ModeSm, EnterSSBReceiveUponInitialization){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
} 

// Enter SSB_Transmit state from SSB_Receive when a PTT_Pressed event is recorded
TEST(ModeSm, EnterSSBTransmitFromReceiveUponPTTPressed){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_TRANSMIT);
} 

// Move between SSB_Receive and SSB_Transmit states
TEST(ModeSm, NavigateBetweeenSSBReceiveAndSSBTransmitStates){
    //ModeSM sm;
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

// Get to the transmit dit state
TEST(ModeSm, NavigateToCWTransmitDit){
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DIT_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DIT_MARK);
}

// Get to the transmit dah state through the two viable logic flows
TEST(ModeSm, NavigateToCWTransmitDah){
    //ModeSM sm;
    ModeSm_start(&modeSM);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_TO_CW_MODE);
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DAH_PRESSED);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CW_TRANSMIT_DAH_MARK);
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

