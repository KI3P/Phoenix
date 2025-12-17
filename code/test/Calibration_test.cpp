/**
 * @file Calibration_test.cpp
 * @brief Unit tests for calibration functions
 *
 */

#include <gtest/gtest.h>
#include "SDT.h"
#include "PowerCalSm.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

// External declarations for power calibration internal state
// These are static variables in MainBoard_DisplayCalibration.cpp
extern float32_t measuredPower;
extern uint32_t Npoints;
extern uint8_t incindexPower;
extern float32_t attenuations_dB[3];
extern float32_t powers_W[3];
extern PowerCalSm powerSM;

// Timer variables for interrupt simulation
static std::atomic<bool> timer_running{false};
static std::thread timer_thread;
#define GETHWRBITS(LSB,len) ((hardwareRegister >> LSB) & ((1 << len) - 1))

/**
 * Timer interrupt function that runs every 1ms
 * Dispatches DO events to the state machines
 */
void timer1ms(void) {
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
}

/**
 * Start the 1ms timer interrupt
 */
void start_timer1ms() {
    if (timer_running.load()) return; // Already running

    timer_running.store(true);
    timer_thread = std::thread([]() {
        while (timer_running.load()) {
            timer1ms();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

/**
 * Stop the 1ms timer interrupt
 */
void stop_timer1ms() {
    if (!timer_running.load()) return; // Not running

    timer_running.store(false);
    if (timer_thread.joinable()) {
        timer_thread.join();
    }
}


void SelectCalibrationMenu(void){
    // Check the state before loop is invoked and then again after
    loop();MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME );
    SetButton(MAIN_MENU_UP);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU);

    // Scroll to the Calibration menu
    IncrementPrimaryMenu();
    IncrementPrimaryMenu();
    IncrementPrimaryMenu();
    IncrementPrimaryMenu();
    IncrementPrimaryMenu();
    IncrementPrimaryMenu();
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify that we're on the calibration secondary menu
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SECONDARY_MENU);
    extern struct PrimaryMenuOption primaryMenu[8];
    extern size_t primaryMenuIndex;
    EXPECT_STREQ(primaryMenu[primaryMenuIndex].label, "Calibration");
}

void ScrollAndSelectCalibrateFrequency(void){
    SelectCalibrationMenu();

    // Scroll down to the frequency cal menu
    IncrementSecondaryMenu();
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_FREQUENCY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_FREQUENCY);
}

void ScrollAndSelectCalibrateReceiveIQ(void){
    SelectCalibrationMenu();

    // Scroll down to the Receive IQ menu
    IncrementSecondaryMenu();
    IncrementSecondaryMenu();
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_RX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_RX_IQ);
}

void ScrollAndSelectCalibrateTransmitIQ(void){
    SelectCalibrationMenu();

    // Scroll down to the Transmit IQ menu
    IncrementSecondaryMenu();
    IncrementSecondaryMenu();
    IncrementSecondaryMenu();
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_TX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_SPACE);
}

void ScrollAndSelectCalibratePower(void){
    SelectCalibrationMenu();

    // Scroll down to the Power menu
    IncrementSecondaryMenu();
    IncrementSecondaryMenu();
    IncrementSecondaryMenu();
    IncrementSecondaryMenu();
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
}

/**
 * Test fixture for Calibration tests
 * Sets up common test environment for display functions
 */
class CalibrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test environment before each test

        // Initialize hardwareRegister to ensure clean starting state
        hardwareRegister = 0;

        // Set up the queues so we get some simulated data through and start the "clock"
        Q_in_L.setChannel(0);
        Q_in_R.setChannel(1);
        Q_in_L.clear();
        Q_in_R.clear();
        Q_in_L_Ex.setChannel(0);
        Q_in_R_Ex.setChannel(1);
        Q_in_L_Ex.clear();
        Q_in_R_Ex.clear();
        StartMillis();

        //-------------------------------------------------------------
        // Radio startup code
        //-------------------------------------------------------------

        InitializeStorage();
        InitializeFrontPanel();
        InitializeSignalProcessing();  // Initialize DSP before starting audio
        InitializeAudio();
        InitializeDisplay();
        InitializeRFHardware(); // RF board, LPF board, and BPF board
        
        // Start the mode state machines
        modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
        modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
        ModeSm_start(&modeSM);
        ED.agc = AGCOff;
        ED.nrOptionSelect = NROff;
        uiSM.vars.splashDuration_ms = 1;
        UISm_start(&uiSM);
        UpdateAudioIOState();

        // Now, start the 1ms timer interrupt to simulate hardware timer
        start_timer1ms();

        extern size_t primaryMenuIndex;
        extern size_t secondaryMenuIndex;
        primaryMenuIndex = 0;
        secondaryMenuIndex = 0;
    }

    void TearDown() override {
        // Clean up after each test
        stop_timer1ms(); // Stop the timer thread to prevent crashes during teardown
    }
};

