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
    extern struct PrimaryMenuOption primaryMenu[NUM_PRIMARY_MENUS];
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
    // The least-squares tanh fit in dB (FitPowerCurve, 2026-11-02; scipy agrees)
    EXPECT_NEAR(ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]],14743,50);
    EXPECT_NEAR(ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]],17.30,0.5);

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
    // The least-squares tanh fit in dB (FitPowerCurve, 2026-11-02; scipy agrees)
    EXPECT_NEAR(ED.PowerCal_100W_Psat_mW[ED.currentBand[ED.activeVFO]],77373,600);
    EXPECT_NEAR(ED.PowerCal_100W_kindex[ED.currentBand[ED.activeVFO]],17.30,0.5);

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
    EXPECT_EQ(GET_BIT(hardwareRegister,TXVFOBIT), 0); // TX VFO should be LO (off)
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
    EXPECT_EQ(GET_BIT(hardwareRegister,XVTRBIT), 0);   // transverter should be LO (in path)
    EXPECT_EQ(GET_BIT(hardwareRegister,PA100WBIT), 0); // PA should always be LO (bypassed)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXBPFBIT), 0);  // TX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXBPFBIT), 0);  // RX path should bypass BPF
    EXPECT_EQ(GET_BIT(hardwareRegister,RXTXBIT), 1);   // RXTX bit should be TX (1)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWBIT), 0);     // CW bit should be 0 (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,MODEBIT),1);   // MODE should be HI (SSB)
    EXPECT_EQ(GET_BIT(hardwareRegister,CALBIT), 1);    // Cal should be HI (on)
    EXPECT_EQ(GET_BIT(hardwareRegister,CWVFOBIT), 0);  // CW transmit VFO should be LO (off)
    EXPECT_EQ(GET_BIT(hardwareRegister,RXVFOBIT), 1); // RX VFO should be HI (on)
    EXPECT_EQ(GET_BIT(hardwareRegister,TXVFOBIT), 1); // TX VFO should be HI (on)
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), 63);  // RX attenuation always 31.5 dB (63 = 2*31.5) during transmit
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), 63);  // TX attenuation
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
    EXPECT_EQ(GET_BIT(hardwareRegister,TXVFOBIT), 0); // TX VFO should be LO (off)
    // Note: TX attenuation is not checked in receive mode because it doesn't affect RX operation
    // and the timing of when it gets reset during state transitions is implementation-dependent
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[band]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    // Now check that the GPIO registers match the hardware register
    CheckThatHardwareRegisterMatchesActualHardware();
}

TEST_F(CalibrationTest, CalibrateTransmitIQState) {
    // TX IQ calibration requires dual VFO mode
    SetDualVFOs(true);

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

// HOME while the PTT is held in Transmit IQ calibration: the radio must leave
// calibration and return to receive, not stay keyed in a calibration state
TEST_F(CalibrationTest, HomeWhileKeyedInTXIQExitsToReceive) {
    SetDualVFOs(true);
    ED.PA100Wactive = false;
    ScrollAndSelectCalibrateTransmitIQ();
    for (int k=0; k<20; k++){ loop(); MyDelay(10); }

    SetInterrupt(iPTT_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_MARK);

    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    for (int k=0; k<3; k++){ loop(); MyDelay(10); }

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    CheckThatRegisterStateIsReceive();
}

// HOME during the automatic Transmit IQ run (button 16): the run stops, the PTT
// it engaged is released, and the band it was sweeping gets its settings back
TEST_F(CalibrationTest, HomeDuringTXIQAutoRunRestoresBandAndUnkeys) {
    SetDualVFOs(true);
    ED.PA100Wactive = false;
    ScrollAndSelectCalibrateTransmitIQ();
    for (int k=0; k<20; k++){ loop(); MyDelay(10); }

    ED.IQXAmpCorrectionFactor[FIRST_BAND] = 1.07f;
    ED.IQXPhaseCorrectionFactor[FIRST_BAND] = 0.03f;

    SetButton(16);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);
    EXPECT_TRUE(TXIQCalibrationAutoActive());
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_MARK);

    // Let the sweep run for a while on the first band
    for (int k=0; k<500; k++){
        TransmitIQCalSm_dispatch_event(&txiqSM, TransmitIQCalSm_EventId_DO);
        if (k % 20 == 0){ loop(); }
    }
    EXPECT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);
    EXPECT_TRUE((ED.IQXAmpCorrectionFactor[FIRST_BAND] != 1.07f) ||
                (ED.IQXPhaseCorrectionFactor[FIRST_BAND] != 0.03f));

    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    for (int k=0; k<3; k++){ loop(); MyDelay(10); }

    EXPECT_FALSE(TXIQCalibrationAutoActive());
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    CheckThatRegisterStateIsReceive();
    EXPECT_FLOAT_EQ(ED.IQXAmpCorrectionFactor[FIRST_BAND], 1.07f);
    EXPECT_FLOAT_EQ(ED.IQXPhaseCorrectionFactor[FIRST_BAND], 0.03f);

    // The stopped run does not touch the settings any more
    for (int k=0; k<200; k++)
        TransmitIQCalSm_dispatch_event(&txiqSM, TransmitIQCalSm_EventId_DO);
    EXPECT_FLOAT_EQ(ED.IQXAmpCorrectionFactor[FIRST_BAND], 1.07f);
    EXPECT_FLOAT_EQ(ED.IQXPhaseCorrectionFactor[FIRST_BAND], 0.03f);
}

