#include "gtest/gtest.h"
#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/RFBoard_si5351.h"

extern Si5351 si5351;

TEST(RFHardwareState, StateStartInReceive){
    InitializeRFHardware();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateRFHardwareState();

    // RECEIVE STATE
    // We expect CLK0 and CLK1 to be enabled, and CLK2 to be disabled
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK0], 1);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK1], 1);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 0);
    // CW is off
    EXPECT_EQ(getCWState(), 0); // CW off
    // RX mode
    EXPECT_EQ(getRXTXState(), 0); // RX
    // Cal feedback off
    EXPECT_EQ(getCalFeedbackState(), 0); // CAL_OFF
    // SSB mode
    EXPECT_EQ(getModulationState(), 1); // XMIT_SSB

    // BPF/XVTR/PA control states for receive mode
    EXPECT_EQ(GET_BIT(hardwareRegister, TXBPFBIT), 0); // TX BPF bypassed
    EXPECT_EQ(GET_BIT(hardwareRegister, RXBPFBIT), 1); // RX BPF selected
    EXPECT_EQ(GET_BIT(hardwareRegister, XVTRBIT), 0);  // XVTR selected (active low)
    EXPECT_EQ(GET_BIT(hardwareRegister, PA100WBIT), 0); // 100W PA bypassed
}

TEST(RFHardwareState, StateTransitionToSSBTransmit){
    InitializeRFHardware();
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateRFHardwareState();

    // SSB TRANSMIT STATE
    // We expect CLK0 and CLK1 to be enabled, and CLK2 to be disabled
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK0], 1);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK1], 1);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 0);
    // CW is off
    EXPECT_EQ(getCWState(), 0); // CW off
    // TX mode
    EXPECT_EQ(getRXTXState(), 1); // TX
    // Cal feedback off
    EXPECT_EQ(getCalFeedbackState(), 0); // CAL_OFF
    // SSB mode
    EXPECT_EQ(getModulationState(), 1); // XMIT_SSB

    // BPF/XVTR/PA control states for SSB transmit mode
    EXPECT_EQ(GET_BIT(hardwareRegister, TXBPFBIT), 1); // TX BPF selected
    EXPECT_EQ(GET_BIT(hardwareRegister, RXBPFBIT), 0); // RX BPF bypassed
    EXPECT_EQ(GET_BIT(hardwareRegister, XVTRBIT), 1);  // XVTR bypassed
    EXPECT_EQ(GET_BIT(hardwareRegister, PA100WBIT), 0); // 100W PA bypassed
}

TEST(RFHardwareState, StateTransitionToCWSpace){
    InitializeRFHardware();
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    UpdateRFHardwareState();

    // CW TRANSMIT SPACE STATE
    // We expect CLK0 and CLK1 to be disabled, and CLK2 to be enabled
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK0], 0);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK1], 0);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 1);
    // CW is off
    EXPECT_EQ(getCWState(), 0); // CW off
    // TX mode
    EXPECT_EQ(getRXTXState(), 1); // TX
    // Cal feedback off
    EXPECT_EQ(getCalFeedbackState(), 0); // CAL_OFF
    // CW mode
    EXPECT_EQ(getModulationState(), 0); // XMIT_CW

    // BPF/XVTR/PA control states for CW transmit space mode
    EXPECT_EQ(GET_BIT(hardwareRegister, TXBPFBIT), 1); // TX BPF selected
    EXPECT_EQ(GET_BIT(hardwareRegister, RXBPFBIT), 0); // RX BPF bypassed
    EXPECT_EQ(GET_BIT(hardwareRegister, XVTRBIT), 1);  // XVTR bypassed
    EXPECT_EQ(GET_BIT(hardwareRegister, PA100WBIT), 0); // 100W PA bypassed
}