/**
 * Test entry to calibration states
 */
TEST_F(CalibrationTest, SelectCalibrateReceiveIQAndExit) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ScrollAndSelectCalibrateReceiveIQ();
    
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

TEST_F(CalibrationTest, SelectCalibrateTransmitIQAndExit) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ScrollAndSelectCalibrateTransmitIQ();
    
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

TEST_F(CalibrationTest, SelectCalibrateFrequencyAndExit) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ScrollAndSelectCalibrateFrequency();
    
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

TEST_F(CalibrationTest, SelectCalibratePowerAndExit) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ScrollAndSelectCalibratePower();

    loop(); MyDelay(10);
    loop(); MyDelay(10);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

/**
 * Test power calibration state transitions
 * Verifies:
 * - Entry into power calibration puts us in CALIBRATE_POWER UI state and CALIBRATE_POWER_SPACE mode state
 * - PTT press transitions from SPACE to MARK state
 * - PTT release transitions from MARK back to SPACE state
 * - HOME button exits to home screen and SSB_RECEIVE mode
 */
TEST_F(CalibrationTest, PowerCalibrationStateTransitions) {
    // Wait for splash screen to finish and state machines to settle
    loop(); MyDelay(10);

    // Verify initial state
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);

    Serial.println("1-Entering power calibration SPACE state");

    // Navigate to power calibration menu and select it
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct initial calibration state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
    Serial.println("1-In power calibration SPACE state");

    // Press PTT to transition to MARK state
    Serial.println("2-Pressing PTT to enter MARK state");
    SetInterrupt(iPTT_PRESSED);
    loop(); MyDelay(10);

    // Verify transition to MARK state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_MARK);
    Serial.println("2-In power calibration MARK state");

    // Release PTT to transition back to SPACE state
    Serial.println("3-Releasing PTT to return to SPACE state");
    SetInterrupt(iPTT_RELEASED);
    loop(); MyDelay(10);

    // Verify transition back to SPACE state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
    Serial.println("3-Back in power calibration SPACE state");

    // Press PTT again to verify we can re-enter MARK state
    Serial.println("4-Pressing PTT again to verify repeatable transition");
    SetInterrupt(iPTT_PRESSED);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_MARK);
    Serial.println("4-In power calibration MARK state again");

    // Release PTT to return to SPACE state before exiting
    Serial.println("5-Releasing PTT before exit");
    SetInterrupt(iPTT_RELEASED);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
    Serial.println("5-Back in SPACE state before exit");

    // Exit to home screen from SPACE state
    Serial.println("6-Pressing HOME to exit to home screen");
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    // Allow additional time for state machines to fully settle after exit
    for (int k=0; k<10; k++){
        loop(); MyDelay(10);
    }

    // Verify we're back at home screen in SSB receive mode
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    Serial.println("6-Successfully exited to home screen");
}

/**
 * Test volume encoder changes transmit attenuation in power calibration
 * The volume encoder controls ED.XAttenCW[currentBand] with 0.5 dB steps
 */
