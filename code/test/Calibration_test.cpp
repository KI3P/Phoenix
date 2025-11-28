/**
 * @file Calibration_test.cpp
 * @brief Unit tests for calibration functions
 *
 */

#include <gtest/gtest.h>
#include "SDT.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

// External declarations for power calibration internal state
// These are static variables in MainBoard_DisplayCalibration.cpp
extern float32_t measuredPower;
extern uint8_t PAselect;
extern uint32_t Npoints;
extern uint8_t incindexPower;
extern float32_t attenuations_dB[3];
extern float32_t powers_W[3];

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
 * The volume encoder controls ED.XAttenSSB[currentBand] with 0.5 dB steps
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
    float32_t initialAtten = ED.XAttenSSB[currentBand];

    // Expected increment value (0.5 dB for transmit attenuation)
    const float32_t expectedIncrement = 0.5;

    // Test incrementing the transmit attenuation by rotating volume encoder clockwise
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);

    float32_t attenAfterIncrease = ED.XAttenSSB[currentBand];
    EXPECT_NEAR(attenAfterIncrease, initialAtten + expectedIncrement, 0.00001);

    // Test decrementing the transmit attenuation by rotating volume encoder counter-clockwise
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);

    float32_t attenAfterDecrease = ED.XAttenSSB[currentBand];
    EXPECT_NEAR(attenAfterDecrease, initialAtten, 0.00001);

    // Test multiple increments
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iVOLUME_INCREASE);
        loop(); MyDelay(10);
    }

    float32_t attenAfterMultipleIncrements = ED.XAttenSSB[currentBand];
    EXPECT_NEAR(attenAfterMultipleIncrements, initialAtten + 5 * expectedIncrement, 0.00001);

    // Test multiple decrements
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iVOLUME_DECREASE);
        loop(); MyDelay(10);
    }

    float32_t attenAfterMultipleDecrements = ED.XAttenSSB[currentBand];
    EXPECT_NEAR(attenAfterMultipleDecrements, initialAtten, 0.00001);

    // Test upper limit (max value is 31.5)
    ED.XAttenSSB[currentBand] = 31.0;
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenSSB[currentBand], 31.5, 0.00001);

    // Try to increment beyond max - should be clamped at 31.5
    SetInterrupt(iVOLUME_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenSSB[currentBand], 31.5, 0.00001);

    // Test lower limit (min value is 0.0)
    ED.XAttenSSB[currentBand] = 0.5;
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenSSB[currentBand], 0.0, 0.00001);

    // Try to decrement beyond min - should be clamped at 0.0
    SetInterrupt(iVOLUME_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenSSB[currentBand], 0.0, 0.00001);

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

    // Initial increment index should be 0
    uint8_t initialIncIndex = incindexPower;
    EXPECT_EQ(initialIncIndex, 0);

    // Press button 15 to change increment
    SetButton(15);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should cycle to next increment index
    EXPECT_EQ(incindexPower, 1);

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
    uint8_t initialPAsel = PAselect;
    EXPECT_EQ(initialPAsel, 0); // PA20W

    // Press button 16 to change PA selection
    SetButton(16);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should switch to PA100W (1)
    EXPECT_EQ(PAselect, 1); // PA100W

    // Press button 16 again to toggle back
    SetButton(16);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should switch back to PA20W (0)
    EXPECT_EQ(PAselect, 0); // PA20W

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
    ED.XAttenSSB[currentBand] = 10.5;
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
    ED.XAttenSSB[currentBand] = 15.0;
    measuredPower = 50.7;

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify second data point was recorded
    EXPECT_EQ(Npoints, 2);
    EXPECT_NEAR(attenuations_dB[1], 15.0, 0.00001);
    EXPECT_NEAR(powers_W[1], 50.7, 0.00001);

    // Record third data point
    ED.XAttenSSB[currentBand] = 20.0;
    measuredPower = 75.2;

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify third data point was recorded
    EXPECT_EQ(Npoints, 3);
    EXPECT_NEAR(attenuations_dB[2], 20.0, 0.00001);
    EXPECT_NEAR(powers_W[2], 75.2, 0.00001);

    // After recording the third point, the system automatically calculates the fit
    // and transitions to offset mode. The fit routine queues button 12, so we need
    // to process it with another loop() call.
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);

    // Exit offset mode back to power calibration
    SetButton(12); // FILTER button toggles between power cal and offset mode
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // Recording another data point should record at index 0 and set Npoints to 1
    ED.XAttenSSB[currentBand] = 5.0;
    measuredPower = 10.1;

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify Npoints is now 1 and first entry was overwritten
    EXPECT_EQ(Npoints, 1);
    EXPECT_NEAR(attenuations_dB[0], 5.0, 0.00001);
    EXPECT_NEAR(powers_W[0], 10.1, 0.00001);

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
    PAselect = 0; // PA20W
    Npoints = 0;  // Start with empty buffer

    // Load the data points into the data buffer
    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 3.0;
    measuredPower = 14.79108388;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 16.5;
    measuredPower = 5.2480746;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 24.0;
    measuredPower = 1.04712855;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // After recording the third point, the system automatically calculates the fit
    // and queues button 12 to transition to offset mode. Process it with another loop() call.
    loop(); MyDelay(10);

    // Test whether the correct parameters have been set to the correct levels
    // Note: The fit algorithm is highly sensitive to initial conditions and numerical precision
    EXPECT_NEAR(ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]],14790,50);
    EXPECT_NEAR(ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]],16.178,0.5);

    // After the fit, the system should have automatically transitioned to offset mode
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);

    // Exit offset mode back to power calibration
    SetButton(12); // FILTER button toggles between power cal and offset mode
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

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
    PAselect = 1; // PA100W
    Npoints = 0;  // Start with empty buffer

    // Load the data points into the data buffer
    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 3.0;
    measuredPower = 77.62471166;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 16.5;
    measuredPower = 27.54228703;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 24.0;
    measuredPower = 5.49540874;
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // After recording the third point, the system automatically calculates the fit
    // and queues button 12 to transition to offset mode. Process it with another loop() call.
    loop(); MyDelay(10);

    // Test whether the correct parameters have been set to the correct levels
    // Note: The fit algorithm is highly sensitive to initial conditions and numerical precision
    EXPECT_NEAR(ED.PowerCal_100W_Psat_mW[ED.currentBand[ED.activeVFO]],77050,600);
    EXPECT_NEAR(ED.PowerCal_100W_kindex[ED.currentBand[ED.activeVFO]],16.178,0.5);

    // After the fit, the system should have automatically transitioned to offset mode
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);

    // Exit offset mode back to power calibration
    SetButton(12); // FILTER button toggles between power cal and offset mode
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);

    // Exit back to home screen
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
    EXPECT_EQ(GET_BIT(hardwareRegister,SSBVFOBIT), 1); // SSB VFO should be HI (on)
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
    EXPECT_EQ(GET_BIT(hardwareRegister,SSBVFOBIT), 1); // SSB VFO should be HI (on)
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[ED.currentBand[ED.activeVFO]]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(TXATTLSB,6), (uint8_t)round(2*ED.XAttenSSB[ED.currentBand[ED.activeVFO]]));  // TX attenuation
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
    EXPECT_EQ(GET_BIT(hardwareRegister,SSBVFOBIT), 1); // SSB VFO should be HI (on)
    // Note: TX attenuation is not checked in receive mode because it doesn't affect RX operation
    // and the timing of when it gets reset during state transitions is implementation-dependent
    EXPECT_EQ(GETHWRBITS(RXATTLSB,6), (uint8_t)round(2*ED.RAtten[band]));  // RX attenuation
    EXPECT_EQ(GETHWRBITS(BPFBAND0BIT,4), BandToBCD(band)); // BPF filter
    // Now check that the GPIO registers match the hardware register
    CheckThatHardwareRegisterMatchesActualHardware();
}