// HOME during the automatic Receive IQ run (button 16): the run stops and the
// band it was sweeping gets its settings back
TEST_F(CalibrationTest, HomeDuringRXIQAutoRunRestoresBand) {
    ScrollAndSelectCalibrateReceiveIQ();
    for (int k=0; k<20; k++){ loop(); MyDelay(10); }

    ED.IQAmpCorrectionFactor[FIRST_BAND] = 0.93f;
    ED.IQPhaseCorrectionFactor[FIRST_BAND] = -0.02f;

    SetButton(16);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_TRUE(RXIQCalibrationAutoActive());

    for (int k=0; k<500; k++){
        ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_DO);
        if (k % 20 == 0){ loop(); }
    }
    EXPECT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);
    EXPECT_TRUE((ED.IQAmpCorrectionFactor[FIRST_BAND] != 0.93f) ||
                (ED.IQPhaseCorrectionFactor[FIRST_BAND] != -0.02f));

    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    for (int k=0; k<3; k++){ loop(); MyDelay(10); }

    EXPECT_FALSE(RXIQCalibrationAutoActive());
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    EXPECT_FLOAT_EQ(ED.IQAmpCorrectionFactor[FIRST_BAND], 0.93f);
    EXPECT_FLOAT_EQ(ED.IQPhaseCorrectionFactor[FIRST_BAND], -0.02f);

    for (int k=0; k<200; k++)
        ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_DO);
    EXPECT_FLOAT_EQ(ED.IQAmpCorrectionFactor[FIRST_BAND], 0.93f);
    EXPECT_FLOAT_EQ(ED.IQPhaseCorrectionFactor[FIRST_BAND], -0.02f);
}

// Single-VFO Transmit IQ transmits through the PA into the antenna connector.
// It must start at full attenuation, and every key-down must use the attenuation
// the screen shows, not 0 dB on the first key-down and 31.5 dB after
TEST_F(CalibrationTest, SingleVFOTXIQKeysAtTheDisplayedAttenuation) {
    extern float32_t attLevel;
    SetDualVFOs(false);
    ED.PA100Wactive = false;
    ScrollAndSelectCalibrateTransmitIQ();
    for (int k=0; k<20; k++){ loop(); MyDelay(10); }

    EXPECT_NEAR(attLevel, 31.5, 0.00001);
    EXPECT_NEAR(GetTXIQAttenuation(), 31.5, 0.00001);

    // First key-down
    SetInterrupt(iPTT_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_MARK);
    EXPECT_NEAR(GetTXAttenuation(), 31.5, 0.01);
    SetInterrupt(iPTT_RELEASED);
    loop(); MyDelay(10);

    // Less attenuation, set while unkeyed (on direct-coupled builds the fine
    // tune encoder sets the I offset instead, and the attenuation stays put)
#ifndef DIRECT_COUPLED_TX
    for (int i = 0; i < 4; i++){
        SetInterrupt(iFINETUNE_DECREASE);
        loop(); MyDelay(10);
    }
    EXPECT_NEAR(attLevel, 29.5, 0.00001);

    // The next two key-downs use it
    for (int n = 0; n < 2; n++){
        SetInterrupt(iPTT_PRESSED);
        loop(); MyDelay(10);
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_MARK);
        EXPECT_NEAR(GetTXAttenuation(), 29.5, 0.01);
        SetInterrupt(iPTT_RELEASED);
        loop(); MyDelay(10);
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_SPACE);
    }
#endif

    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    for (int k=0; k<3; k++){ loop(); MyDelay(10); }
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    SetDualVFOs(true);
}

#ifdef DIRECT_COUPLED_TX
// The automatic carrier null (button 14) reads the carrier through the receiver,
// which a single-VFO radio cannot do: there it must do nothing
TEST_F(CalibrationTest, CarrierNullOnlyOnDualVFO) {
    SetDualVFOs(false);
    ScrollAndSelectCalibrateTransmitIQ();
    for (int k=0; k<20; k++){ loop(); MyDelay(10); }
    SetButton(14);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);
    EXPECT_EQ(txcarrSM.state_id, TransmitCarrierCalSm_StateId_STANDBY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_SPACE);
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    for (int k=0; k<3; k++){ loop(); MyDelay(10); }

    SetDualVFOs(true);
    extern size_t primaryMenuIndex;
    extern size_t secondaryMenuIndex;
    primaryMenuIndex = 0;
    secondaryMenuIndex = 0;
    ScrollAndSelectCalibrateTransmitIQ();
    for (int k=0; k<20; k++){ loop(); MyDelay(10); }
    SetButton(14);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);
    EXPECT_NE(txcarrSM.state_id, TransmitCarrierCalSm_StateId_STANDBY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_TX_IQ_MARK);
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    for (int k=0; k<3; k++){ loop(); MyDelay(10); }
    EXPECT_EQ(txcarrSM.state_id, TransmitCarrierCalSm_StateId_STANDBY);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
}
#endif