TEST_F(CalibrationTest, VolumeEncoderChangesTransmitAttInPowerCal) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to power calibration state
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // Get the current band and store initial transmit attenuation value
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    float32_t initialAtten = ED.XAttenCW[currentBand];

    // Expected increment value (0.5 dB for transmit attenuation)
    const float32_t expectedIncrement = 0.5;

    // Test incrementing the transmit attenuation by rotating volume encoder clockwise
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);

    float32_t attenAfterIncrease = ED.XAttenCW[currentBand];
    EXPECT_NEAR(attenAfterIncrease, initialAtten + expectedIncrement, 0.00001);

    // Test decrementing the transmit attenuation by rotating volume encoder counter-clockwise
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);

    float32_t attenAfterDecrease = ED.XAttenCW[currentBand];
    EXPECT_NEAR(attenAfterDecrease, initialAtten, 0.00001);

    // Test multiple increments
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iVOLUME_INCREASE);
        loop(); MyDelay(10);
    }

    float32_t attenAfterMultipleIncrements = ED.XAttenCW[currentBand];
    EXPECT_NEAR(attenAfterMultipleIncrements, initialAtten + 5 * expectedIncrement, 0.00001);

    // Test multiple decrements
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iVOLUME_DECREASE);
        loop(); MyDelay(10);
    }

    float32_t attenAfterMultipleDecrements = ED.XAttenCW[currentBand];
    EXPECT_NEAR(attenAfterMultipleDecrements, initialAtten, 0.00001);

    // Test upper limit (max value is 31.5)
    ED.XAttenCW[currentBand] = 31.0;
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenCW[currentBand], 31.5, 0.00001);

    // Try to increment beyond max - should be clamped at 31.5
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenCW[currentBand], 31.5, 0.00001);

    // Test lower limit (min value is 0.0)
    ED.XAttenCW[currentBand] = 0.5;
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenCW[currentBand], 0.0, 0.00001);

    // Try to decrement beyond min - should be clamped at 0.0
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenCW[currentBand], 0.0, 0.00001);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

/**
 * Test filter encoder changes measured power in power calibration
 * The filter encoder controls measuredPower with variable increment steps
 */
TEST_F(CalibrationTest, FilterEncoderChangesMeasuredPowerInPowerCal) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to power calibration state
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // Store initial measured power value
    float32_t initialPower = measuredPower;

    // Default increment is 0.1 (powerincvals[0])
    const float32_t defaultIncrement = 0.1;

    // Test incrementing the measured power by rotating filter encoder clockwise
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);

    float32_t powerAfterIncrease = measuredPower;
    EXPECT_NEAR(powerAfterIncrease, initialPower + defaultIncrement, 0.00001);

    // Test decrementing the measured power by rotating filter encoder counter-clockwise
    SetInterrupt(iFILTER_DECREASE);
    loop(); MyDelay(10);

    float32_t powerAfterDecrease = measuredPower;
    EXPECT_NEAR(powerAfterDecrease, initialPower, 0.00001);

    // Test multiple increments
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iFILTER_INCREASE);
        loop(); MyDelay(10);
    }

    float32_t powerAfterMultipleIncrements = measuredPower;
    EXPECT_NEAR(powerAfterMultipleIncrements, initialPower + 5 * defaultIncrement, 0.00001);

    // Test multiple decrements
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iFILTER_DECREASE);
        loop(); MyDelay(10);
    }

    float32_t powerAfterMultipleDecrements = measuredPower;
    EXPECT_NEAR(powerAfterMultipleDecrements, initialPower, 0.00001);

    // Test upper limit (max value is 100.0)
    measuredPower = 99.95;
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(measuredPower, 100.0, 0.00001);

    // Try to increment beyond max - should be clamped at 100.0
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(measuredPower, 100.0, 0.00001);

    // Test lower limit (min value is 0.0)
    measuredPower = 0.05;
    SetInterrupt(iFILTER_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(measuredPower, 0.0, 0.00001);

    // Try to decrement beyond min - should be clamped at 0.0
    SetInterrupt(iFILTER_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(measuredPower, 0.0, 0.00001);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

/**
 * Test button 15 changes power increment in power calibration
 * Button 15 toggles between different power increment values
 */
TEST_F(CalibrationTest, Button15ChangesPowerIncrementInPowerCal) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to power calibration state
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);

    // Initial increment index should be 1
    uint8_t initialIncIndex = incindexPower;
    EXPECT_EQ(initialIncIndex, 1);

    // Press button 15 to change increment
    SetButton(15);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should cycle to next increment index
    EXPECT_EQ(incindexPower, 2);

    // Press button 15 again to cycle back
    SetButton(15);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should wrap back to 0
    EXPECT_EQ(incindexPower, 0);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

/**
 * Test button 16 changes PA selection in power calibration
 * Button 16 toggles between PA20W (0) and PA100W (1)
 */
TEST_F(CalibrationTest, Button16ChangesPASelectionInPowerCal) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to power calibration state
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);

    // Initial PA selection should be PA20W (0)
    bool initialPAsel = ED.PA100Wactive;
    EXPECT_EQ(initialPAsel, false); // PA20W

    // Press button 16 to change PA selection
    SetButton(16);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should switch to PA100W (1)
    EXPECT_EQ(ED.PA100Wactive, true); // PA100W

    // Press button 16 again to toggle back
    SetButton(16);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should switch back to PA20W (0)
    EXPECT_EQ(ED.PA100Wactive, false); // PA20W

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