TEST(RFHardwareState, StateTransitionToCWMark){
    InitializeRFHardware();
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateRFHardwareState();

    // CW TRANSMIT MARK STATE
    // We expect CLK0 and CLK1 to be disabled, and CLK2 to be enabled
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK0], 0);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK1], 0);
    EXPECT_EQ(si5351.output_enable_calls[SI5351_CLK2], 1);
    // CW is on
    EXPECT_EQ(getCWState(), 1); // CW on
    // TX mode
    EXPECT_EQ(getRXTXState(), 1); // TX
    // Cal feedback off
    EXPECT_EQ(getCalFeedbackState(), 0); // CAL_OFF
    // CW mode
    EXPECT_EQ(getModulationState(), 0); // XMIT_CW

    // BPF/XVTR/PA control states for CW transmit mark mode
    EXPECT_EQ(GET_BIT(hardwareRegister, TXBPFBIT), 1); // TX BPF selected
    EXPECT_EQ(GET_BIT(hardwareRegister, RXBPFBIT), 0); // RX BPF bypassed
    EXPECT_EQ(GET_BIT(hardwareRegister, XVTRBIT), 1);  // XVTR bypassed
    EXPECT_EQ(GET_BIT(hardwareRegister, PA100WBIT), 0); // 100W PA bypassed
}

TEST(RFHardwareState, FrequenciesSetUponStateChange){
    // Set up the arrays
    ED.centerFreq_Hz[ED.activeVFO] = 7100000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 500L;
    int64_t rxtx = 7100000L - 500L - 192000L/4;

/*think about frequency control. How will this change as I switch between SSB mode and CW mode?
and between SSB receive and SSB transmit mode? it changes from state to state.
* CW/SSB Receive:  RXfreq = centerFreq_Hz - fineTuneFreq_Hz - SampleRate/4 (see Tune.cpp:98)
* SSB Transmit:    TXfreq = centerFreq_Hz - fineTuneFreq_Hz - SampleRate/4
* CW Transmit:     TXfreq = centerFreq_Hz - fineTuneFreq_Hz - SampleRate/4 -/+ CWToneOffset
*/

    InitializeRFHardware();
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateRFHardwareState();    
    EXPECT_EQ(GetSSBVFOFrequency(), 7100000L);
    EXPECT_EQ(ED.fineTuneFreq_Hz[ED.activeVFO], 500L);
    EXPECT_EQ(GetTXRXFreq_dHz(),rxtx*100);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Tune State Machine Tests
///////////////////////////////////////////////////////////////////////////////////////////////////

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromSSBReceive) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateTuneState();
    
    // Should set SSB VFO frequency to centerFreq_Hz * 100
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 707400000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 707400000);
}

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromCWReceive) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateTuneState();
    
    // Should set SSB VFO frequency to centerFreq_Hz * 100 (same as SSB receive)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 707400000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 707400000);
}

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromSSBTransmit) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;

    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateTuneState();

    // Should set SSB VFO frequency to GetTXRXFreq_dHz()
    // Calculation: 7074000 - 100 - 48000/4 = 7061900 Hz * 100 = 706190000 (see Tune.cpp:98)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 706190000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 706190000);
}

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromCWTransmitMark) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_40M; // LSB
    ED.CWToneIndex = 3; // 750 Hz

    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateTuneState();

    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (7074000 - 100 - 12000) * 100 = 706190000 (see Tune.cpp:98)
    // GetCWTXFreq_dHz() = 706190000 - 75000 = 706115000 (LSB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 706115000);
}

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromCWTransmitSpace) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 14074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_20M; // USB
    ED.CWToneIndex = 3; // 750 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (14074000 - 100 - 12000) * 100 = 1406190000 (see Tune.cpp:98)
    // GetCWTXFreq_dHz() = 1406190000 + 75000 = 1406265000 (USB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 1406265000);
}

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromCWTransmitDitMark) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_40M; // LSB
    ED.CWToneIndex = 2; // 656.5 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DIT_MARK;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (7074000 - 100 - 12000) * 100 = 706190000 (see Tune.cpp:98)
    // GetCWTXFreq_dHz() = 706190000 - 65650 = 706124350 (LSB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 706124350);
}

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromCWTransmitDahMark) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 14074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 200L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_20M; // USB
    ED.CWToneIndex = 1; // 562.5 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_DAH_MARK;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (14074000 - 200 - 12000) * 100 = 1406180000 (see Tune.cpp:98)
    // GetCWTXFreq_dHz() = 1406180000 + 56250 = 1406236250 (USB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 1406236250);
}

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromCWTransmitKeyerSpace) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 3574000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 50L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_80M; // LSB
    ED.CWToneIndex = 0; // 400 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (3574000 - 50 - 12000) * 100 = 356195000 (see Tune.cpp:98)
    // GetCWTXFreq_dHz() = 356195000 - 40000 = 356155000 (LSB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 356155000);
}