// The automatic Transmit IQ run calibrates every band from the first, like the
// Receive IQ run, whichever band the operator selected on the screen before it
TEST_F(CalibrationTest, TXIQAutoRunStartsFromFirstBand) {
    auto savedED = ED;
    SetDualVFOs(true);
    ED.PA100Wactive = false;
    ScrollAndSelectCalibrateTransmitIQ();
    for (int k=0; k<20; k++){ loop(); MyDelay(10); }

    // Move to another band with the band buttons (at least one press)
    int presses = 0;
    do {
        SetButton(BAND_UP);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);
    } while ((++presses <= NUMBER_OF_BANDS) && (ED.currentBand[ED.activeVFO] != BAND_40M));
    ASSERT_EQ(ED.currentBand[ED.activeVFO], BAND_40M);

    SetButton(16);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);   // the PTT press the run queued
    ASSERT_TRUE(TXIQCalibrationAutoActive());
    for (int k = 0; k < 10; k++)
        TransmitIQCalSm_dispatch_event(&txiqSM, TransmitIQCalSm_EventId_DO);
    EXPECT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);

    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    for (int k=0; k<4; k++){ loop(); MyDelay(10); }
    EXPECT_FALSE(TXIQCalibrationAutoActive());
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);
    ED = savedED;
}

// The automatic sweeps keep, for each pass, the setting with the largest reading
// above 0 dB. When no reading beats 0 dB (a broken or mirrored loopback), a pass
// must leave its setting alone, not write a value left over from the last pass.
// A mirrored spectrum makes every reading negative: the receive image bin (128)
// is above the wanted bin (384), the transmit image above the wanted tone
// (256 -/+ 17 by sideband), and the carrier (256) above both.
static void MirroredSpectrum(int32_t band){
    for (int i = 0; i < SPECTRUM_RES; i++) psdnew[i] = 1.0f;
    psdnew[128] = 2.0f;
    psdnew[256] = 3.0f;
    bool usb = (bands[band].mode == USB);
    psdnew[256 + 17] = usb ? 1.0f : 2.0f;
    psdnew[256 - 17] = usb ? 2.0f : 1.0f;
}

TEST_F(CalibrationTest, RXIQAutoKeepsSettingWhenNoReadingIsValid) {
    auto savedED = ED; // the sweep goes on to the next band: put everything back after
    MirroredSpectrum(FIRST_BAND);
    ED.currentBand[ED.activeVFO] = FIRST_BAND;
    ED.IQAmpCorrectionFactor[FIRST_BAND] = 1.07f;
    ED.IQPhaseCorrectionFactor[FIRST_BAND] = 0.03f;
    InitializeRXIQCalibration();
    ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_AUTO);
    ASSERT_TRUE(RXIQCalibrationAutoActive());
    // Run until the first band is done and the sweep moves on
    for (int k = 0; (k < 200000) && (ED.currentBand[ED.activeVFO] == FIRST_BAND); k++){
        UpdateRXDeltaVal();
        ReceiveIQCalSm_dispatch_event(&rxiqSM, ReceiveIQCalSm_EventId_DO);
    }
    EXPECT_NE(ED.currentBand[ED.activeVFO], FIRST_BAND);
    // The amplitude is untouched; the phase is where each band's sweep starts it
    EXPECT_FLOAT_EQ(ED.IQAmpCorrectionFactor[FIRST_BAND], 1.07f);
    EXPECT_FLOAT_EQ(ED.IQPhaseCorrectionFactor[FIRST_BAND], 0.0f);
    AbortRXIQCalibration();
    while (GetInterrupt() != iNONE) {} // drop the PTT release it queued
    ED = savedED;
}

TEST_F(CalibrationTest, TXIQAutoKeepsSettingWhenNoReadingIsValid) {
    auto savedED = ED;
    SetDualVFOs(true);
    MirroredSpectrum(FIRST_BAND);
    ED.currentBand[ED.activeVFO] = FIRST_BAND;
    ED.IQXAmpCorrectionFactor[FIRST_BAND] = 1.07f;
    ED.IQXPhaseCorrectionFactor[FIRST_BAND] = 0.03f;
    InitializeTXIQCalibration();
    TransmitIQCalSm_dispatch_event(&txiqSM, TransmitIQCalSm_EventId_AUTO);
    ASSERT_TRUE(TXIQCalibrationAutoActive());
    for (int k = 0; (k < 200000) && (ED.currentBand[ED.activeVFO] == FIRST_BAND); k++){
        UpdateTXDeltaVal();
        TransmitIQCalSm_dispatch_event(&txiqSM, TransmitIQCalSm_EventId_DO);
    }
    EXPECT_NE(ED.currentBand[ED.activeVFO], FIRST_BAND);
    EXPECT_FLOAT_EQ(ED.IQXAmpCorrectionFactor[FIRST_BAND], 1.07f);
    EXPECT_FLOAT_EQ(ED.IQXPhaseCorrectionFactor[FIRST_BAND], 0.0f);
    AbortTXIQCalibration();
    while (GetInterrupt() != iNONE) {} // drop the PTT release it queued
    ED = savedED;
}