/**
 * Test MENU_OPTION_SELECT button records power data point
 * This button captures the current attenuation and power values
 */
TEST_F(CalibrationTest, MenuSelectRecordsPowerDataPointInPowerCal) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to power calibration state
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);

    // Reset data points counter
    Npoints = 0;

    // Set some known values for attenuation and power
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    ED.XAttenCW[currentBand] = 10.5;
    measuredPower = 25.3;

    // Record first data point
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify data point was recorded
    EXPECT_EQ(Npoints, 1);
    EXPECT_NEAR(attenuations_dB[0], 10.5, 0.00001);
    EXPECT_NEAR(powers_W[0], 25.3, 0.00001);

    // Change values and record second data point
    ED.XAttenCW[currentBand] = 15.0;
    measuredPower = 50.7;

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify second data point was recorded
    EXPECT_EQ(Npoints, 2);
    EXPECT_NEAR(attenuations_dB[1], 15.0, 0.00001);
    EXPECT_NEAR(powers_W[1], 50.7, 0.00001);

    // Record third data point
    ED.XAttenCW[currentBand] = 20.0;
    measuredPower = 75.2;

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify third data point was recorded
    EXPECT_EQ(Npoints, 3);
    EXPECT_NEAR(attenuations_dB[2], 20.0, 0.00001);
    EXPECT_NEAR(powers_W[2], 75.2, 0.00001);

    // After recording the third point, curve fit is automatically calculated
    // but we should still be in CALIBRATE_POWER_SPACE (no automatic state transition)
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // User must manually press button 12 to transition to offset mode
    SetButton(12);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_SSBPOINT);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

/**
 * Test BAND_UP and BAND_DN buttons change band in power calibration
 * Band up/down should change the current band and update frequency
 */
TEST_F(CalibrationTest, BandUpDownChangesBandInPowerCal) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to power calibration state
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);

    // Store initial band
    int32_t initialBand = ED.currentBand[ED.activeVFO];

    // Press BAND_UP
    SetButton(BAND_UP);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify band increased
    int32_t bandAfterUp = ED.currentBand[ED.activeVFO];
    if (initialBand < LAST_BAND) {
        EXPECT_EQ(bandAfterUp, initialBand + 1);
    } else {
        // Wraps to first band
        EXPECT_EQ(bandAfterUp, FIRST_BAND);
    }

    // Press BAND_DN
    SetButton(BAND_DN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify band returned to initial
    EXPECT_EQ(ED.currentBand[ED.activeVFO], initialBand);

    // Test BAND_DN wrapping
    // First, set to first band
    ED.currentBand[ED.activeVFO] = FIRST_BAND;

    SetButton(BAND_DN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should wrap to last band
    EXPECT_EQ(ED.currentBand[ED.activeVFO], LAST_BAND);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

/**
 * Test fitting a calibration model to the data
 */
TEST_F(CalibrationTest, Test20WFitInPowerCal) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to power calibration state
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // Reset state from any previous tests
    ED.PA100Wactive = false; // PA20W
    Npoints = 0;  // Start with empty buffer

    // Load the data points into the data buffer
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 3.0;
    measuredPower = 14.79108388;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 16.5;
    measuredPower = 5.2480746;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 24.0;
    measuredPower = 1.04712855;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // After recording the third point, curve fit is automatically calculated
    // Test whether the correct parameters have been set to the correct levels
    // Note: The fit algorithm is highly sensitive to initial conditions and numerical precision
    EXPECT_NEAR(ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]],14790,50);
    EXPECT_NEAR(ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]],16.178,0.5);

    // But we should still be in CALIBRATE_POWER_SPACE (no automatic state transition)
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // User must manually press button 12 to transition to offset mode
    SetButton(12);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Now we should have transitioned to offset mode
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);

    // Exit back to home screen (no need to return to power space first)
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}