TEST(RFHardwareState, TuneStateMachine_UpdateTuneStateFromCWTransmitKeyerWait) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 21074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = -50L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_15M; // USB
    ED.CWToneIndex = 4; // 843.75 Hz
    
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT;
    UpdateTuneState();
    
    // Should set CW VFO frequency to GetCWTXFreq_dHz()
    // GetTXRXFreq_dHz() = (21074000 - (-50) - 12000) * 100 = 2106205000 (see Tune.cpp:98)
    // GetCWTXFreq_dHz() = 2106205000 + 84375 = 2106289375 (USB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 2106289375);
}

TEST(RFHardwareState, TuneStateMachine_StateTransitionSequenceSSBToReceive) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 14230000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    SampleRate = SAMPLE_RATE_48K;
    
    // Start in SSB receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateTuneState();
    EXPECT_EQ(GetSSBVFOFrequency(), 14230000);
    
    // Transition to SSB transmit
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (14230000 - 100 - 12000) * 100 = 1421790000 (see Tune.cpp:98)
    EXPECT_EQ(GetSSBVFOFrequency(), 14217900);
    
    // Back to SSB receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateTuneState();
    EXPECT_EQ(GetSSBVFOFrequency(), 14230000);
}

TEST(RFHardwareState, TuneStateMachine_StateTransitionSequenceCWReceiveToTransmit) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 7030000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 200L;
    SampleRate = SAMPLE_RATE_48K;
    ED.currentBand[ED.activeVFO] = BAND_40M; // LSB
    ED.CWToneIndex = 3; // 750 Hz
    
    // Start in CW receive
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateTuneState();
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 703000000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 703000000);
    
    // Transition to CW transmit mark
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_MARK;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (7030000 - 200 - 12000) * 100 = 701780000 (see Tune.cpp:98)
    // GetCWTXFreq_dHz() = 701780000 - 75000 = 701705000 (LSB)
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 701705000);

    // Transition to CW transmit space
    modeSM.state_id = ModeSm_StateId_CW_TRANSMIT_SPACE;
    UpdateTuneState();
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK2], 701705000);
    
    // Back to CW receive
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;
    UpdateTuneState();
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK0], 703000000);
    EXPECT_EQ(si5351.clk_freq[SI5351_CLK1], 703000000);
}

TEST(RFHardwareState, TuneStateMachine_DifferentSampleRates) {
    si5351 = Si5351(); // Reset mock
    ED.centerFreq_Hz[ED.activeVFO] = 14074000L;
    ED.fineTuneFreq_Hz[ED.activeVFO] = 100L;
    
    // Test with 192kHz sample rate
    SampleRate = SAMPLE_RATE_192K;
    modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (14074000 - 100 - 192000/4) * 100 = (14073900 - 48000) * 100 = 1402590000 (see Tune.cpp:98)
    EXPECT_EQ(GetSSBVFOFrequency(), 14025900);

    // Test with 96kHz sample rate
    SampleRate = SAMPLE_RATE_96K;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (14074000 - 100 - 96000/4) * 100 = (14073900 - 24000) * 100 = 1404990000 (see Tune.cpp:98)
    EXPECT_EQ(GetSSBVFOFrequency(), 14049900);

    // Test with 48kHz sample rate
    SampleRate = SAMPLE_RATE_48K;
    UpdateTuneState();
    // GetTXRXFreq_dHz() = (14074000 - 100 - 48000/4) * 100 = (14073900 - 12000) * 100 = 1406190000 (see Tune.cpp:98)
    EXPECT_EQ(GetSSBVFOFrequency(), 14061900);
}

// ================== BUFFER LOGGING TESTS ==================

