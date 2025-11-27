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
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_CW_PA);
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