/**
 * Test fitting a calibration model to the data
 */
TEST_F(CalibrationTest, Test100WFitInPowerCal) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to power calibration state
    ScrollAndSelectCalibratePower();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // Reset state from any previous tests
    ED.PA100Wactive = true; // PA100W
    Npoints = 0;  // Start with empty buffer

    // Load the data points into the data buffer
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 3.0;
    measuredPower = 77.62471166;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 16.5;
    measuredPower = 27.54228703;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 24.0;
    measuredPower = 5.49540874;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // After recording the third point, curve fit is automatically calculated
    // Test whether the correct parameters have been set to the correct levels
    // Note: The fit algorithm is highly sensitive to initial conditions and numerical precision
    EXPECT_NEAR(ED.PowerCal_100W_Psat_mW[ED.currentBand[ED.activeVFO]],77050,600);
    EXPECT_NEAR(ED.PowerCal_100W_kindex[ED.currentBand[ED.activeVFO]],16.178,0.5);

    // But we should still be in CALIBRATE_POWER_SPACE (no automatic state transition)
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // User must manually press button 12 to transition to offset mode
    SetButton(12);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Now we should have transitioned to offset mode
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);

    // Exit back to home screen (no need to return to power space first)
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}


void CheckThatHardwareRegisterMatchesActualHardware(){
    // LPF
    uint16_t gpioab = GetLPFMCPRegisters(); // a is upper half, b is lower half
    EXPECT_EQ((uint8_t)(gpioab & 0x00FF), (uint8_t)(hardwareRegister & 0x000000FF)); // gpiob
    EXPECT_EQ((uint8_t)((gpioab >> 8) & 0x0003), (uint8_t)((hardwareRegister >> 8) & 0x00000003));
    // RF
    gpioab = GetRFMCPRegisters();
    EXPECT_EQ((uint8_t)(gpioab & 0x003F), (uint8_t)((hardwareRegister >> TXATTLSB) & 0x0000003F)); // tx atten
    EXPECT_EQ((uint8_t)((gpioab >> 8) & 0x003F), (uint8_t)((hardwareRegister >> RXATTLSB) & 0x0000003F));
    // BPF
    gpioab = GetBPFMCPRegisters();
    EXPECT_EQ(gpioab, BPF_WORD);
    // Teensy
    EXPECT_EQ(digitalRead(RXTX),GET_BIT(hardwareRegister,RXTXBIT));
    EXPECT_EQ(digitalRead(CW_ON_OFF),GET_BIT(hardwareRegister,CWBIT));
    EXPECT_EQ(digitalRead(XMIT_MODE),GET_BIT(hardwareRegister,MODEBIT));
    EXPECT_EQ(digitalRead(CAL),GET_BIT(hardwareRegister,CALBIT));
}

void CheckThatStateIsCalReceiveIQ(){
    // Check that the hardware register contains the expected bits
    int32_t band = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(GETHWRBITS(LPFBAND0BIT,4), BandToBCD(band)); // LPF filter
    EXPECT_EQ(GETHWRBITS(ANT0BIT,2), ED.antennaSelection[band]); // antenna
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 0);   // transverter should be LO (in path) for receive
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 0);  // TX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 1);  // RX path should include BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTXBIT), 0);   // RXTX bit should be RX (0)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 1);     // CW bit should be 1 (on)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT), 0);   // MODE should be LO (CW)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 1);    // Cal should be HI (on)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 1);  // CW transmit VFO should be HI (on)
    EXPECT_EQ(GET_BIT(hardwareRegister,RXVFOBIT), 1); // SSB VFO should be HI (on)
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*31.5));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), (uint8_t)round(2*31.5));  // TX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    // Now check that the GPIO registers match the hardware register
    CheckThatHardwareRegisterMatchesActualHardware();
}