TEST(RFHardwareState, BufferLogsSSBVFOStateChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test EnableSSBVFOOutput - should call SET_BIT which includes buffer_add()
    EnableSSBVFOOutput();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);
    EXPECT_EQ(buffer.head, 1);

    // Verify the register value was logged
    EXPECT_EQ(buffer.entries[0].register_value, hardwareRegister);

    // Test DisableSSBVFOOutput - should call CLEAR_BIT which includes buffer_add()
    DisableSSBVFOOutput();

    // Verify buffer has two entries
    EXPECT_EQ(buffer.count, 2);
    EXPECT_EQ(buffer.head, 2);

    // Verify the register values are different
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);

    // Verify timestamps are increasing
    EXPECT_LE(buffer.entries[0].timestamp, buffer.entries[1].timestamp);
}

TEST(RFHardwareState, BufferLogsCWVFOStateChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test EnableCWVFOOutput
    EnableCWVFOOutput();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    // Test DisableCWVFOOutput
    DisableCWVFOOutput();

    // Verify buffer has two entries with different register values
    EXPECT_EQ(buffer.count, 2);
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);
}

TEST(RFHardwareState, BufferLogsCWOnOffChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test CWon
    CWon();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    uint32_t register_after_on = buffer.entries[0].register_value;

    // Test CWoff
    CWoff();

    // Verify buffer has two entries
    EXPECT_EQ(buffer.count, 2);

    // Verify the register values are different
    EXPECT_NE(register_after_on, buffer.entries[1].register_value);

    // Verify timestamps are increasing
    EXPECT_LE(buffer.entries[0].timestamp, buffer.entries[1].timestamp);
}

TEST(RFHardwareState, BufferLogsModulationChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test SelectTXSSBModulation
    SelectTXSSBModulation();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    // Test SelectTXCWModulation
    SelectTXCWModulation();

    // Verify buffer has two entries with different register values
    EXPECT_EQ(buffer.count, 2);
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);
}

TEST(RFHardwareState, BufferLogsCalFeedbackChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test EnableCalFeedback
    EnableCalFeedback();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    // Test DisableCalFeedback
    DisableCalFeedback();

    // Verify buffer has two entries with different register values
    EXPECT_EQ(buffer.count, 2);
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);
}

TEST(RFHardwareState, BufferLogsRXTXModeChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Test SelectTXMode
    SelectTXMode();

    // Verify buffer has one entry
    EXPECT_EQ(buffer.count, 1);

    // Test SelectRXMode
    SelectRXMode();

    // Verify buffer has two entries with different register values
    EXPECT_EQ(buffer.count, 2);
    EXPECT_NE(buffer.entries[0].register_value, buffer.entries[1].register_value);
}

TEST(RFHardwareState, BufferLogsAttenuatorChanges) {
    // Initialize timing and buffer
    StartMillis();
    buffer.head = 0;
    buffer.count = 0;

    // Create RX attenuator (this should log register changes via SET_RF_GPA_RXATT macro)
    RXAttenuatorCreate(10.0);

    // Verify buffer has entries (creation may involve multiple register operations)
    EXPECT_GT(buffer.count, 0);

    size_t initial_count = buffer.count;

    // Change RX attenuation value
    SetRXAttenuation(20.0);

    // Verify buffer count increased
    EXPECT_GT(buffer.count, initial_count);

    // Create TX attenuator (this should log register changes via SET_RF_GPB_TXATT macro)
    size_t count_before_tx = buffer.count;
    TXAttenuatorCreate(15.0);

    // Verify buffer count increased
    EXPECT_GT(buffer.count, count_before_tx);

    // Change TX attenuation value
    size_t count_before_set_tx = buffer.count;
    SetTXAttenuation(25.0);

    // Verify buffer count increased
    EXPECT_GT(buffer.count, count_before_set_tx);
}

TEST(RFHardwareState, BufferLogsSequentialOperations) {
    // Initialize timing and buffer
    StartMillis();

    // Record the initial buffer count
    size_t initial_count = buffer.count;

    // Perform a simple sequence of operations
    EnableSSBVFOOutput();
    DisableSSBVFOOutput();

    // Verify we have at least 2 new entries
    EXPECT_GE(buffer.count, initial_count + 2);

    // Verify that buffer functionality is working by checking the count increased
    // This is a simple test that doesn't rely on complex buffer indexing
    EXPECT_GT(buffer.count, initial_count);
}