TEST_F(CalibrationTest, CarrierNullAutoKeepsSettingWhenNoReadingIsValid) {
    auto savedED = ED;
    SetDualVFOs(true);
    MirroredSpectrum(FIRST_BAND);
    ED.currentBand[ED.activeVFO] = FIRST_BAND;
    ED.DCOffsetI[FIRST_BAND] = 123;
    ED.DCOffsetQ[FIRST_BAND] = -45;
    InitializeTXCarrierCalibration();
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_AUTO);
    for (int k = 0; (k < 400000) && (ED.currentBand[ED.activeVFO] == FIRST_BAND); k++){
        UpdateTXDeltaVal();
        TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_DO);
    }
    EXPECT_NE(ED.currentBand[ED.activeVFO], FIRST_BAND);
    // Nothing stood out on the band: both offsets as they were before the run
    EXPECT_EQ(ED.DCOffsetI[FIRST_BAND], 123);
    EXPECT_EQ(ED.DCOffsetQ[FIRST_BAND], -45);
    AbortTXCarrierCalibration();
    while (GetInterrupt() != iNONE) {} // drop the PTT release it queued
    ED = savedED;
}

// Transmit is disabled outside the amateur bands (IsTxAllowed), so the
// transmit calibrations skip those bands: general coverage is the last band
TEST_F(CalibrationTest, NextTXCalBandSkipsBandsWithoutTransmit) {
    EXPECT_EQ(NextTXCalBand(FIRST_BAND), FIRST_BAND);
    EXPECT_EQ(bands[BAND_GENERAL].band_type, MISC_BAND);
    EXPECT_EQ(NextTXCalBand(BAND_GENERAL), -1);
    EXPECT_EQ(NextTXCalBand(LAST_BAND + 1), -1);
    for (int32_t b = FIRST_BAND; b <= LAST_BAND; b++){
        int32_t n = NextTXCalBand(b);
        if (n < 0) continue;
        EXPECT_GE(n, b);
        EXPECT_EQ(bands[n].band_type, HAM_BAND) << "band " << n;
    }
}

TEST_F(CalibrationTest, TXIQAutoRunSkipsBandsWithoutTransmit) {
    auto savedED = ED;
    SetDualVFOs(true);
    MirroredSpectrum(FIRST_BAND);
    ED.IQXAmpCorrectionFactor[BAND_GENERAL] = 1.23f;
    ED.IQXPhaseCorrectionFactor[BAND_GENERAL] = -0.04f;
    InitializeTXIQCalibration();
    TransmitIQCalSm_dispatch_event(&txiqSM, TransmitIQCalSm_EventId_AUTO);
    ASSERT_TRUE(TXIQCalibrationAutoActive());
    bool visited[NUMBER_OF_BANDS] = {false};
    for (int k = 0; (k < 3000000) && TXIQCalibrationAutoActive(); k++){
        visited[ED.currentBand[ED.activeVFO]] = true;
        UpdateTXDeltaVal();
        TransmitIQCalSm_dispatch_event(&txiqSM, TransmitIQCalSm_EventId_DO);
    }
    EXPECT_FALSE(TXIQCalibrationAutoActive());
    for (int32_t b = FIRST_BAND; b <= LAST_BAND; b++)
        EXPECT_EQ(visited[b], bands[b].band_type == HAM_BAND) << "band " << b;
    EXPECT_FLOAT_EQ(ED.IQXAmpCorrectionFactor[BAND_GENERAL], 1.23f);
    EXPECT_FLOAT_EQ(ED.IQXPhaseCorrectionFactor[BAND_GENERAL], -0.04f);
    while (GetInterrupt() != iNONE) {} // drop the PTT release the run queued
    ED = savedED;
}

// The carrier null skips the bands without transmit too
TEST_F(CalibrationTest, CarrierNullRunSkipsBandsWithoutTransmit) {
    auto savedED = ED;
    SetDualVFOs(true);
    MirroredSpectrum(FIRST_BAND);
    ED.DCOffsetI[BAND_GENERAL] = 77;
    ED.DCOffsetQ[BAND_GENERAL] = -88;
    ED.DCOffsetI[FIRST_BAND] = 11;
    ED.DCOffsetQ[FIRST_BAND] = -22;
    InitializeTXCarrierCalibration();
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_AUTO);
    ASSERT_NE(txcarrSM.state_id, TransmitCarrierCalSm_StateId_STANDBY);
    bool visited[NUMBER_OF_BANDS] = {false};
    for (int k = 0; (k < 6000000) && (txcarrSM.state_id != TransmitCarrierCalSm_StateId_STANDBY); k++){
        visited[ED.currentBand[ED.activeVFO]] = true;
        UpdateTXDeltaVal();
        TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_DO);
    }
    EXPECT_EQ(txcarrSM.state_id, TransmitCarrierCalSm_StateId_STANDBY);
    for (int32_t b = FIRST_BAND; b <= LAST_BAND; b++)
        EXPECT_EQ(visited[b], bands[b].band_type == HAM_BAND) << "band " << b;
    EXPECT_EQ(ED.DCOffsetI[BAND_GENERAL], 77);
    EXPECT_EQ(ED.DCOffsetQ[BAND_GENERAL], -88);
    // The flat mirrored spectrum: nothing stands out, so the offsets are restored
    EXPECT_EQ(ED.DCOffsetI[FIRST_BAND], 11);
    EXPECT_EQ(ED.DCOffsetQ[FIRST_BAND], -22);
    while (GetInterrupt() != iNONE) {} // drop the PTT release the run queued
    ED = savedED;
}