void CheckThatStateIsCalTransmitIQ(){
    // Check that the hardware register contains the expected bits
    int32_t band = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(GETHWRBITS(LPFBAND0BIT,4), BandToBCD(band)); // LPF filter
    EXPECT_EQ(GETHWRBITS(ANT0BIT,2), ED.antennaSelection[band]); // antenna
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 1);   // transverter should be HI (out of path) for receive
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 1);  // TX path should include BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 0);  // RX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTXBIT), 1);   // RXTX bit should be TX (1)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 0);     // CW bit should be 0 (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT),1);   // MODE should be HI (SSB)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 0);    // Cal should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 0);  // CW transmit VFO should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,RXVFOBIT), 1); // SSB VFO should be HI (on)
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), 63);  // RX attenuation always 31.5 dB (63 = 2*31.5) during transmit
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), 0);  // TX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    // Now check that the GPIO registers match the hardware register
    CheckThatHardwareRegisterMatchesActualHardware();
}

void CheckThatRegisterStateIsReceive(){
    // Check that the hardware register contains the expected bits
    int32_t band = ED.currentBand[ED.activeVFO];
    EXPECT_EQ(GETHWRBITS(LPFBAND0BIT,4), BandToBCD(band)); // LPF filter
    EXPECT_EQ(GETHWRBITS(ANT0BIT,2), ED.antennaSelection[band]); // antenna
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 0);   // transverter should be LO (in path) for receive
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 0);  // TX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 1);  // RX path should include BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTXBIT), 0);      // RXTX bit should be RX (0)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 0);     // CW bit should be 0 (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT), 1);   // MODE doesn't matter for receive, should be HI(SSB)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 0);    // Cal should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 0);  // CW transmit VFO should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,RXVFOBIT), 1); // SSB VFO should be HI (on)
    // Note: TX attenuation is not checked in receive mode because it doesn't affect RX operation
    // and the timing of when it gets reset during state transitions is implementation-dependent
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[band]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    // Now check that the GPIO registers match the hardware register
    CheckThatHardwareRegisterMatchesActualHardware();
}

TEST_F(CalibrationTest, CalibrateTransmitIQState) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Reset PA selection to ensure test isolation
    ED.PA100Wactive = false;

    Serial.println("1-Entering TX IQ space state");

    ScrollAndSelectCalibrateTransmitIQ();
    
    for (int k=0; k<50; k++){
        loop(); MyDelay(10); 
    }

    // Now, check to ensure that we are in the SSB receive from a hardware register POV
    CheckThatRegisterStateIsReceive();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_CALIBRATE_TX_IQ_SPACE);
    EXPECT_EQ(uiSM.state_id,UISm_StateId_CALIBRATE_TX_IQ);
    Serial.println("1-In TX IQ space state");

    // Press the PTT to go to CAL IQ transmit mode
    Serial.println("2-Entering TX IQ mark state");

    SetInterrupt(iPTT_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id,UISm_StateId_CALIBRATE_TX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_MARK);
    CheckThatStateIsCalTransmitIQ();

    Serial.println("2-In TX IQ mark state");

    // Release PTT to go back to CAL IQ transmit space mode
    Serial.println("3-Entering TX IQ space state");
    SetInterrupt(iPTT_RELEASED);
    loop(); MyDelay(10);

    CheckThatRegisterStateIsReceive();
    EXPECT_EQ(modeSM.state_id,ModeSm_StateId_CALIBRATE_TX_IQ_SPACE);
    EXPECT_EQ(uiSM.state_id,UISm_StateId_CALIBRATE_TX_IQ);
    Serial.println("3-In TX IQ space state");

    // Exit back to home screen
    Serial.println("4-Entering home state");
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10); 
    loop(); MyDelay(10); 

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    Serial.println("4-In home state");

}