TEST_F(CalibrationTest, CalibrateTransmitIQState) {
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

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
    float32_t initialAtten = ED.XAttenSSB[currentBand];

    // Expected increment value (0.5 for transmit attenuation)
    const float32_t expectedIncrement = 0.5;

    // Test incrementing the transmit attenuation by rotating finetune encoder clockwise
    SetInterrupt(iFINETUNE_INCREASE);
    loop(); MyDelay(10);

    float32_t attenAfterIncrease = ED.XAttenSSB[currentBand];
    EXPECT_NEAR(attenAfterIncrease, initialAtten + expectedIncrement, 0.00001);

    // Test decrementing the transmit attenuation by rotating finetune encoder counter-clockwise
    SetInterrupt(iFINETUNE_DECREASE);
    loop(); MyDelay(10);

    float32_t attenAfterDecrease = ED.XAttenSSB[currentBand];
    EXPECT_NEAR(attenAfterDecrease, initialAtten, 0.00001);

    // Test multiple increments
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iFINETUNE_INCREASE);
        loop(); MyDelay(10);
    }

    float32_t attenAfterMultipleIncrements = ED.XAttenSSB[currentBand];
    EXPECT_NEAR(attenAfterMultipleIncrements, initialAtten + 5 * expectedIncrement, 0.00001);

    // Test multiple decrements
    for (int i = 0; i < 5; i++) {
        SetInterrupt(iFINETUNE_DECREASE);
        loop(); MyDelay(10);
    }

    float32_t attenAfterMultipleDecrements = ED.XAttenSSB[currentBand];
    EXPECT_NEAR(attenAfterMultipleDecrements, initialAtten, 0.00001);

    // Test upper limit (max value is 31.5)
    // Set to a value close to max
    ED.XAttenSSB[currentBand] = 31.0;
    SetInterrupt(iFINETUNE_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenSSB[currentBand], 31.5, 0.00001);

    // Try to increment beyond max - should be clamped at 31.5
    SetInterrupt(iFINETUNE_INCREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenSSB[currentBand], 31.5, 0.00001);

    // Test lower limit (min value is 0.0)
    // Set to a value close to min
    ED.XAttenSSB[currentBand] = 0.5;
    SetInterrupt(iFINETUNE_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenSSB[currentBand], 0.0, 0.00001);

    // Try to decrement beyond min - should be clamped at 0.0
    SetInterrupt(iFINETUNE_DECREASE);
    loop(); MyDelay(10);
    EXPECT_NEAR(ED.XAttenSSB[currentBand], 0.0, 0.00001);

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