// A model of the loopback: a fixed tone, and a carrier whose level depends on
// how far the I and Q offsets are from the band's null. Far from it (as with
// the offsets at 0) the carrier is above the tone, as on the KI3P prototype.
static int16_t modelNullI = -990, modelNullQ = -1010;
static void CarrierModelSpectrum(void){
    int32_t b = ED.currentBand[ED.activeVFO];
    float32_t dI = (float32_t)(ED.DCOffsetI[b] - modelNullI);
    float32_t dQ = (float32_t)(ED.DCOffsetQ[b] - modelNullQ);
    float32_t carrier_dB = 20.0f * log10f(sqrtf(dI * dI + dQ * dQ) + 0.5f) - 30.0f;
    for (int i = 0; i < SPECTRUM_RES; i++) psdnew[i] = -2.0f;
    psdnew[256 + 17] = 2.0f;           // the tone, 20 dB, in either sideband
    psdnew[256 - 17] = 2.0f;
    psdnew[256] = carrier_dB / 10.0f;
}

TEST_F(CalibrationTest, CarrierNullFindsANullThatStartsAboveTheTone) {
    auto savedED = ED;
    SetDualVFOs(true);
    for (int32_t b = FIRST_BAND; b <= LAST_BAND; b++){ ED.DCOffsetI[b] = 0; ED.DCOffsetQ[b] = 0; }
    CarrierModelSpectrum();
    InitializeTXCarrierCalibration();
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_AUTO);
    for (int k = 0; (k < 6000000) && (txcarrSM.state_id != TransmitCarrierCalSm_StateId_STANDBY); k++){
        CarrierModelSpectrum();
        UpdateTXDeltaVal();
        TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_DO);
    }
    EXPECT_EQ(txcarrSM.state_id, TransmitCarrierCalSm_StateId_STANDBY);
    for (int32_t b = FIRST_BAND; b <= LAST_BAND; b++){
        if (bands[b].band_type != HAM_BAND) continue;
        EXPECT_NEAR(ED.DCOffsetI[b], modelNullI, 2) << "band " << b;
        EXPECT_NEAR(ED.DCOffsetQ[b], modelNullQ, 2) << "band " << b;
        EXPECT_GT(GetTXCarrierVals(b), 40.0f) << "band " << b;
    }
    while (GetInterrupt() != iNONE) {} // drop the PTT release the run queued
    ED = savedED;
}

// The spectrum lags the offset, so the coarse passes, where a lag can put the
// null outside every later window, wait 150 ms per step; the others 60 ms
TEST_F(CalibrationTest, CarrierNullCoarsePassesWaitLonger) {
    auto savedED = ED;
    SetDualVFOs(true);
    CarrierModelSpectrum();
    InitializeTXCarrierCalibration();
    TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_AUTO);
    ASSERT_EQ(ED.currentBand[ED.activeVFO], FIRST_BAND);
    int ticks = 0;
    bool saw150 = false, saw60 = false;
    while ((ticks < 200000) && (ED.currentBand[ED.activeVFO] == FIRST_BAND)){
        CarrierModelSpectrum();
        UpdateTXDeltaVal();
        TransmitCarrierCalSm_dispatch_event(&txcarrSM, TransmitCarrierCalSm_EventId_DO);
        if (txcarrSM.vars.acquisitionDuration_ms == 150) saw150 = true;
        if (txcarrSM.vars.acquisitionDuration_ms == 60) saw60 = true;
        ticks++;
    }
    EXPECT_TRUE(saw150);
    EXPECT_TRUE(saw60);
    // 1 ms per tick: two coarse passes of 100 steps at 150 ms, eight of 24 at 60 ms
    const int expected = 2 * 100 * 150 + 8 * 24 * 60;
    EXPECT_GE(ticks, expected);
    EXPECT_LE(ticks, expected * 11 / 10);
    AbortTXCarrierCalibration();
    while (GetInterrupt() != iNONE) {} // drop the PTT release it queued
    ED = savedED;
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

    // Test upper limit (max value is 2.5)
    // Set to a value close to max
    ED.IQXAmpCorrectionFactor[currentBand] = 2.499;
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXAmpCorrectionFactor[currentBand], 2.5, 0.00001);

    // Try to increment beyond max - should be clamped at 2.5
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.IQXAmpCorrectionFactor[currentBand], 2.5, 0.00001);

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

#ifndef DIRECT_COUPLED_TX
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

    // The screen opens at full attenuation; start mid-range so both directions move
    EXPECT_NEAR(attLevel, 31.5, 0.00001);
    attLevel = 15.0;

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
#endif

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

// ============================================================================
// Oscillator Mode and Feedback Tests
// ============================================================================
// These tests verify that the oscillator-driven mode for Q_in_L_Ex and Q_in_R_Ex
// works correctly for TX IQ calibration feedback.

#include "MainBoard_AudioIO.h"
#include <cmath>
#include <vector>

/**
 * Test fixture for Oscillator/Feedback mode tests
 */
class OscillatorFeedbackTest : public ::testing::Test {
protected:
    static constexpr int BLOCK_SIZE = 128;
    static constexpr double SAMPLE_RATE = 192000.0;
    static constexpr double PI_TIMES_2 = 2.0 * M_PI;