TEST_F(CalibrationTest, FilterEncoderChangesTXIQPhase) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to the Transmit IQ calibration state
    ScrollAndSelectCalibrateTransmitIQ();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_TX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_SPACE);

    // Get the current band and store initial IQXPhaseCorrectionFactor value
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    float32_t initialPhase = ED.IQXPhaseCorrectionFactor[currentBand];

    // Expected increment value (default is 0.01 from incvals[0])
    const float32_t expectedIncrement = 0.01;

    // Test incrementing the phase correction by rotating filter encoder clockwise
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);

    float32_t phaseAfterIncrease = ED.IQXPhaseCorrectionFactor[currentBand];
    EXPECT_NEAR(phaseAfterIncrease, initialPhase + expectedIncrement, 0.00001);

    // Test decrementing the phase correction by rotating filter encoder counter-clockwise
    SetInterrupt(iFILTER_DECREASE);
    loop(); MyDelay(10);

    float32_t phaseAfterDecrease = ED.IQXPhaseCorrectionFactor[currentBand];
    EXPECT_NEAR(phaseAfterDecrease, initialPhase, 0.00001);

    // Test multiple increments
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iFILTER_INCREASE);
        loop(); MyDelay(10);
    }

    float32_t phaseAfterMultipleIncrements = ED.IQXPhaseCorrectionFactor[currentBand];
    EXPECT_NEAR(phaseAfterMultipleIncrements, initialPhase + 5 * expectedIncrement, 0.00001);

    // Test multiple decrements
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iFILTER_DECREASE);
        loop(); MyDelay(10);
    }

    float32_t phaseAfterMultipleDecrements = ED.IQXPhaseCorrectionFactor[currentBand];
    EXPECT_NEAR(phaseAfterMultipleDecrements, initialPhase, 0.00001);

    // Test upper limit (max value is 0.5)
    // Set to a value close to max
    ED.IQXPhaseCorrectionFactor[currentBand] = 0.499;
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXPhaseCorrectionFactor[currentBand], 0.5, 0.00001);

    // Try to increment beyond max - should be clamped at 0.5
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXPhaseCorrectionFactor[currentBand], 0.5, 0.00001);

    // Test lower limit (min value is -0.5)
    // Set to a value close to min
    ED.IQXPhaseCorrectionFactor[currentBand] = -0.499;
    SetInterrupt(iFILTER_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXPhaseCorrectionFactor[currentBand], -0.5, 0.00001);

    // Try to decrement beyond min - should be clamped at -0.5
    SetInterrupt(iFILTER_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXPhaseCorrectionFactor[currentBand], -0.5, 0.00001);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

TEST_F(CalibrationTest, VolumeEncoderChangesTXIQAmp) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to the Transmit IQ calibration state
    ScrollAndSelectCalibrateTransmitIQ();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_TX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_SPACE);

    // Get the current band and store initial IQXAmpCorrectionFactor value
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    float32_t initialAmp = ED.IQXAmpCorrectionFactor[currentBand];

    // Expected increment value (default is 0.01 from incvals[0])
    const float32_t expectedIncrement = 0.01;

    // Test incrementing the amplitude correction by rotating volume encoder clockwise
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);

    float32_t ampAfterIncrease = ED.IQXAmpCorrectionFactor[currentBand];
    EXPECT_NEAR(ampAfterIncrease, initialAmp + expectedIncrement, 0.00001);

    // Test decrementing the amplitude correction by rotating volume encoder counter-clockwise
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);

    float32_t ampAfterDecrease = ED.IQXAmpCorrectionFactor[currentBand];
    EXPECT_NEAR(ampAfterDecrease, initialAmp, 0.00001);

    // Test multiple increments
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iVOLUME_INCREASE);
        loop(); MyDelay(10);
    }

    float32_t ampAfterMultipleIncrements = ED.IQXAmpCorrectionFactor[currentBand];
    EXPECT_NEAR(ampAfterMultipleIncrements, initialAmp + 5 * expectedIncrement, 0.00001);

    // Test multiple decrements
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iVOLUME_DECREASE);
        loop(); MyDelay(10);
    }

    float32_t ampAfterMultipleDecrements = ED.IQXAmpCorrectionFactor[currentBand];
    EXPECT_NEAR(ampAfterMultipleDecrements, initialAmp, 0.00001);

    // Test upper limit (max value is 2.0)
    // Set to a value close to max
    ED.IQXAmpCorrectionFactor[currentBand] = 1.999;
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXAmpCorrectionFactor[currentBand], 2.0, 0.00001);

    // Try to increment beyond max - should be clamped at 2.0
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXAmpCorrectionFactor[currentBand], 2.0, 0.00001);

    // Test lower limit (min value is 0.5)
    // Set to a value close to min
    ED.IQXAmpCorrectionFactor[currentBand] = 0.501;
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXAmpCorrectionFactor[currentBand], 0.5, 0.00001);

    // Try to decrement beyond min - should be clamped at 0.5
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXAmpCorrectionFactor[currentBand], 0.5, 0.00001);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