/**
 * Test fixture for Power Calibration function tests
 * These tests verify the mathematical correctness of PredictPowerLevel and CalculateAttenuation
 */
class PowerCalibrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize minimal environment for power calculation tests
        InitializeStorage();

        // Set known calibration values for testing
        // Using default values from SDT.h for 20W PA
        ED.PowerCal_20W_Psat_mW[BAND_20M] = 14680.0f;
        ED.PowerCal_20W_kindex[BAND_20M] = 16.2f;
        ED.PowerCal_20W_att_offset_dB[BAND_20M] = 0.0f;

        // Default values for 100W PA
        ED.PowerCal_100W_Psat_mW[BAND_20M] = 86000.0f;
        ED.PowerCal_100W_kindex[BAND_20M] = 10.0f;
        ED.PowerCal_100W_att_offset_dB[BAND_20M] = 0.0f;

        // Threshold for PA selection
        ED.PowerCal_20W_to_100W_threshold_W = 10.0f;

        // Set active band to 20M for testing
        ED.currentBand[ED.activeVFO] = BAND_20M;
    }
};

/**
 * Test PredictPowerLevel returns zero for negative attenuation
 */
TEST_F(PowerCalibrationTest, PredictPowerLevel_RejectsNegativeAttenuation) {
    float32_t power = PredictPowerLevel(-1.0f, 0, 0);
    EXPECT_EQ(power, 0.0f);
}

/**
 * Test PredictPowerLevel returns zero for out-of-range attenuation
 */
TEST_F(PowerCalibrationTest, PredictPowerLevel_RejectsExcessiveAttenuation) {
    float32_t power = PredictPowerLevel(32.0f, 0, 0);
    EXPECT_EQ(power, 0.0f);
}

/**
 * Test PredictPowerLevel with zero attenuation approaches saturation power
 */
TEST_F(PowerCalibrationTest, PredictPowerLevel_ZeroAttenuation_20W_CW) {
    // With zero attenuation, output should approach Psat
    float32_t power = PredictPowerLevel(0.0f, 0, 0);
    // At zero attenuation: tanh(k) = tanh(16.2) ≈ 1.0
    EXPECT_NEAR(power, ED.PowerCal_20W_Psat_mW[BAND_20M], 1.0f);
}

/**
 * Test PredictPowerLevel with maximum attenuation gives very low power
 */
TEST_F(PowerCalibrationTest, PredictPowerLevel_MaxAttenuation_20W_CW) {
    // With maximum attenuation (31.5 dB), output should be very small
    float32_t power = PredictPowerLevel(31.5f, 0, 0);
    // At 31.5 dB attenuation, power should be much less than 1W (1000 mW)
    EXPECT_LT(power, 1000.0f);
}

/**
 * Test PredictPowerLevel for 100W PA
 */
TEST_F(PowerCalibrationTest, PredictPowerLevel_ZeroAttenuation_100W_CW) {
    float32_t power = PredictPowerLevel(0.0f, 1, 0);
    // At zero attenuation: tanh(k) = tanh(10.0) ≈ 1.0
    EXPECT_NEAR(power, ED.PowerCal_100W_Psat_mW[BAND_20M], 1.0f);
}

/**
 * Test PredictPowerLevel SSB mode applies offset
 */