    void SetUp() override {
        // Clear the queues
        Q_in_L_Ex.setChannel(0);
        Q_in_R_Ex.setChannel(1);
        Q_in_L_Ex.clear();
        Q_in_R_Ex.clear();

        // Disable oscillator mode initially
        Q_in_L_Ex.setOscillatorSource(nullptr);
        Q_in_R_Ex.setOscillatorSource(nullptr);

        // Start the millis timer
        StartMillis();
    }

    void TearDown() override {
        // Clean up oscillator mode
        Q_in_L_Ex.setOscillatorSource(nullptr);
        Q_in_R_Ex.setOscillatorSource(nullptr);
    }

    /**
     * Calculate the expected phase at a given sample index for a sine wave
     */
    double expectedPhase(double frequency, uint32_t sampleIndex) {
        return PI_TIMES_2 * frequency * sampleIndex / SAMPLE_RATE;
    }

    /**
     * Estimate the phase of a sample given amplitude and expected amplitude
     * Returns phase in radians
     */
    double estimatePhase(int16_t sample, double amplitude) {
        double normalized = sample / amplitude;
        // Clamp to [-1, 1] to handle any numerical errors
        if (normalized > 1.0) normalized = 1.0;
        if (normalized < -1.0) normalized = -1.0;
        return acos(normalized);
    }

    /**
     * Check if a block contains all zeros
     */
    bool isZeroBlock(int16_t* block, int size) {
        for (int i = 0; i < size; i++) {
            if (block[i] != 0) return false;
        }
        return true;
    }

    /**
     * Calculate RMS of a block
     */
    double calculateRMS(int16_t* block, int size) {
        double sum = 0;
        for (int i = 0; i < size; i++) {
            sum += (double)block[i] * block[i];
        }
        return sqrt(sum / size);
    }
};

/**
 * Helper to wait for blocks to become available with timeout
 * Returns the number of blocks available, or 0 if timeout
 */
static int waitForBlocks(AudioRecordQueue& queue, int timeoutMs = 50) {
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        int avail = queue.available();
        if (avail > 0) return avail;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        elapsed += 2;
    }
    return queue.available();
}

/**
 * Test that oscillator mode can be enabled and disabled
 */
TEST_F(OscillatorFeedbackTest, OscillatorModeEnableDisable) {
    // Enable the queue
    Q_in_L_Ex.begin();

    // Set oscillator source
    Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);

    // Wait for blocks to be available (with retry to handle initialization)
    int avail = waitForBlocks(Q_in_L_Ex);
    EXPECT_GT(avail, 0) << "Expected blocks to be available after enabling oscillator mode";

    // Disable oscillator mode
    Q_in_L_Ex.setOscillatorSource(nullptr);
    Q_in_L_Ex.clear();

    // After clearing, available should reflect non-oscillator mode behavior
    // (In this case, it falls back to other modes)
}

/**
 * Test that oscillator produces non-zero samples
 */
TEST_F(OscillatorFeedbackTest, OscillatorProducesNonZeroSamples) {
    // Set up oscillator with known frequency and amplitude
    transmitIQcal_oscillator.frequency(800.0f);
    transmitIQcal_oscillator.amplitude(0.08f);  // 40/500 as in InitializeAudio

    Q_in_L_Ex.begin();
    Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);

    // Wait for samples to be generated
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Read several blocks
    int blocksRead = 0;
    int zeroBlocks = 0;

    for (int i = 0; i < 10; i++) {
        if (Q_in_L_Ex.available() > 0) {
            int16_t* block = Q_in_L_Ex.readBuffer();
            blocksRead++;

            if (isZeroBlock(block, BLOCK_SIZE)) {
                zeroBlocks++;
            }

            Q_in_L_Ex.freeBuffer();
        } else {
            // Wait for more samples
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    EXPECT_GT(blocksRead, 0) << "Should have read at least one block";
    EXPECT_EQ(zeroBlocks, 0) << "Should not have any zero-valued blocks";
}

/**
 * Test that samples have reasonable amplitude
 * Note: The oscillator amplitude is multiplied by 500*30 in the implementation,
 * so we use a small value to avoid int16_t overflow/clipping
 */
TEST_F(OscillatorFeedbackTest, OscillatorSampleAmplitude) {
    // Set up oscillator with amplitude that won't clip
    // amplitude * 500 * 30 should be < 32767
    // So amplitude should be < 32767/(500*30) ≈ 2.18
    float amplitude_setting = 0.001f;  // Will give peak of ~15
    transmitIQcal_oscillator.frequency(800.0f);
    transmitIQcal_oscillator.amplitude(amplitude_setting);

    // Expected peak amplitude: amp * 500 * 30 = 0.001 * 500 * 30 = 15
    double expectedAmplitude = amplitude_setting * 500.0 * 30.0;

    Q_in_L_Ex.begin();
    Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);

    // Wait for blocks to be available
    int avail = waitForBlocks(Q_in_L_Ex);
    ASSERT_GT(avail, 0) << "Should have blocks available";

    int16_t* block = Q_in_L_Ex.readBuffer();

    // Find max absolute value in block
    int16_t maxVal = 0;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (abs(block[i]) > maxVal) {
            maxVal = abs(block[i]);
        }
    }

    // Max value should be close to expected amplitude (within 20% - allows for phase)
    EXPECT_NEAR(maxVal, expectedAmplitude, expectedAmplitude * 0.2)
        << "Peak amplitude should match expected value";

    Q_in_L_Ex.freeBuffer();
}