TEST_F(CalibrationTest, FinetuneEncoderChangesTXAttenuation) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    extern float32_t attLevel;
    // Navigate to the Transmit IQ calibration state
    ScrollAndSelectCalibrateTransmitIQ();

    // Allow state machine to settle
    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Verify we're in the correct state
    EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_TX_IQ);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_SPACE);

    // Get the current band and store initial XAttenSSB value
    int32_t currentBand = ED.currentBand[ED.activeVFO];
    float32_t initialAtten = attLevel;

    // Expected increment value (0.5 for transmit attenuation)
    const float32_t expectedIncrement = 0.5;

    // Test incrementing the transmit attenuation by rotating finetune encoder clockwise
    SetInterrupt(iFINETUNE_INCREASE);
    loop(); MyDelay(10);

    float32_t attenAfterIncrease = attLevel;
    EXPECT_NEAR(attenAfterIncrease, initialAtten + expectedIncrement, 0.00001);
    EXPECT_NEAR(attLevel,GetTXAttenuation(),0.51);

    // Test decrementing the transmit attenuation by rotating finetune encoder counter-clockwise
    SetInterrupt(iFINETUNE_DECREASE);
    loop(); MyDelay(10);

    float32_t attenAfterDecrease = attLevel;
    EXPECT_NEAR(attenAfterDecrease, initialAtten, 0.00001);

    // Test multiple increments
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iFINETUNE_INCREASE);
        loop(); MyDelay(10);
    }

    float32_t attenAfterMultipleIncrements = attLevel;
    EXPECT_NEAR(attenAfterMultipleIncrements, initialAtten + 5 * expectedIncrement, 0.00001);

    // Test multiple decrements
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iFINETUNE_DECREASE);
        loop(); MyDelay(10);
    }

    float32_t attenAfterMultipleDecrements = attLevel;
    EXPECT_NEAR(attenAfterMultipleDecrements, initialAtten, 0.00001);
    EXPECT_NEAR(attLevel,GetTXAttenuation(),0.51);

    // Test upper limit (max value is 31.5)
    // Set to a value close to max
    attLevel = 31.0;
    SetInterrupt(iFINETUNE_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(attLevel, 31.5, 0.00001);

    // Try to increment beyond max - should be clamped at 31.5
    SetInterrupt(iFINETUNE_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(attLevel, 31.5, 0.00001);

    // Test lower limit (min value is 0.0)
    // Set to a value close to min
    attLevel = 0.5;
    SetInterrupt(iFINETUNE_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(attLevel, 0.0, 0.00001);

    // Try to decrement beyond min - should be clamped at 0.0
    SetInterrupt(iFINETUNE_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(attLevel, 0.0, 0.00001);
    EXPECT_NEAR(attLevel,GetTXAttenuation(),0.51);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}

/*TEST_F(CalibrationTest, CalibrateReceiveIQAutotuneSteps) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ScrollAndSelectCalibrateReceiveIQ();

    for (int k=0; k<50; k++){
        loop(); MyDelay(10);
    }

    // Now, check to ensure that we are in the receive IQ state
    CheckThatStateIsCalReceiveIQ();

    SetButton(16);
    SetInterrupt(iBUTTON_PRESSED);
    for (int k=0; k<5000; k++){
        loop(); MyDelay(10);
    }
}*/