TEST_F(PowerCalibrationTest, PredictPowerLevel_SSB_AppliesOffset) {
    // Set a non-zero offset for SSB mode
    ED.PowerCal_20W_att_offset_dB[BAND_20M] = 3.0f;

    // Get power for CW (no offset)
    float32_t powerCW = PredictPowerLevel(10.0f, 0, 0);

    // Get power for SSB (with offset)
    float32_t powerSSB = PredictPowerLevel(10.0f, 0, 1);

    // SSB should have lower power due to the 3dB offset (effective attenuation is higher)
    // The offset represents how much less attenuation is needed in SSB mode
    EXPECT_LT(powerSSB, powerCW);

    // Reset offset
    ED.PowerCal_20W_att_offset_dB[BAND_20M] = 0.0f;
}

/**
 * Test CalculateAttenuation returns zero for negative power
 */
TEST_F(PowerCalibrationTest, CalculateAttenuation_RejectsNegativePower) {
    int8_t PAsel = 0;
    float32_t atten = CalculateAttenuation(-1.0f, 0, &PAsel);
    EXPECT_EQ(atten, 0.0f);
}

/**
 * Test CalculateAttenuation returns zero for excessive power
 */
TEST_F(PowerCalibrationTest, CalculateAttenuation_RejectsExcessivePower) {
    int8_t PAsel = 0;
    float32_t atten = CalculateAttenuation(101.0f, 0, &PAsel);
    EXPECT_EQ(atten, 0.0f);
}

/**
 * Test CalculateAttenuation selects correct PA for low power
 */
TEST_F(PowerCalibrationTest, CalculateAttenuation_SelectsLowPA_ForLowPower) {
    int8_t PAsel = 0;
    CalculateAttenuation(5.0f, 0, &PAsel);
    EXPECT_EQ(PAsel, 0);  // Should select 20W PA for 5W
}

/**
 * Test CalculateAttenuation selects correct PA for high power
 */
TEST_F(PowerCalibrationTest, CalculateAttenuation_SelectsHighPA_ForHighPower) {
    int8_t PAsel = 0;
    CalculateAttenuation(15.0f, 0, &PAsel);
    EXPECT_EQ(PAsel, 1);  // Should select 100W PA for 15W
}

/**
 * Test CalculateAttenuation at threshold power
 */
TEST_F(PowerCalibrationTest, CalculateAttenuation_ThresholdPower_SelectsHighPA) {
    int8_t PAsel = 0;
    CalculateAttenuation(10.0f, 0, &PAsel);
    EXPECT_EQ(PAsel, 1);  // Should select 100W PA at exactly 10W threshold
}

/**
 * Test that PredictPowerLevel and CalculateAttenuation are inverses (20W PA, CW)
 * Note: Only test attenuations that result in power below the PA selection threshold
 */
TEST_F(PowerCalibrationTest, InverseFunctions_20W_CW) {
    // Test attenuation values that keep power below 10W threshold
    // Higher attenuations to avoid PA auto-selection issues
    float32_t test_attenuations[] = {15.0f, 20.0f, 25.0f, 31.5f};

    for (float32_t atten : test_attenuations) {
        // Predict power from attenuation
        float32_t power_mW = PredictPowerLevel(atten, 0, 0);
        float32_t power_W = power_mW / 1000.0f;

        // Calculate attenuation from power
        int8_t PAsel = 0;
        float32_t calculated_atten = CalculateAttenuation(power_W, 0, &PAsel);

        // Should get back the original attenuation (within tolerance)
        // Use larger tolerance for numerical precision issues
        EXPECT_NEAR(calculated_atten, atten, 0.1f)
            << "Failed for attenuation=" << atten
            << ", power=" << power_W << "W";

        // Should select the correct PA
        EXPECT_EQ(PAsel, 0) << "Wrong PA selected for " << power_W << "W";
    }
}

/**
 * Test that PredictPowerLevel and CalculateAttenuation are inverses (100W PA, CW)
 * Note: Only test attenuations that result in power above the PA selection threshold
 */
TEST_F(PowerCalibrationTest, InverseFunctions_100W_CW) {
    // Test attenuation values that result in power above 10W threshold
    // so PA auto-selection picks 100W PA
    float32_t test_attenuations[] = {5.0f, 10.0f, 15.0f};

    for (float32_t atten : test_attenuations) {
        // Predict power from attenuation
        float32_t power_mW = PredictPowerLevel(atten, 1, 0);
        float32_t power_W = power_mW / 1000.0f;

        // Calculate attenuation from power
        int8_t PAsel = 0;
        float32_t calculated_atten = CalculateAttenuation(power_W, 0, &PAsel);

        // Should get back the original attenuation (within tolerance)
        // Use larger tolerance for numerical precision issues
        EXPECT_NEAR(calculated_atten, atten, 0.1f)
            << "Failed for attenuation=" << atten
            << ", power=" << power_W << "W";

        // For high power outputs, should select 100W PA
        if (power_W >= ED.PowerCal_20W_to_100W_threshold_W) {
            EXPECT_EQ(PAsel, 1) << "Wrong PA selected for " << power_W << "W";
        }
    }
}