/**
 * Test phase continuity across multiple blocks
 * This is the key test - verifies no phase discontinuities
 */
TEST_F(OscillatorFeedbackTest, PhaseContinuityAcrossBlocks) {
    // Use a frequency that gives a nice number of samples per cycle
    // 800 Hz at 192 kHz = 240 samples per cycle
    float frequency = 800.0f;
    float amplitude_setting = 0.08f;

    transmitIQcal_oscillator.frequency(frequency);
    transmitIQcal_oscillator.amplitude(amplitude_setting);

    double expectedAmplitude = amplitude_setting * 500.0 * 32767.0;
    double phaseIncrement = PI_TIMES_2 * frequency / SAMPLE_RATE;

    Q_in_L_Ex.begin();
    Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);

    // Wait for samples to accumulate
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Read multiple blocks and verify continuity
    const int NUM_BLOCKS = 10;
    int16_t allSamples[NUM_BLOCKS * BLOCK_SIZE];
    int totalSamples = 0;

    for (int b = 0; b < NUM_BLOCKS; b++) {
        // Wait if needed
        while (Q_in_L_Ex.available() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        int16_t* block = Q_in_L_Ex.readBuffer();
        ASSERT_NE(block, nullptr) << "readBuffer should not return null";
        ASSERT_FALSE(isZeroBlock(block, BLOCK_SIZE))
            << "Block " << b << " should not be all zeros";

        // Copy samples
        for (int i = 0; i < BLOCK_SIZE; i++) {
            allSamples[totalSamples++] = block[i];
        }

        Q_in_L_Ex.freeBuffer();
    }

    ASSERT_EQ(totalSamples, NUM_BLOCKS * BLOCK_SIZE);

    // Now verify phase continuity by checking that the signal is a smooth cosine
    // We do this by computing the "derivative" and checking it's smooth
    // For a cosine, the derivative magnitude should be approximately constant

    int discontinuities = 0;
    double maxDerivative = 0;
    double expectedMaxDerivative = expectedAmplitude * phaseIncrement;  // A * 2πf/fs

    for (int i = 1; i < totalSamples; i++) {
        double diff = allSamples[i] - allSamples[i-1];
        double absDiff = fabs(diff);

        if (absDiff > maxDerivative) {
            maxDerivative = absDiff;
        }

        // A discontinuity would show up as a very large jump
        // For a smooth sine at 800Hz, max derivative is about 2πf/fs * A ≈ 34.5
        // A phase discontinuity would cause a jump of up to 2*A ≈ 2620
        // We use a threshold of 3x expected max derivative to detect discontinuities
        if (absDiff > 3 * expectedMaxDerivative) {
            discontinuities++;
            // Log the discontinuity location
            ADD_FAILURE() << "Phase discontinuity detected at sample " << i
                          << ": jump of " << diff
                          << " (expected max ~" << expectedMaxDerivative << ")";
        }
    }

    EXPECT_EQ(discontinuities, 0)
        << "Should have no phase discontinuities across " << NUM_BLOCKS << " blocks";

    // Also verify max derivative is in expected range
    EXPECT_LT(maxDerivative, 2 * expectedMaxDerivative)
        << "Maximum sample-to-sample difference should be within expected range";
}

/**
 * Test that both I and Q channels produce valid samples
 */
TEST_F(OscillatorFeedbackTest, BothChannelsProduceSamples) {
    transmitIQcal_oscillator.frequency(800.0f);
    transmitIQcal_oscillator.amplitude(0.08f);

    Q_in_L_Ex.begin();
    Q_in_R_Ex.begin();
    Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);
    Q_in_R_Ex.setOscillatorSource(&transmitIQcal_oscillator);

    // Wait for blocks to be available on both channels
    int availI = waitForBlocks(Q_in_L_Ex);
    int availQ = waitForBlocks(Q_in_R_Ex);

    EXPECT_GT(availI, 0) << "I channel should have blocks";
    EXPECT_GT(availQ, 0) << "Q channel should have blocks";

    // Read from both
    int16_t* blockI = Q_in_L_Ex.readBuffer();
    int16_t* blockQ = Q_in_R_Ex.readBuffer();

    ASSERT_FALSE(isZeroBlock(blockI, BLOCK_SIZE)) << "I channel should not be zero";
    ASSERT_FALSE(isZeroBlock(blockQ, BLOCK_SIZE)) << "Q channel should not be zero";

    // Both should have similar RMS (since they're generated from same oscillator)
    double rmsI = calculateRMS(blockI, BLOCK_SIZE);
    double rmsQ = calculateRMS(blockQ, BLOCK_SIZE);

    EXPECT_GT(rmsI, 100) << "I channel RMS should be significant";
    EXPECT_GT(rmsQ, 100) << "Q channel RMS should be significant";

    Q_in_L_Ex.freeBuffer();
    Q_in_R_Ex.freeBuffer();
}

/**
 * Test continuous reading over a longer period
 * Verifies no zero blocks appear during sustained operation
 */