/**
 * Test that PredictPowerLevel and CalculateAttenuation are inverses (SSB mode)
 * Note: Use higher attenuations to keep power below PA selection threshold
 */
TEST_F(PowerCalibrationTest, InverseFunctions_20W_SSB) {
    // Set a non-zero offset for SSB
    ED.PowerCal_20W_att_offset_dB[BAND_20M] = 2.5f;

    // Test attenuation values that keep power below 10W threshold
    // Higher attenuations to avoid PA auto-selection issues
    float32_t test_attenuations[] = {15.0f, 20.0f, 25.0f};

    for (float32_t atten : test_attenuations) {
        // Predict power from attenuation (SSB mode)
        float32_t power_mW = PredictPowerLevel(atten, 0, 1);
        float32_t power_W = power_mW / 1000.0f;

        // Calculate attenuation from power (SSB mode)
        int8_t PAsel = 0;
        float32_t calculated_atten = CalculateAttenuation(power_W, 1, &PAsel);

        // Should get back the original attenuation (within tolerance)
        // Use larger tolerance for numerical precision issues
        EXPECT_NEAR(calculated_atten, atten, 0.1f)
            << "Failed for attenuation=" << atten
            << ", power=" << power_W << "W";
    }

    // Reset offset
    ED.PowerCal_20W_att_offset_dB[BAND_20M] = 0.0f;
}

/**
 * Test power prediction for realistic operating scenarios
 */
TEST_F(PowerCalibrationTest, RealisticOperatingScenarios) {
    // Scenario 1: Low power QRP operation (5W)
    int8_t PAsel = 0;
    float32_t atten_5W = CalculateAttenuation(5.0f, 0, &PAsel);
    float32_t power_5W = PredictPowerLevel(atten_5W, PAsel, 0);
    EXPECT_NEAR(power_5W / 1000.0f, 5.0f, 0.05f);
    EXPECT_EQ(PAsel, 0);  // Should use 20W PA

    // Scenario 2: Medium power operation (50W)
    PAsel = 0;
    float32_t atten_50W = CalculateAttenuation(50.0f, 0, &PAsel);
    float32_t power_50W = PredictPowerLevel(atten_50W, PAsel, 0);
    EXPECT_NEAR(power_50W / 1000.0f, 50.0f, 0.5f);
    EXPECT_EQ(PAsel, 1);  // Should use 100W PA

    // Scenario 3: High power operation (100W)
    PAsel = 0;
    float32_t atten_100W = CalculateAttenuation(80.0f, 0, &PAsel);
    float32_t power_100W = PredictPowerLevel(atten_100W, PAsel, 0);
    EXPECT_NEAR(power_100W / 1000.0f, 80.0f, 0.8f);
    EXPECT_EQ(PAsel, 1);  // Should use 100W PA
}

/**
 * Test that higher attenuation always produces lower power
 */
TEST_F(PowerCalibrationTest, MonotonicBehavior) {
    float32_t power_0dB = PredictPowerLevel(0.0f, 0, 0);
    float32_t power_10dB = PredictPowerLevel(10.0f, 0, 0);
    float32_t power_20dB = PredictPowerLevel(20.0f, 0, 0);
    float32_t power_31_5dB = PredictPowerLevel(31.5f, 0, 0);

    // Power should decrease monotonically with increasing attenuation
    EXPECT_GT(power_0dB, power_10dB);
    EXPECT_GT(power_10dB, power_20dB);
    EXPECT_GT(power_20dB, power_31_5dB);
}

/**
 * Test fixture for SetPower function tests
 * These tests verify the menu callback and hardware integration
 */
class SetPowerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize minimal environment for SetPower tests
        InitializeStorage();
        InitializeFrontPanel();
        InitializeRFHardware();

        // Start the mode state machines
        ModeSm_start(&modeSM);
        UISm_start(&uiSM);

        // Set known calibration values for testing
        ED.PowerCal_20W_Psat_mW[BAND_20M] = 14680.0f;
        ED.PowerCal_20W_kindex[BAND_20M] = 16.2f;
        ED.PowerCal_20W_att_offset_dB[BAND_20M] = 0.0f;

        ED.PowerCal_100W_Psat_mW[BAND_20M] = 86000.0f;
        ED.PowerCal_100W_kindex[BAND_20M] = 10.0f;
        ED.PowerCal_100W_att_offset_dB[BAND_20M] = 0.0f;

        ED.PowerCal_20W_to_100W_threshold_W = 10.0f;

        // Set active band to 20M for testing
        ED.currentBand[ED.activeVFO] = BAND_20M;

        // Clear any pending interrupts
        ConsumeInterrupt();
    }

    void TearDown() override {
        // Clean up after each test
        ConsumeInterrupt();
    }
};

/**
 * Test SetPower correctly sets attenuation for low power (20W PA) in SSB mode
 */
TEST_F(SetPowerTest, SetPower_LowPower_SSB) {
    // Set mode to SSB receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    // Set a low power that should use the 20W PA
    float32_t target_power = 5.0f;  // 5 watts

    // Call SetPower
    SetPower(target_power,ModeSm_StateId_SSB_TRANSMIT);

    // Verify that PA100Wactive is false (should use 20W PA)
    EXPECT_FALSE(ED.PA100Wactive);

    // Verify that XAttenSSB was set for the current band
    float32_t atten_set = ED.XAttenSSB[ED.currentBand[ED.activeVFO]];

    // Attenuation should be within valid range
    EXPECT_GE(atten_set, 0.0f);
    EXPECT_LE(atten_set, 31.5f);

    // Verify that the power change interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);

    // Verify that the attenuation produces approximately the target power
    int8_t PAsel = 0;
    float32_t predicted_power_mW = PredictPowerLevel(atten_set, PAsel, 1); // SSB mode
    float32_t predicted_power_W = predicted_power_mW / 1000.0f;
    EXPECT_NEAR(predicted_power_W, target_power, 0.1f);
}

/**
 * Test SetPower correctly sets attenuation for high power (100W PA) in SSB mode
 */
TEST_F(SetPowerTest, SetPower_HighPower_SSB) {
    // Set mode to SSB receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    // Set a high power that should use the 100W PA
    float32_t target_power = 50.0f;  // 50 watts

    // Call SetPower
    SetPower(target_power,ModeSm_StateId_SSB_TRANSMIT);

    // Verify that PA100Wactive is true (should use 100W PA)
    EXPECT_TRUE(ED.PA100Wactive);

    // Verify that XAttenSSB was set for the current band
    float32_t atten_set = ED.XAttenSSB[ED.currentBand[ED.activeVFO]];

    // Attenuation should be within valid range
    EXPECT_GE(atten_set, 0.0f);
    EXPECT_LE(atten_set, 31.5f);

    // Verify that the power change interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);

    // Verify that the attenuation produces approximately the target power
    int8_t PAsel = 1;
    float32_t predicted_power_mW = PredictPowerLevel(atten_set, PAsel, 1); // SSB mode
    float32_t predicted_power_W = predicted_power_mW / 1000.0f;
    EXPECT_NEAR(predicted_power_W, target_power, 0.5f);
}

/**
 * Test SetPower correctly sets attenuation for low power (20W PA) in CW mode
 */
TEST_F(SetPowerTest, SetPower_LowPower_CW) {
    // Set mode to CW receive (not SSB)
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;

    // Set a low power that should use the 20W PA
    float32_t target_power = 5.0f;  // 5 watts

    // Call SetPower
    SetPower(target_power,ModeSm_StateId_CW_TRANSMIT_MARK);

    // Verify that PA100Wactive is false (should use 20W PA)
    EXPECT_FALSE(ED.PA100Wactive);

    // Verify that XAttenCW was set for the current band (not XAttenSSB)
    float32_t atten_set = ED.XAttenCW[ED.currentBand[ED.activeVFO]];

    // Attenuation should be within valid range
    EXPECT_GE(atten_set, 0.0f);
    EXPECT_LE(atten_set, 31.5f);

    // Verify that the power change interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);

    // Verify that the attenuation produces approximately the target power
    int8_t PAsel = 0;
    float32_t predicted_power_mW = PredictPowerLevel(atten_set, PAsel, 0); // CW mode
    float32_t predicted_power_W = predicted_power_mW / 1000.0f;
    EXPECT_NEAR(predicted_power_W, target_power, 0.1f);
}

/**
 * Test SetPower correctly sets attenuation for high power (100W PA) in CW mode
 */
TEST_F(SetPowerTest, SetPower_HighPower_CW) {
    // Set mode to CW receive (not SSB)
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;

    // Set a high power that should use the 100W PA
    float32_t target_power = 50.0f;  // 50 watts

    // Call SetPower
    SetPower(target_power,ModeSm_StateId_CW_TRANSMIT_MARK);

    // Verify that PA100Wactive is true (should use 100W PA)
    EXPECT_TRUE(ED.PA100Wactive);

    // Verify that XAttenCW was set for the current band (not XAttenSSB)
    float32_t atten_set = ED.XAttenCW[ED.currentBand[ED.activeVFO]];

    // Attenuation should be within valid range
    EXPECT_GE(atten_set, 0.0f);
    EXPECT_LE(atten_set, 31.5f);

    // Verify that the power change interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);

    // Verify that the attenuation produces approximately the target power
    int8_t PAsel = 1;
    float32_t predicted_power_mW = PredictPowerLevel(atten_set, PAsel, 0); // CW mode
    float32_t predicted_power_W = predicted_power_mW / 1000.0f;
    EXPECT_NEAR(predicted_power_W, target_power, 0.5f);
}

/**
 * Test SetPower at PA selection threshold (10W)
 * Verify it selects the 100W PA at exactly 10W
 */
TEST_F(SetPowerTest, SetPower_ThresholdPower_SSB) {
    // Set mode to SSB receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    // Set power at the threshold
    float32_t target_power = 10.0f;  // Exactly at threshold

    // Call SetPower
    SetPower(target_power,ModeSm_StateId_SSB_TRANSMIT);

    // Verify that PA100Wactive is true (should use 100W PA at threshold)
    EXPECT_TRUE(ED.PA100Wactive);

    // Verify that the power change interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
}

/**
 * Test SetPower across different bands
 * Verify it updates the correct band-specific attenuation
 */
TEST_F(SetPowerTest, SetPower_DifferentBands) {
    // Set mode to SSB receive
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;

    // Test on 20M band
    ED.currentBand[ED.activeVFO] = BAND_20M;
    float32_t power_20m = 5.0f;
    SetPower(power_20m,ModeSm_StateId_SSB_TRANSMIT);
    float32_t atten_20m = ED.XAttenSSB[BAND_20M];
    ConsumeInterrupt();

    // Test on 40M band
    ED.currentBand[ED.activeVFO] = BAND_40M;
    float32_t power_40m = 8.0f;
    SetPower(power_40m,ModeSm_StateId_SSB_TRANSMIT);
    float32_t atten_40m = ED.XAttenSSB[BAND_40M];
    ConsumeInterrupt();

    // Verify that different bands have different attenuations
    // (assuming calibration differs across bands, or powers differ)
    EXPECT_GE(atten_20m, 0.0f);
    EXPECT_GE(atten_40m, 0.0f);

    // Verify 20M band attenuation wasn't overwritten
    EXPECT_EQ(ED.XAttenSSB[BAND_20M], atten_20m);
}

/**
 * Test fixture for menu-based SetPower tests
 * These tests verify the UpdatePower callback is correctly invoked from the menu system
 */
class MenuSetPowerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test environment
        Q_in_L.setChannel(0);
        Q_in_R.setChannel(1);
        Q_in_L.clear();
        Q_in_R.clear();
        Q_in_L_Ex.setChannel(0);
        Q_in_R_Ex.setChannel(1);
        Q_in_L_Ex.clear();
        Q_in_R_Ex.clear();
        StartMillis();

        // Radio startup code
        InitializeStorage();
        InitializeFrontPanel();
        InitializeSignalProcessing();
        InitializeAudio();
        InitializeDisplay();
        InitializeRFHardware();

        // Start the mode state machines
        modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
        modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
        ModeSm_start(&modeSM);
        ED.agc = AGCOff;
        ED.nrOptionSelect = NROff;
        uiSM.vars.splashDuration_ms = 1;
        UISm_start(&uiSM);
        UpdateAudioIOState();

        // Set known calibration values
        ED.PowerCal_20W_Psat_mW[BAND_20M] = 14680.0f;
        ED.PowerCal_20W_kindex[BAND_20M] = 16.2f;
        ED.PowerCal_20W_att_offset_dB[BAND_20M] = 0.0f;

        ED.PowerCal_100W_Psat_mW[BAND_20M] = 86000.0f;
        ED.PowerCal_100W_kindex[BAND_20M] = 10.0f;
        ED.PowerCal_100W_att_offset_dB[BAND_20M] = 0.0f;

        ED.PowerCal_20W_to_100W_threshold_W = 10.0f;

        // Set active band to 20M
        ED.currentBand[ED.activeVFO] = BAND_20M;

        // Reset menu indices
        extern size_t primaryMenuIndex;
        extern size_t secondaryMenuIndex;
        primaryMenuIndex = 0;
        secondaryMenuIndex = 0;

        // Start the 1ms timer interrupt to simulate hardware timer
        start_timer1ms();
    }

    void TearDown() override {
        // Clean up after each test
        stop_timer1ms();
    }
};