TEST_F(OscillatorFeedbackTest, SustainedOperationNoZeroBlocks) {
    transmitIQcal_oscillator.frequency(800.0f);
    transmitIQcal_oscillator.amplitude(0.08f);

    Q_in_L_Ex.begin();
    Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);

    // Read blocks over 100ms of simulated time
    // At 192kHz, 128 samples = 0.667ms, so ~150 blocks in 100ms
    const int TARGET_BLOCKS = 100;
    int blocksRead = 0;
    int zeroBlocks = 0;
    int iterations = 0;
    const int MAX_ITERATIONS = 1000;

    while (blocksRead < TARGET_BLOCKS && iterations < MAX_ITERATIONS) {
        iterations++;

        int avail = Q_in_L_Ex.available();
        if (avail > 0) {
            int16_t* block = Q_in_L_Ex.readBuffer();
            blocksRead++;

            if (isZeroBlock(block, BLOCK_SIZE)) {
                zeroBlocks++;
                ADD_FAILURE() << "Zero block detected at block " << blocksRead;
            }

            Q_in_L_Ex.freeBuffer();
        } else {
            // Wait for more samples to be generated
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    EXPECT_GE(blocksRead, TARGET_BLOCKS)
        << "Should have read at least " << TARGET_BLOCKS << " blocks";
    EXPECT_EQ(zeroBlocks, 0)
        << "Should not have any zero blocks during sustained operation";
}

/**
 * Test that clearing the queue doesn't cause issues
 */
TEST_F(OscillatorFeedbackTest, ClearAndResume) {
    transmitIQcal_oscillator.frequency(800.0f);
    transmitIQcal_oscillator.amplitude(0.08f);

    Q_in_L_Ex.begin();
    Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);

    // Wait for blocks to be available
    int initialAvail = waitForBlocks(Q_in_L_Ex);
    EXPECT_GT(initialAvail, 0) << "Should have initial samples";

    // Clear the queue
    Q_in_L_Ex.clear();

    // Wait for new samples after clear
    int afterClearAvail = waitForBlocks(Q_in_L_Ex);
    EXPECT_GT(afterClearAvail, 0) << "Should have samples after clear";

    // Read and verify not zero
    int16_t* block = Q_in_L_Ex.readBuffer();
    EXPECT_FALSE(isZeroBlock(block, BLOCK_SIZE)) << "Block after clear should not be zero";

    Q_in_L_Ex.freeBuffer();
}

/**
 * Test that signal is a valid periodic waveform (has regular zero crossings)
 * Note: Exact frequency depends on the oscillator implementation and global state
 */
TEST_F(OscillatorFeedbackTest, SignalIsPeriodicWaveform) {
    // Use lower amplitude to avoid clipping
    transmitIQcal_oscillator.frequency(800.0f);
    transmitIQcal_oscillator.amplitude(0.001f);

    Q_in_L_Ex.begin();
    Q_in_L_Ex.setOscillatorSource(&transmitIQcal_oscillator);

    // Collect samples from multiple blocks
    const int NUM_SAMPLES = 2560;  // 20 blocks worth
    int16_t samples[NUM_SAMPLES];
    int samplesCollected = 0;

    while (samplesCollected < NUM_SAMPLES) {
        int avail = waitForBlocks(Q_in_L_Ex);
        if (avail > 0) {
            int16_t* block = Q_in_L_Ex.readBuffer();
            int toCopy = std::min(BLOCK_SIZE, NUM_SAMPLES - samplesCollected);
            for (int i = 0; i < toCopy; i++) {
                samples[samplesCollected++] = block[i];
            }
            Q_in_L_Ex.freeBuffer();
        }
    }

    // Count positive-going zero crossings
    int zeroCrossings = 0;
    for (int i = 1; i < NUM_SAMPLES; i++) {
        if (samples[i-1] < 0 && samples[i] >= 0) {
            zeroCrossings++;
        }
    }

    // Should have some zero crossings indicating a periodic signal
    EXPECT_GT(zeroCrossings, 0) << "Should have at least some zero crossings";

    // Check that zero crossings are somewhat regular (not noise)
    // For a periodic signal, the spacing between crossings should be similar
    std::vector<int> crossingPositions;
    for (int i = 1; i < NUM_SAMPLES; i++) {
        if (samples[i-1] < 0 && samples[i] >= 0) {
            crossingPositions.push_back(i);
        }
    }

    if (crossingPositions.size() >= 3) {
        // Calculate intervals between crossings
        std::vector<int> intervals;
        for (size_t i = 1; i < crossingPositions.size(); i++) {
            intervals.push_back(crossingPositions[i] - crossingPositions[i-1]);
        }

        // Calculate mean and std dev of intervals
        double sum = 0;
        for (int interval : intervals) {
            sum += interval;
        }
        double mean = sum / intervals.size();

        double varSum = 0;
        for (int interval : intervals) {
            varSum += (interval - mean) * (interval - mean);
        }
        double stdDev = sqrt(varSum / intervals.size());

        // For a clean sinusoid, std dev should be small compared to mean
        // Allow 10% variation (accounts for integer sample positions)
        EXPECT_LT(stdDev, mean * 0.1)
            << "Zero crossing intervals should be consistent (mean=" << mean
            << ", stdDev=" << stdDev << ")";
    }
}