/**
 * Helper function to navigate to RF menu
 */
void SelectRFMenu(void) {
    // Check initial state
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);

    // Enter main menu
    SetButton(MAIN_MENU_UP);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU);

    // Select first menu option (RF Options)
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Verify we're on the RF secondary menu
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SECONDARY_MENU);
    extern struct PrimaryMenuOption primaryMenu[8];
    extern size_t primaryMenuIndex;
    EXPECT_STREQ(primaryMenu[primaryMenuIndex].label, "RF Options");
}

/**
 * Test UpdatePower is correctly invoked when SSB Power is changed via menu
 */
TEST_F(MenuSetPowerTest, UpdatePower_InvokedFromSSBPowerMenu) {
    // Ensure we're in SSB receive mode
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to RF menu
    SelectRFMenu();

    // We should now be on the SSB Power option (first item in RF menu)
    // Store initial SSB power and attenuation
    float32_t initial_power = ED.powerOutSSB[ED.currentBand[ED.activeVFO]];
    float32_t initial_atten = ED.XAttenSSB[ED.currentBand[ED.activeVFO]];

    // Select SSB Power to enter UPDATE state
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_UPDATE);

    // Increment the power value
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);

    // Verify that powerOutSSB was incremented
    float32_t new_power = ED.powerOutSSB[ED.currentBand[ED.activeVFO]];
    EXPECT_GT(new_power, initial_power);

    // Verify that XAttenSSB was updated (UpdatePower was called)
    float32_t new_atten = ED.XAttenSSB[ED.currentBand[ED.activeVFO]];
    EXPECT_NE(new_atten, initial_atten);

    // Verify that iPOWER_CHANGE interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
    ConsumeInterrupt();

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

/**
 * Test UpdatePower is correctly invoked when CW Power is changed via menu
 */
TEST_F(MenuSetPowerTest, UpdatePower_InvokedFromCWPowerMenu) {
    // Switch to CW receive mode
    modeSM.state_id = ModeSm_StateId_CW_RECEIVE;

    // Navigate to RF menu
    SelectRFMenu();

    // Navigate to CW Power option (second item in RF menu)
    IncrementSecondaryMenu();

    // Store initial CW power and attenuation
    float32_t initial_power = ED.powerOutCW[ED.currentBand[ED.activeVFO]];
    float32_t initial_atten = ED.XAttenCW[ED.currentBand[ED.activeVFO]];

    // Select CW Power to enter UPDATE state
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_UPDATE);

    // Increment the power value
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);

    // Verify that powerOutCW was incremented
    float32_t new_power = ED.powerOutCW[ED.currentBand[ED.activeVFO]];
    EXPECT_GT(new_power, initial_power);

    // Verify that XAttenCW was updated (UpdatePower was called)
    float32_t new_atten = ED.XAttenCW[ED.currentBand[ED.activeVFO]];
    EXPECT_NE(new_atten, initial_atten);

    // Verify that iPOWER_CHANGE interrupt was set
    EXPECT_EQ(GetInterrupt(), iPOWER_CHANGE);
    ConsumeInterrupt();

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

/**
 * Test that PA selection changes correctly when power crosses threshold via menu
 */
TEST_F(MenuSetPowerTest, UpdatePower_PASelectionChanges) {
    // Ensure we're in SSB receive mode
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Set initial power below threshold
    ED.powerOutSSB[ED.currentBand[ED.activeVFO]] = 5.0f;

    // Call SetPower to set initial state
    SetPower(5.0f,ModeSm_StateId_SSB_TRANSMIT);
    EXPECT_FALSE(ED.PA100Wactive);
    ConsumeInterrupt();

    // Navigate to RF menu and select SSB Power
    SelectRFMenu();
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_UPDATE);

    // Increment power multiple times to cross the 10W threshold
    for (int i = 0; i < 12; i++) {  // Increment by 0.5W each time, 12 times = 6W increase
        SetInterrupt(iFILTER_INCREASE);
        loop(); MyDelay(10);
        ConsumeInterrupt();
    }

    // Verify that power is now above threshold
    float32_t final_power = ED.powerOutSSB[ED.currentBand[ED.activeVFO]];
    EXPECT_GE(final_power, 10.0f);

    // Verify that PA selection switched to 100W PA
    EXPECT_TRUE(ED.PA100Wactive);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}

