
#include <gtest/gtest.h>
#include "SDT.h"
#include "PowerCalSm.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iostream>

// External declarations for power calibration internal state
// These are static variables in MainBoard_DisplayCalibration_Power.cpp
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
    if (!PowerCalSmBusy()) // not while the main loop is dispatching
        PowerCalSm_dispatch_event(&powerSM, PowerCalSm_EventId_DO);
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
        ED.PowerCal_20W_DSP_Gain_correction_dB[BAND_20M] = 0.0f;

        // Default values for 100W PA
        ED.PowerCal_100W_Psat_mW[BAND_20M] = 86000.0f;
        ED.PowerCal_100W_kindex[BAND_20M] = 10.0f;
        ED.PowerCal_100W_DSP_Gain_correction_dB[BAND_20M] = 0.0f;

        // Threshold for PA selection
        ED.PowerCal_20W_to_100W_threshold_W = 10.0f;

        // Set active band to 20M for testing
        ED.currentBand[ED.activeVFO] = BAND_20M;
    }
};

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
        ED.PowerCal_20W_DSP_Gain_correction_dB[BAND_20M] = 0.0f;

        ED.PowerCal_100W_Psat_mW[BAND_20M] = 86000.0f;
        ED.PowerCal_100W_kindex[BAND_20M] = 10.0f;
        ED.PowerCal_100W_DSP_Gain_correction_dB[BAND_20M] = 0.0f;

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
    extern struct PrimaryMenuOption primaryMenu[9];
    extern size_t primaryMenuIndex;
    EXPECT_STREQ(primaryMenu[primaryMenuIndex].label, "RF Options");
}

/**
 * Test UpdatePower is correctly invoked when SSB Power is changed via menu
 */
TEST_F(MenuSetPowerTest, UpdatePower_InvokedFromSSBPowerMenu) {
    extern float32_t TXgainDSP;
    // Ensure we're in SSB receive mode
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Navigate to RF menu
    SelectRFMenu();

    // We should now be on the SSB Power option (first item in RF menu)
    // Store initial SSB power and DSP gain
    float32_t initial_power = ED.powerOutSSB[ED.currentBand[ED.activeVFO]];
    float32_t initial_gain = TXgainDSP;

    // Select SSB Power to enter UPDATE state
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_UPDATE);

    // Increment the power value
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);

    SetInterrupt(iPTT_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    // Verify that powerOutSSB was incremented
    float32_t new_power = ED.powerOutSSB[ED.currentBand[ED.activeVFO]];
    EXPECT_GT(new_power, initial_power);

    // Verify that DSP gain was updated
    EXPECT_NE(TXgainDSP, initial_gain);

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
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_UPDATE);

    // Increment the power value
    SetInterrupt(iFILTER_INCREASE);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);

    SetInterrupt(iKEY1_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    // Verify that powerOutCW was incremented
    float32_t new_power = ED.powerOutCW[ED.currentBand[ED.activeVFO]];
    EXPECT_GT(new_power, initial_power);

    // Verify that XAttenCW was updated (UpdatePower was called)
    float32_t new_atten = ED.XAttenCW[ED.currentBand[ED.activeVFO]];
    EXPECT_NE(new_atten, initial_atten);
}

/**
 * Test that PA selection changes correctly when power crosses threshold via menu
 */
TEST_F(MenuSetPowerTest, UpdatePower_PASelectionChanges) {
    // Ensure we're in SSB receive mode
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Set initial power below threshold
    ED.powerOutSSB[ED.currentBand[ED.activeVFO]] = 5.0f;
    EXPECT_FALSE(ED.PA100Wactive);

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

    // Exit back to home screen
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);

    // Press PTT to trigger transmit mode - this is when PA selection is updated
    SetInterrupt(iPTT_PRESSED);
    loop(); MyDelay(10);
    loop(); MyDelay(10);

    // Verify that PA selection switched to 100W PA
    EXPECT_TRUE(ED.PA100Wactive);
}

////////////////////////////////////////////////////////////////////////////////
// Tests for Power Conversion Functions
// These test the core mathematical functions that convert between power and
// attenuation/gain settings
////////////////////////////////////////////////////////////////////////////////

/**
 * Test CalculateCWPowerLevel with 20W PA at various attenuation levels
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_20W_VariousAttenuations) {
    // Test with known calibration values for 20W PA on 20M band
    // P_sat = 14680 mW, k = 16.2

    // Test at 0 dB attenuation (maximum power)
    float32_t power_0dB = CalculateCWPowerLevel(0.0f, 0);
    EXPECT_GT(power_0dB, 13000.0f);  // Should be close to saturation
    EXPECT_LE(power_0dB, 14680.0f);  // Should be at or below saturation

    // Test at 3 dB attenuation
    float32_t power_3dB = CalculateCWPowerLevel(3.0f, 0);
    EXPECT_GT(power_3dB, 0.0f);
    EXPECT_LT(power_3dB, power_0dB);  // Less power than 0 dB

    // Test at 10 dB attenuation
    float32_t power_10dB = CalculateCWPowerLevel(10.0f, 0);
    EXPECT_GT(power_10dB, 0.0f);
    EXPECT_LT(power_10dB, power_3dB);  // Less power than 3 dB

    // Test at 20 dB attenuation
    float32_t power_20dB = CalculateCWPowerLevel(20.0f, 0);
    EXPECT_GT(power_20dB, 0.0f);
    EXPECT_LT(power_20dB, power_10dB);  // Less power than 10 dB

    // Verify monotonically decreasing power with increasing attenuation
    EXPECT_GT(power_0dB, power_3dB);
    EXPECT_GT(power_3dB, power_10dB);
    EXPECT_GT(power_10dB, power_20dB);
}

/**
 * Test CalculateCWPowerLevel with 100W PA at various attenuation levels
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_100W_VariousAttenuations) {
    // Test with known calibration values for 100W PA on 20M band
    // P_sat = 86000 mW, k = 10.0

    // Test at 0 dB attenuation (maximum power)
    float32_t power_0dB = CalculateCWPowerLevel(0.0f, 1);
    EXPECT_GT(power_0dB, 70000.0f);  // Should be close to saturation
    EXPECT_LE(power_0dB, 86000.0f);  // Should be at or below saturation

    // Test at 3 dB attenuation
    float32_t power_3dB = CalculateCWPowerLevel(3.0f, 1);
    EXPECT_GT(power_3dB, 0.0f);
    EXPECT_LT(power_3dB, power_0dB);

    // Test at 10 dB attenuation
    float32_t power_10dB = CalculateCWPowerLevel(10.0f, 1);
    EXPECT_GT(power_10dB, 0.0f);
    EXPECT_LT(power_10dB, power_3dB);
}

/**
 * Test CalculateCWPowerLevel with invalid inputs
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_InvalidInputs) {
    // Negative attenuation should return 0
    float32_t result = CalculateCWPowerLevel(-5.0f, 0);
    EXPECT_EQ(result, 0.0f);

    // Attenuation > 31.5 dB should return 0
    result = CalculateCWPowerLevel(32.0f, 0);
    EXPECT_EQ(result, 0.0f);
}

/**
 * Test CalculateCWAttenuation with 20W PA at various power levels
 */
TEST_F(PowerCalibrationTest, CalculateCWAttenuation_20W_VariousPowers) {
    bool PAsel;

    // Test at 1W (should use 20W PA)
    float32_t atten_1W = CalculateCWAttenuation(1.0f, &PAsel);
    EXPECT_FALSE(PAsel);  // Should select 20W PA
    EXPECT_GT(atten_1W, 0.0f);
    EXPECT_LT(atten_1W, 31.5f);

    // Test at 5W (should use 20W PA)
    float32_t atten_5W = CalculateCWAttenuation(5.0f, &PAsel);
    EXPECT_FALSE(PAsel);  // Should select 20W PA
    EXPECT_GT(atten_5W, 0.0f);
    EXPECT_LT(atten_5W, atten_1W);  // Less attenuation for more power

    // Test at 9W (should use 20W PA, just below threshold)
    float32_t atten_9W = CalculateCWAttenuation(9.0f, &PAsel);
    EXPECT_FALSE(PAsel);  // Should select 20W PA
    EXPECT_GT(atten_9W, 0.0f);
    EXPECT_LT(atten_9W, atten_5W);
}

/**
 * Test CalculateCWAttenuation with 100W PA at various power levels
 */
TEST_F(PowerCalibrationTest, CalculateCWAttenuation_100W_VariousPowers) {
    bool PAsel;

    // Test at 15W (should use 100W PA, above threshold)
    float32_t atten_15W = CalculateCWAttenuation(15.0f, &PAsel);
    EXPECT_TRUE(PAsel);  // Should select 100W PA
    EXPECT_GT(atten_15W, 0.0f);
    EXPECT_LT(atten_15W, 31.5f);

    // Test at 50W (should use 100W PA)
    float32_t atten_50W = CalculateCWAttenuation(50.0f, &PAsel);
    EXPECT_TRUE(PAsel);  // Should select 100W PA
    EXPECT_GT(atten_50W, 0.0f);
    EXPECT_LT(atten_50W, atten_15W);  // Less attenuation for more power

    // Test at 80W (should use 100W PA)
    float32_t atten_80W = CalculateCWAttenuation(80.0f, &PAsel);
    EXPECT_TRUE(PAsel);  // Should select 100W PA
    EXPECT_GT(atten_80W, 0.0f);
    EXPECT_LT(atten_80W, atten_50W);
}

/**
 * Test CalculateCWAttenuation at threshold boundary
 */
TEST_F(PowerCalibrationTest, CalculateCWAttenuation_ThresholdBoundary) {
    bool PAsel;

    // Just below threshold (9.9W) - should use 20W PA
    CalculateCWAttenuation(9.9f, &PAsel);
    EXPECT_FALSE(PAsel);

    // At threshold (10.0W) - should use 100W PA
    CalculateCWAttenuation(10.0f, &PAsel);
    EXPECT_TRUE(PAsel);

    // Just above threshold (10.1W) - should use 100W PA
    CalculateCWAttenuation(10.1f, &PAsel);
    EXPECT_TRUE(PAsel);
}

/**
 * Test CalculateCWAttenuation with invalid inputs
 */
TEST_F(PowerCalibrationTest, CalculateCWAttenuation_InvalidInputs) {
    bool PAsel;

    // Negative power should return 0
    float32_t result = CalculateCWAttenuation(-5.0f, &PAsel);
    EXPECT_EQ(result, 0.0f);

    // Power > 100W should return 0
    result = CalculateCWAttenuation(150.0f, &PAsel);
    EXPECT_EQ(result, 0.0f);
}

/**
 * Test that CalculateCWAttenuation returns attenuation within valid range (0 to 31.5 dB)
 * This test ensures the function handles all edge cases including power exceeding P_sat
 */
TEST_F(PowerCalibrationTest, CalculateCWAttenuation_AttenuationLimits) {
    bool PAsel;
    float32_t atten;

    // Test 20W PA scenarios (P_sat = 14.68W for 20M band)

    // Very low power - should give high attenuation, but not exceed 31.5 dB
    atten = CalculateCWAttenuation(0.1f, &PAsel);
    EXPECT_FALSE(PAsel);  // Should select 20W PA
    EXPECT_GE(atten, 0.0f) << "Attenuation should not be negative";
    EXPECT_LE(atten, 31.5f) << "Attenuation should not exceed 31.5 dB";

    // Low power
    atten = CalculateCWAttenuation(0.5f, &PAsel);
    EXPECT_FALSE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);

    // Mid-range power
    atten = CalculateCWAttenuation(5.0f, &PAsel);
    EXPECT_FALSE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);

    // High power within 20W PA range (below 10W threshold)
    atten = CalculateCWAttenuation(9.5f, &PAsel);
    EXPECT_FALSE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);

    // Power at threshold - should still use 20W PA (< 10W)
    atten = CalculateCWAttenuation(9.9f, &PAsel);
    EXPECT_FALSE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);

    // Note: 14.68W (P_sat for 20W PA) is above 10W threshold, so it uses 100W PA
    // Testing with powers above 10W is handled in the 100W PA section below

    // Power exceeding 20W PA's P_sat but below threshold (not practically possible
    // since P_sat=14.68W > threshold=10W, but test the clamping logic)
    // This case is actually handled by the 100W PA since power > 10W

    // Test 100W PA scenarios (P_sat = 86W for 20M band)

    // Low power on 100W PA
    atten = CalculateCWAttenuation(15.0f, &PAsel);
    EXPECT_TRUE(PAsel);  // Should select 100W PA (above 10W threshold)
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);

    // Mid-range power
    atten = CalculateCWAttenuation(50.0f, &PAsel);
    EXPECT_TRUE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);

    // High power, approaching P_sat
    atten = CalculateCWAttenuation(80.0f, &PAsel);
    EXPECT_TRUE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);

    // Power very close to P_sat (86W)
    atten = CalculateCWAttenuation(85.5f, &PAsel);
    EXPECT_TRUE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);

    // Power equal to P_sat - should return 0 dB
    atten = CalculateCWAttenuation(86.0f, &PAsel);
    EXPECT_TRUE(PAsel);
    EXPECT_NEAR(atten, 0.0f, 0.1f) << "Attenuation at P_sat should be ~0 dB";
    EXPECT_LE(atten, 31.5f);

    // Power exceeding P_sat - should clamp to 0 dB
    atten = CalculateCWAttenuation(95.0f, &PAsel);
    EXPECT_TRUE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);
    EXPECT_NEAR(atten, 0.0f, 0.1f) << "Attenuation above P_sat should be clamped to ~0 dB";

    // Maximum valid power (100W) - should also clamp to 0 dB
    atten = CalculateCWAttenuation(100.0f, &PAsel);
    EXPECT_TRUE(PAsel);
    EXPECT_GE(atten, 0.0f);
    EXPECT_LE(atten, 31.5f);
    EXPECT_NEAR(atten, 0.0f, 0.1f) << "Attenuation at 100W should be clamped to ~0 dB";
}

/**
 * Test attenuation limits across a wide range of power values
 * This is a comprehensive sweep test to ensure no corner cases are missed
 */
TEST_F(PowerCalibrationTest, CalculateCWAttenuation_AttenuationLimits_Sweep) {
    bool PAsel;
    float32_t atten;

    // Sweep through power values for 20W PA (0.01W to 10W)
    for (float32_t power = 0.01f; power <= 10.0f; power += 0.5f) {
        atten = CalculateCWAttenuation(power, &PAsel);
        EXPECT_FALSE(PAsel) << "PA selection failed at power " << power << "W";
        EXPECT_GE(atten, 0.0f) << "Attenuation is negative at power " << power << "W";
        EXPECT_LE(atten, 31.5f) << "Attenuation exceeds 31.5 dB at power " << power << "W";
        EXPECT_FALSE(std::isnan(atten)) << "Attenuation is NaN at power " << power << "W";
        EXPECT_FALSE(std::isinf(atten)) << "Attenuation is infinite at power " << power << "W";
    }

    // Sweep through power values for 100W PA (10W to 100W)
    for (float32_t power = 10.0f; power <= 100.0f; power += 5.0f) {
        atten = CalculateCWAttenuation(power, &PAsel);
        EXPECT_TRUE(PAsel) << "PA selection failed at power " << power << "W";
        EXPECT_GE(atten, 0.0f) << "Attenuation is negative at power " << power << "W";
        EXPECT_LE(atten, 31.5f) << "Attenuation exceeds 31.5 dB at power " << power << "W";
        EXPECT_FALSE(std::isnan(atten)) << "Attenuation is NaN at power " << power << "W";
        EXPECT_FALSE(std::isinf(atten)) << "Attenuation is infinite at power " << power << "W";
    }
}

/**
 * Test edge cases at exactly 0 dB and 31.5 dB boundaries
 */
TEST_F(PowerCalibrationTest, CalculateCWAttenuation_BoundaryConditions) {
    bool PAsel;
    float32_t atten;

    // Note: 20W PA P_sat (14.68W) is above 10W threshold, so it uses 100W PA
    // Cannot directly test 20W PA at its P_sat since it would trigger 100W PA selection

    // For 100W PA: Test that power at P_sat gives ~0 dB attenuation
    atten = CalculateCWAttenuation(86.0f, &PAsel);
    EXPECT_TRUE(PAsel);
    EXPECT_NEAR(atten, 0.0f, 0.1f) << "Power at P_sat should give ~0 dB attenuation";

    // Test minimum power (should give maximum attenuation, clamped to 31.5 dB)
    atten = CalculateCWAttenuation(0.001f, &PAsel);
    EXPECT_FALSE(PAsel);
    EXPECT_LE(atten, 31.5f) << "Even minimum power should not exceed 31.5 dB attenuation";
    EXPECT_GT(atten, 20.0f) << "Minimum power should give high attenuation";

    // Test that the function handles the full dynamic range
    // P_sat * tanh(k * 10^(-31.5/10)) should be the minimum achievable power
    // For 20W PA: k=16.2, P_sat=14680 mW
    // Min power = 14680 * tanh(16.2 * 10^(-31.5/10)) = 14680 * tanh(16.2 * 0.000708) ≈ 0.12W
    // Any power below this should clamp attenuation to 31.5 dB
    atten = CalculateCWAttenuation(0.01f, &PAsel);
    EXPECT_FALSE(PAsel);
    EXPECT_LE(atten, 31.5f);
}

/**
 * Test monotonicity: increasing power should result in decreasing attenuation
 */
TEST_F(PowerCalibrationTest, CalculateCWAttenuation_Monotonicity) {
    bool PAsel;

    // Test for 20W PA
    float32_t prev_atten = 31.5f;
    for (float32_t power = 1.0f; power <= 9.0f; power += 1.0f) {
        float32_t atten = CalculateCWAttenuation(power, &PAsel);
        EXPECT_FALSE(PAsel);
        EXPECT_LE(atten, prev_atten)
            << "Attenuation should decrease (or stay same) as power increases: "
            << "power=" << power << "W, atten=" << atten << " dB, prev=" << prev_atten << " dB";
        prev_atten = atten;
    }

    // Test for 100W PA
    prev_atten = 31.5f;
    for (float32_t power = 15.0f; power <= 85.0f; power += 10.0f) {
        float32_t atten = CalculateCWAttenuation(power, &PAsel);
        EXPECT_TRUE(PAsel);
        EXPECT_LE(atten, prev_atten)
            << "Attenuation should decrease (or stay same) as power increases: "
            << "power=" << power << "W, atten=" << atten << " dB, prev=" << prev_atten << " dB";
        prev_atten = atten;
    }
}

/**
 * Test roundtrip conversion: power -> attenuation -> power
 */
TEST_F(PowerCalibrationTest, CW_PowerAttenuation_Roundtrip) {
    bool PAsel;

    // Test roundtrip for 5W (20W PA)
    float32_t target_power = 5.0f;
    float32_t atten = CalculateCWAttenuation(target_power, &PAsel);
    float32_t recovered_power = CalculateCWPowerLevel(atten, PAsel ? 1 : 0);

    // Should recover the original power within 1% tolerance
    EXPECT_NEAR(recovered_power / 1000.0f, target_power, target_power * 0.01f);

    // Test roundtrip for 50W (100W PA)
    target_power = 50.0f;
    atten = CalculateCWAttenuation(target_power, &PAsel);
    recovered_power = CalculateCWPowerLevel(atten, PAsel ? 1 : 0);

    // Should recover the original power within 1% tolerance
    EXPECT_NEAR(recovered_power / 1000.0f, target_power, target_power * 0.01f);
}

// ============================================================================
// attenToPower_mW Direct Unit Tests
// ============================================================================

/**
 * Test attenToPower_mW returns power within valid range
 * Power should be > 0 and <= P_sat for all valid attenuation values
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_PowerLimits) {
    float32_t power;

    // Test with 20W PA parameters (P_sat = 14680 mW, k = 16.2)
    const float32_t P_sat_20W = 14680.0f;
    const float32_t k_20W = 16.2f;

    // Minimum attenuation (0 dB) - should give maximum power (close to P_sat)
    power = attenToPower_mW(0.0f, P_sat_20W, k_20W);
    EXPECT_GT(power, 0.0f) << "Power should be positive at 0 dB attenuation";
    EXPECT_LE(power, P_sat_20W) << "Power should not exceed P_sat at 0 dB";
    EXPECT_GT(power, 14000.0f) << "Power at 0 dB should be close to P_sat";

    // Low attenuation
    power = attenToPower_mW(3.0f, P_sat_20W, k_20W);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat_20W);

    // Mid-range attenuation
    power = attenToPower_mW(15.0f, P_sat_20W, k_20W);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat_20W);

    // High attenuation
    power = attenToPower_mW(25.0f, P_sat_20W, k_20W);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat_20W);

    // Maximum attenuation (31.5 dB)
    power = attenToPower_mW(31.5f, P_sat_20W, k_20W);
    EXPECT_GT(power, 0.0f) << "Power should still be positive at 31.5 dB";
    EXPECT_LE(power, P_sat_20W);
    EXPECT_LT(power, 200.0f) << "Power at 31.5 dB should be very low";

    // Test with 100W PA parameters (P_sat = 86000 mW, k = 10.0)
    const float32_t P_sat_100W = 86000.0f;
    const float32_t k_100W = 10.0f;

    // Minimum attenuation (0 dB)
    power = attenToPower_mW(0.0f, P_sat_100W, k_100W);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat_100W);
    EXPECT_GT(power, 80000.0f) << "Power at 0 dB should be close to P_sat";

    // Low attenuation
    power = attenToPower_mW(3.0f, P_sat_100W, k_100W);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat_100W);

    // Mid-range attenuation
    power = attenToPower_mW(15.0f, P_sat_100W, k_100W);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat_100W);

    // High attenuation
    power = attenToPower_mW(25.0f, P_sat_100W, k_100W);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat_100W);

    // Maximum attenuation (31.5 dB)
    power = attenToPower_mW(31.5f, P_sat_100W, k_100W);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat_100W);
    EXPECT_LT(power, 650.0f) << "Power at 31.5 dB should be very low";
}

/**
 * Test attenToPower_mW with sweep through attenuation range
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_PowerLimits_Sweep) {
    float32_t power;
    const float32_t P_sat_20W = 14680.0f;
    const float32_t k_20W = 16.2f;
    const float32_t P_sat_100W = 86000.0f;
    const float32_t k_100W = 10.0f;

    // Sweep through attenuation values for 20W PA (0 to 31.5 dB in 0.5 dB steps)
    for (float32_t atten = 0.0f; atten <= 31.5f; atten += 0.5f) {
        power = attenToPower_mW(atten, P_sat_20W, k_20W);
        EXPECT_GT(power, 0.0f) << "Power should be positive at attenuation " << atten << " dB";
        EXPECT_LE(power, P_sat_20W) << "Power should not exceed P_sat at attenuation " << atten << " dB";
        EXPECT_FALSE(std::isnan(power)) << "Power is NaN at attenuation " << atten << " dB";
        EXPECT_FALSE(std::isinf(power)) << "Power is infinite at attenuation " << atten << " dB";
    }

    // Sweep through attenuation values for 100W PA (0 to 31.5 dB in 1 dB steps)
    for (float32_t atten = 0.0f; atten <= 31.5f; atten += 1.0f) {
        power = attenToPower_mW(atten, P_sat_100W, k_100W);
        EXPECT_GT(power, 0.0f) << "Power should be positive at attenuation " << atten << " dB";
        EXPECT_LE(power, P_sat_100W) << "Power should not exceed P_sat at attenuation " << atten << " dB";
        EXPECT_FALSE(std::isnan(power)) << "Power is NaN at attenuation " << atten << " dB";
        EXPECT_FALSE(std::isinf(power)) << "Power is infinite at attenuation " << atten << " dB";
    }
}

/**
 * Test attenToPower_mW boundary conditions
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_BoundaryConditions) {
    float32_t power;
    const float32_t P_sat_20W = 14680.0f;
    const float32_t k_20W = 16.2f;
    const float32_t P_sat_100W = 86000.0f;
    const float32_t k_100W = 10.0f;

    // For 20W PA: Test that 0 dB gives power close to P_sat
    power = attenToPower_mW(0.0f, P_sat_20W, k_20W);
    EXPECT_NEAR(power, P_sat_20W, 100.0f) << "Power at 0 dB should be close to P_sat";

    // For 100W PA: Test that 0 dB gives power close to P_sat
    power = attenToPower_mW(0.0f, P_sat_100W, k_100W);
    EXPECT_NEAR(power, P_sat_100W, 1000.0f) << "Power at 0 dB should be close to P_sat";

    // Test maximum attenuation (31.5 dB) gives very low but positive power
    power = attenToPower_mW(31.5f, P_sat_20W, k_20W);
    EXPECT_GT(power, 0.0f) << "Power at 31.5 dB should still be positive";
    EXPECT_LT(power, 200.0f) << "Power at 31.5 dB should be very low (20W PA)";

    power = attenToPower_mW(31.5f, P_sat_100W, k_100W);
    EXPECT_GT(power, 0.0f) << "Power at 31.5 dB should still be positive";
    EXPECT_LT(power, 650.0f) << "Power at 31.5 dB should be very low (100W PA)";

    // Test the physical model: at very high attenuation, power approaches 0
    // but never reaches exactly 0 due to tanh function asymptotic behavior
    power = attenToPower_mW(31.5f, P_sat_20W, k_20W);
    EXPECT_GT(power, 0.0f);

    power = attenToPower_mW(31.5f, P_sat_100W, k_100W);
    EXPECT_GT(power, 0.0f);
}

/**
 * Test attenToPower_mW monotonicity: increasing attenuation should decrease power
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_Monotonicity) {
    const float32_t P_sat_20W = 14680.0f;
    const float32_t k_20W = 16.2f;
    const float32_t P_sat_100W = 86000.0f;
    const float32_t k_100W = 10.0f;

    // Test for 20W PA
    float32_t prev_power = 15000.0f; // Start with value higher than P_sat
    for (float32_t atten = 0.0f; atten <= 31.0f; atten += 1.0f) {
        float32_t power = attenToPower_mW(atten, P_sat_20W, k_20W);
        EXPECT_LE(power, prev_power)
            << "Power should decrease (or stay same) as attenuation increases: "
            << "atten=" << atten << " dB, power=" << power << " mW, prev=" << prev_power << " mW";
        prev_power = power;
    }

    // Test for 100W PA
    prev_power = 90000.0f; // Start with value higher than P_sat
    for (float32_t atten = 0.0f; atten <= 31.0f; atten += 2.0f) {
        float32_t power = attenToPower_mW(atten, P_sat_100W, k_100W);
        EXPECT_LE(power, prev_power)
            << "Power should decrease (or stay same) as attenuation increases: "
            << "atten=" << atten << " dB, power=" << power << " mW, prev=" << prev_power << " mW";
        prev_power = power;
    }
}

/**
 * Test attenToPower_mW never exceeds P_sat for any valid attenuation
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_NeverExceedsPsat) {
    float32_t power;
    const float32_t P_sat_20W = 14680.0f;
    const float32_t k_20W = 16.2f;
    const float32_t P_sat_100W = 86000.0f;
    const float32_t k_100W = 10.0f;

    // For 20W PA: Test every 0.1 dB from 0 to 31.5 dB
    for (float32_t atten = 0.0f; atten <= 31.5f; atten += 0.1f) {
        power = attenToPower_mW(atten, P_sat_20W, k_20W);
        EXPECT_LE(power, P_sat_20W)
            << "Power (" << power << " mW) exceeds P_sat at attenuation " << atten << " dB";
    }

    // For 100W PA: Test every 0.5 dB from 0 to 31.5 dB
    for (float32_t atten = 0.0f; atten <= 31.5f; atten += 0.5f) {
        power = attenToPower_mW(atten, P_sat_100W, k_100W);
        EXPECT_LE(power, P_sat_100W)
            << "Power (" << power << " mW) exceeds P_sat at attenuation " << atten << " dB";
    }
}

/**
 * Test attenToPower_mW with edge case parameters
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_EdgeCaseParameters) {
    float32_t power;

    // Test with very small k value (low drive ratio)
    power = attenToPower_mW(10.0f, 14680.0f, 0.1f);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 14680.0f);

    // Test with very large k value (high drive ratio)
    power = attenToPower_mW(10.0f, 14680.0f, 100.0f);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 14680.0f);

    // Test with very small P_sat
    power = attenToPower_mW(10.0f, 100.0f, 16.2f);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 100.0f);

    // Test with very large P_sat
    power = attenToPower_mW(10.0f, 1000000.0f, 16.2f);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 1000000.0f);
}

/**
 * Test attenToPower_mW physical model validation
 * Formula: P_out = P_sat * tanh(k * 10^(-attenuation/10))
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_PhysicalModelValidation) {
    const float32_t P_sat = 14680.0f;
    const float32_t k = 16.2f;

    // At 0 dB attenuation: P_out = P_sat * tanh(k * 1.0) = P_sat * tanh(k)
    float32_t power_0dB = attenToPower_mW(0.0f, P_sat, k);
    float32_t expected_0dB = P_sat * tanh(k);
    EXPECT_NEAR(power_0dB, expected_0dB, 0.1f);

    // At 10 dB attenuation: P_out = P_sat * tanh(k * 0.1)
    float32_t power_10dB = attenToPower_mW(10.0f, P_sat, k);
    float32_t expected_10dB = P_sat * tanh(k * 0.1f);
    EXPECT_NEAR(power_10dB, expected_10dB, 0.1f);

    // At 20 dB attenuation: P_out = P_sat * tanh(k * 0.01)
    float32_t power_20dB = attenToPower_mW(20.0f, P_sat, k);
    float32_t expected_20dB = P_sat * tanh(k * 0.01f);
    EXPECT_NEAR(power_20dB, expected_20dB, 0.1f);

    // Verify the mathematical relationship is correct
    EXPECT_LT(power_10dB, power_0dB);
    EXPECT_LT(power_20dB, power_10dB);
}

/**
 * Test attenToPower_mW with invalid attenuation values
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_InvalidAttenuation) {
    float32_t power;
    const float32_t P_sat = 14680.0f;
    const float32_t k = 16.2f;

    // Negative attenuation - now returns 0 (input validation)
    power = attenToPower_mW(-1.0f, P_sat, k);
    EXPECT_EQ(power, 0.0f) << "Negative attenuation should return 0";

    power = attenToPower_mW(-10.0f, P_sat, k);
    EXPECT_EQ(power, 0.0f) << "Negative attenuation should return 0";

    power = attenToPower_mW(-50.0f, P_sat, k);
    EXPECT_EQ(power, 0.0f) << "Negative attenuation should return 0";

    // Very large attenuation (beyond valid range but still positive)
    // Function should still work since attenuation >= 0 and P_sat >= 0
    power = attenToPower_mW(50.0f, P_sat, k);
    EXPECT_GT(power, 0.0f) << "Power should still be positive at very high attenuation";
    EXPECT_LE(power, P_sat);
    EXPECT_LT(power, 10.0f) << "Power at 50 dB should be extremely low";

    power = attenToPower_mW(100.0f, P_sat, k);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat);
    EXPECT_LT(power, 1.0f) << "Power at 100 dB should be nearly zero";
}

/**
 * Test attenToPower_mW with invalid P_sat values
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_InvalidPsat) {
    float32_t power;
    const float32_t k = 16.2f;

    // Negative P_sat - now returns 0 (input validation)
    power = attenToPower_mW(10.0f, -1000.0f, k);
    EXPECT_EQ(power, 0.0f) << "Negative P_sat should return 0";

    power = attenToPower_mW(0.0f, -14680.0f, k);
    EXPECT_EQ(power, 0.0f) << "Negative P_sat should return 0";

    // Zero P_sat - should return 0 (mathematically: 0 * tanh(...) = 0)
    power = attenToPower_mW(10.0f, 0.0f, k);
    EXPECT_EQ(power, 0.0f) << "Zero P_sat should result in zero power";

    power = attenToPower_mW(0.0f, 0.0f, k);
    EXPECT_EQ(power, 0.0f);

    power = attenToPower_mW(31.5f, 0.0f, k);
    EXPECT_EQ(power, 0.0f);

    // Very small positive P_sat - should still work
    power = attenToPower_mW(10.0f, 0.001f, k);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 0.001f);

    // Infinity P_sat - mathematical edge case
    power = attenToPower_mW(10.0f, INFINITY, k);
    EXPECT_TRUE(std::isinf(power) || std::isnan(power)) << "Infinite P_sat should result in inf or NaN";
}

/**
 * Test attenToPower_mW with invalid k values
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_InvalidK) {
    float32_t power;
    const float32_t P_sat = 14680.0f;

    // Negative k - mathematically defined but physically nonsensical
    // Formula: P_sat * tanh(k * 10^(-att/10))
    // With k < 0: tanh(negative value) = negative
    // Result: P_sat * (negative) = negative power
    power = attenToPower_mW(10.0f, P_sat, -16.2f);
    EXPECT_LT(power, 0.0f) << "Negative k produces negative power";
    EXPECT_GE(power, -P_sat) << "Negative power should be >= -P_sat";

    power = attenToPower_mW(10.0f, P_sat, -10.0f);
    EXPECT_LT(power, 0.0f) << "Negative k results in negative power";
    EXPECT_GE(power, -P_sat);

    // Zero k - should return 0
    // P_sat * tanh(0 * anything) = P_sat * tanh(0) = P_sat * 0 = 0
    power = attenToPower_mW(10.0f, P_sat, 0.0f);
    EXPECT_EQ(power, 0.0f) << "Zero k should result in zero power";

    power = attenToPower_mW(0.0f, P_sat, 0.0f);
    EXPECT_EQ(power, 0.0f);

    power = attenToPower_mW(31.5f, P_sat, 0.0f);
    EXPECT_EQ(power, 0.0f);

    // Very small positive k
    power = attenToPower_mW(10.0f, P_sat, 0.001f);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, P_sat);
    EXPECT_LT(power, 100.0f) << "Very small k should give very low power";

    // Infinity k - tanh(inf) = 1
    power = attenToPower_mW(10.0f, P_sat, INFINITY);
    EXPECT_NEAR(power, P_sat, 1.0f) << "Infinite k: tanh(inf) = 1, so power approaches P_sat";
}

/**
 * Test attenToPower_mW with combinations of invalid parameters
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_InvalidParameterCombinations) {
    float32_t power;

    // All zero parameters
    power = attenToPower_mW(0.0f, 0.0f, 0.0f);
    EXPECT_EQ(power, 0.0f) << "All zero parameters should give zero power";

    // Negative attenuation with negative P_sat - both invalid, returns 0
    power = attenToPower_mW(-10.0f, -14680.0f, 16.2f);
    EXPECT_EQ(power, 0.0f) << "Negative attenuation should return 0 (checked first)";

    // Negative attenuation with negative k - attenuation check happens first, returns 0
    power = attenToPower_mW(-10.0f, 14680.0f, -16.2f);
    EXPECT_EQ(power, 0.0f) << "Negative attenuation should return 0";

    // Very large attenuation with zero k
    power = attenToPower_mW(1000.0f, 14680.0f, 0.0f);
    EXPECT_EQ(power, 0.0f) << "Zero k always gives zero power regardless of attenuation";

    // NaN inputs - should propagate NaN
    power = attenToPower_mW(NAN, 14680.0f, 16.2f);
    EXPECT_TRUE(std::isnan(power)) << "NaN attenuation should result in NaN power";

    power = attenToPower_mW(10.0f, NAN, 16.2f);
    EXPECT_TRUE(std::isnan(power)) << "NaN P_sat should result in NaN power";

    power = attenToPower_mW(10.0f, 14680.0f, NAN);
    EXPECT_TRUE(std::isnan(power)) << "NaN k should result in NaN power";
}

/**
 * Test attenToPower_mW output sanity checks for invalid inputs
 */
TEST_F(PowerCalibrationTest, attenToPower_mW_InvalidInputsSanityCheck) {
    float32_t power;

    // Document expected behavior with input validation
    // Function now validates att_dB >= 0 and P_sat_mW >= 0

    // Negative attenuation - now returns 0 (input validation)
    power = attenToPower_mW(-5.0f, 14680.0f, 16.2f);
    EXPECT_EQ(power, 0.0f) << "Negative attenuation returns 0";
    EXPECT_FALSE(std::isnan(power)) << "Should not produce NaN";
    EXPECT_FALSE(std::isinf(power)) << "Should not produce infinity";

    // Attenuation > 31.5 dB is still mathematically valid (positive attenuation)
    power = attenToPower_mW(60.0f, 14680.0f, 16.2f);
    EXPECT_FALSE(std::isnan(power)) << "High attenuation should not produce NaN";
    EXPECT_FALSE(std::isinf(power)) << "High attenuation should not produce infinity";
    EXPECT_GT(power, 0.0f) << "High attenuation should still give positive power";

    // Very small k is mathematically valid
    power = attenToPower_mW(10.0f, 14680.0f, 0.01f);
    EXPECT_FALSE(std::isnan(power));
    EXPECT_FALSE(std::isinf(power));
    EXPECT_GT(power, 0.0f);

    // Very large k is mathematically valid
    power = attenToPower_mW(10.0f, 14680.0f, 1000.0f);
    EXPECT_FALSE(std::isnan(power));
    EXPECT_FALSE(std::isinf(power));
    EXPECT_GT(power, 0.0f);

    // Negative P_sat - now returns 0 (input validation)
    power = attenToPower_mW(10.0f, -14680.0f, 16.2f);
    EXPECT_EQ(power, 0.0f) << "Negative P_sat returns 0";
    EXPECT_FALSE(std::isnan(power));
    EXPECT_FALSE(std::isinf(power));
}

// ============================================================================
// CalculateCWPowerLevel Tests (wrapper function)
// ============================================================================

/**
 * Test that CalculateCWPowerLevel returns power within valid range
 * Power should be > 0 and <= P_sat for all valid attenuation values
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_PowerLimits) {
    float32_t power;

    // Test 20W PA scenarios (P_sat = 14.68W = 14680 mW for 20M band)

    // Minimum attenuation (0 dB) - should give maximum power (close to P_sat)
    power = CalculateCWPowerLevel(0.0f, 0);
    EXPECT_GT(power, 0.0f) << "Power should be positive at 0 dB attenuation";
    EXPECT_LE(power, 14680.0f) << "Power should not exceed P_sat at 0 dB";
    EXPECT_GT(power, 14000.0f) << "Power at 0 dB should be close to P_sat (14680 mW)";

    // Low attenuation
    power = CalculateCWPowerLevel(3.0f, 0);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 14680.0f);

    // Mid-range attenuation
    power = CalculateCWPowerLevel(15.0f, 0);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 14680.0f);

    // High attenuation
    power = CalculateCWPowerLevel(25.0f, 0);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 14680.0f);

    // Maximum attenuation (31.5 dB) - should give very low power but still positive
    power = CalculateCWPowerLevel(31.5f, 0);
    EXPECT_GT(power, 0.0f) << "Power should still be positive at 31.5 dB";
    EXPECT_LE(power, 14680.0f);
    EXPECT_LT(power, 200.0f) << "Power at 31.5 dB should be very low (~168 mW)";

    // Test 100W PA scenarios (P_sat = 86W = 86000 mW for 20M band)

    // Minimum attenuation (0 dB)
    power = CalculateCWPowerLevel(0.0f, 1);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 86000.0f) << "Power should not exceed P_sat";
    EXPECT_GT(power, 80000.0f) << "Power at 0 dB should be close to P_sat (86000 mW)";

    // Low attenuation
    power = CalculateCWPowerLevel(3.0f, 1);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 86000.0f);

    // Mid-range attenuation
    power = CalculateCWPowerLevel(15.0f, 1);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 86000.0f);

    // High attenuation
    power = CalculateCWPowerLevel(25.0f, 1);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 86000.0f);

    // Maximum attenuation (31.5 dB)
    power = CalculateCWPowerLevel(31.5f, 1);
    EXPECT_GT(power, 0.0f);
    EXPECT_LE(power, 86000.0f);
    EXPECT_LT(power, 650.0f) << "Power at 31.5 dB should be very low (~609 mW)";
}

/**
 * Test CalculateCWPowerLevel with sweep through attenuation range
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_PowerLimits_Sweep) {
    float32_t power;

    // Sweep through attenuation values for 20W PA (0 to 31.5 dB in 0.5 dB steps)
    for (float32_t atten = 0.0f; atten <= 31.5f; atten += 0.5f) {
        power = CalculateCWPowerLevel(atten, 0);
        EXPECT_GT(power, 0.0f) << "Power should be positive at attenuation " << atten << " dB";
        EXPECT_LE(power, 14680.0f) << "Power should not exceed P_sat at attenuation " << atten << " dB";
        EXPECT_FALSE(std::isnan(power)) << "Power is NaN at attenuation " << atten << " dB";
        EXPECT_FALSE(std::isinf(power)) << "Power is infinite at attenuation " << atten << " dB";
    }

    // Sweep through attenuation values for 100W PA (0 to 31.5 dB in 1 dB steps)
    for (float32_t atten = 0.0f; atten <= 31.5f; atten += 1.0f) {
        power = CalculateCWPowerLevel(atten, 1);
        EXPECT_GT(power, 0.0f) << "Power should be positive at attenuation " << atten << " dB";
        EXPECT_LE(power, 86000.0f) << "Power should not exceed P_sat at attenuation " << atten << " dB";
        EXPECT_FALSE(std::isnan(power)) << "Power is NaN at attenuation " << atten << " dB";
        EXPECT_FALSE(std::isinf(power)) << "Power is infinite at attenuation " << atten << " dB";
    }
}

/**
 * Test edge cases at exactly 0 dB and 31.5 dB boundaries for power calculation
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_BoundaryConditions) {
    float32_t power;

    // For 20W PA: Test that 0 dB gives power close to P_sat
    power = CalculateCWPowerLevel(0.0f, 0);
    EXPECT_NEAR(power, 14680.0f, 100.0f) << "Power at 0 dB should be close to P_sat";

    // For 100W PA: Test that 0 dB gives power close to P_sat
    power = CalculateCWPowerLevel(0.0f, 1);
    EXPECT_NEAR(power, 86000.0f, 1000.0f) << "Power at 0 dB should be close to P_sat";

    // Test maximum attenuation (31.5 dB) gives very low but positive power
    power = CalculateCWPowerLevel(31.5f, 0);
    EXPECT_GT(power, 0.0f) << "Power at 31.5 dB should still be positive";
    EXPECT_LT(power, 200.0f) << "Power at 31.5 dB should be very low (20W PA: ~168 mW)";

    power = CalculateCWPowerLevel(31.5f, 1);
    EXPECT_GT(power, 0.0f) << "Power at 31.5 dB should still be positive";
    EXPECT_LT(power, 650.0f) << "Power at 31.5 dB should be very low (100W PA: ~609 mW)";

    // Test the physical model: at very high attenuation, power approaches 0
    // but never reaches exactly 0 due to tanh function asymptotic behavior
    power = CalculateCWPowerLevel(31.5f, 0);
    EXPECT_GT(power, 0.0f);

    power = CalculateCWPowerLevel(31.5f, 1);
    EXPECT_GT(power, 0.0f);
}

/**
 * Test monotonicity: increasing attenuation should result in decreasing power
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_Monotonicity) {
    // Test for 20W PA
    float32_t prev_power = 15000.0f; // Start with value higher than any possible output
    for (float32_t atten = 0.0f; atten <= 31.0f; atten += 1.0f) {
        float32_t power = CalculateCWPowerLevel(atten, 0);
        EXPECT_LE(power, prev_power)
            << "Power should decrease (or stay same) as attenuation increases: "
            << "atten=" << atten << " dB, power=" << power << " mW, prev=" << prev_power << " mW";
        prev_power = power;
    }

    // Test for 100W PA
    prev_power = 90000.0f; // Start with value higher than any possible output
    for (float32_t atten = 0.0f; atten <= 31.0f; atten += 2.0f) {
        float32_t power = CalculateCWPowerLevel(atten, 1);
        EXPECT_LE(power, prev_power)
            << "Power should decrease (or stay same) as attenuation increases: "
            << "atten=" << atten << " dB, power=" << power << " mW, prev=" << prev_power << " mW";
        prev_power = power;
    }
}

/**
 * Test that invalid attenuation values are handled correctly
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_InvalidAttenuation) {
    float32_t power;

    // Negative attenuation should return 0
    power = CalculateCWPowerLevel(-1.0f, 0);
    EXPECT_EQ(power, 0.0f) << "Negative attenuation should return 0";

    power = CalculateCWPowerLevel(-10.0f, 1);
    EXPECT_EQ(power, 0.0f) << "Negative attenuation should return 0";

    // Attenuation > 31.5 dB should return 0
    power = CalculateCWPowerLevel(32.0f, 0);
    EXPECT_EQ(power, 0.0f) << "Attenuation > 31.5 dB should return 0";

    power = CalculateCWPowerLevel(40.0f, 1);
    EXPECT_EQ(power, 0.0f) << "Attenuation > 31.5 dB should return 0";

    power = CalculateCWPowerLevel(100.0f, 0);
    EXPECT_EQ(power, 0.0f) << "Attenuation > 31.5 dB should return 0";
}

/**
 * Test that power output never exceeds P_sat for any valid attenuation
 */
TEST_F(PowerCalibrationTest, CalculateCWPowerLevel_NeverExceedsPsat) {
    float32_t power;

    // For 20W PA: P_sat = 14680 mW
    for (float32_t atten = 0.0f; atten <= 31.5f; atten += 0.1f) {
        power = CalculateCWPowerLevel(atten, 0);
        EXPECT_LE(power, 14680.0f)
            << "Power (" << power << " mW) exceeds P_sat at attenuation " << atten << " dB";
    }

    // For 100W PA: P_sat = 86000 mW
    for (float32_t atten = 0.0f; atten <= 31.5f; atten += 0.5f) {
        power = CalculateCWPowerLevel(atten, 1);
        EXPECT_LE(power, 86000.0f)
            << "Power (" << power << " mW) exceeds P_sat at attenuation " << atten << " dB";
    }
}

/**
 * Test CalculateSSBTXGain with 20W PA at various power levels
 */
TEST_F(PowerCalibrationTest, CalculateSSBTXGain_20W_VariousPowers) {
    bool PAsel;

    // Test at calibration point (should give 0 dB gain)
    ED.PA100Wactive = false;
    float32_t gain_cal = CalculateSSBTXGain(SSB_20W_CAL_POWER_POINT_W, &PAsel);
    EXPECT_FALSE(PAsel);  // Should select 20W PA
    EXPECT_NEAR(gain_cal, 0.0f, 0.01f);  // Should be 0 dB at calibration point

    // Test at 1W (below calibration point)
    float32_t gain_1W = CalculateSSBTXGain(1.0f, &PAsel);
    EXPECT_FALSE(PAsel);  // Should select 20W PA
    EXPECT_LT(gain_1W, 0.0f);  // Negative gain (attenuation)

    // Test at 5W (above calibration point for 20W, if cal point is lower)
    float32_t gain_5W = CalculateSSBTXGain(5.0f, &PAsel);
    EXPECT_FALSE(PAsel);  // Should select 20W PA

    // Higher power should require more gain
    EXPECT_GT(gain_5W, gain_1W);
}

/**
 * Test CalculateSSBTXGain with 100W PA at various power levels
 */
TEST_F(PowerCalibrationTest, CalculateSSBTXGain_100W_VariousPowers) {
    bool PAsel;

    // Test at calibration point (should give 0 dB gain)
    ED.PA100Wactive = true;
    float32_t gain_cal = CalculateSSBTXGain(SSB_100W_CAL_POWER_POINT_W, &PAsel);
    EXPECT_TRUE(PAsel);  // Should select 100W PA
    EXPECT_NEAR(gain_cal, 0.0f, 0.01f);  // Should be 0 dB at calibration point

    // Test at 15W (should use 100W PA)
    float32_t gain_15W = CalculateSSBTXGain(15.0f, &PAsel);
    EXPECT_TRUE(PAsel);  // Should select 100W PA

    // Test at 50W (should use 100W PA)
    float32_t gain_50W = CalculateSSBTXGain(50.0f, &PAsel);
    EXPECT_TRUE(PAsel);  // Should select 100W PA

    // Higher power should require more gain
    EXPECT_GT(gain_50W, gain_15W);
}

/**
 * Test CalculateSSBTXGain with invalid inputs
 */
TEST_F(PowerCalibrationTest, CalculateSSBTXGain_InvalidInputs) {
    bool PAsel;

    // Negative power should return 0
    float32_t result = CalculateSSBTXGain(-5.0f, &PAsel);
    EXPECT_EQ(result, 0.0f);

    // Power > 100W should return 0
    result = CalculateSSBTXGain(150.0f, &PAsel);
    EXPECT_EQ(result, 0.0f);
}

/**
 * Test CalculateSSBTXGain gain is proportional to log10(Power)
 */
TEST_F(PowerCalibrationTest, CalculateSSBTXGain_LogarithmicRelationship) {
    bool PAsel;
    ED.PA100Wactive = false;

    // Doubling power should add ~3 dB (10*log10(2) ≈ 3.01 dB)
    float32_t gain_2W = CalculateSSBTXGain(2.0f, &PAsel);
    float32_t gain_4W = CalculateSSBTXGain(4.0f, &PAsel);
    float32_t gain_8W = CalculateSSBTXGain(8.0f, &PAsel);

    // Check that doubling power adds approximately 3 dB
    EXPECT_NEAR(gain_4W - gain_2W, 3.01f, 0.1f);
    EXPECT_NEAR(gain_8W - gain_4W, 3.01f, 0.1f);

    // 10x power increase should add 10 dB
    float32_t gain_1W = CalculateSSBTXGain(1.0f, &PAsel);
    EXPECT_NEAR(gain_8W - gain_1W, 10.0f * log10f(8.0f), 0.1f);
}

/**
 * Test FitPowerCurve with synthetic data
 */
TEST_F(PowerCalibrationTest, FitPowerCurve_SyntheticData) {
    // Generate synthetic data from known parameters
    float32_t P_sat_true = 15000.0f;  // mW
    float32_t k_true = 16.0f;

    const int32_t N = 5;
    float32_t att_dB[N] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f};
    float32_t pout_mW[N];

    // Generate power values using the true parameters
    for (int32_t i = 0; i < N; i++) {
        pout_mW[i] = P_sat_true * tanh(k_true * powf(10.0f, -att_dB[i] / 10.0f));
    }

    // Fit the curve
    FitResult result = FitPowerCurve(att_dB, pout_mW, N, 14000.0f, 15.0f);

    // Check that fitted parameters are close to true values
    EXPECT_NEAR(result.P_sat, P_sat_true, 100.0f);  // Within 100 mW
    EXPECT_NEAR(result.k, k_true, 0.5f);  // Within 0.5

    // Check that RMS error is small
    EXPECT_LT(result.rms_error, 10.0f);  // Less than 10 mW RMS error

    // Check that iterations converged
    EXPECT_LT(result.iterations, 100);
}

/**
 * Test FitPowerCurve with noisy data
 */
TEST_F(PowerCalibrationTest, FitPowerCurve_NoisyData) {
    // Generate data with some noise
    float32_t P_sat_true = 86000.0f;  // mW
    float32_t k_true = 10.0f;

    const int32_t N = 6;
    float32_t att_dB[N] = {0.0f, 3.0f, 6.0f, 10.0f, 15.0f, 20.0f};
    float32_t pout_mW[N];

    // Generate power values with 5% noise
    for (int32_t i = 0; i < N; i++) {
        float32_t true_power = P_sat_true * tanh(k_true * powf(10.0f, -att_dB[i] / 10.0f));
        pout_mW[i] = true_power * (1.0f + 0.05f * sinf((float32_t)i));  // Add 5% periodic noise
    }

    // Fit the curve
    FitResult result = FitPowerCurve(att_dB, pout_mW, N, 85000.0f, 9.5f);

    // Check that fitted parameters are reasonably close despite noise
    EXPECT_NEAR(result.P_sat, P_sat_true, 5000.0f);  // Within 5W
    EXPECT_NEAR(result.k, k_true, 1.0f);  // Within 1.0

    // Check that iterations converged (allow up to max iterations)
    EXPECT_LE(result.iterations, 100);
}

/**
 * Test FitPowerCurve convergence with different initial guesses
 */
TEST_F(PowerCalibrationTest, FitPowerCurve_DifferentInitialGuesses) {
    // Use the same synthetic data
    float32_t P_sat_true = 14680.0f;
    float32_t k_true = 16.2f;

    const int32_t N = 4;
    float32_t att_dB[N] = {0.0f, 10.0f, 20.0f, 30.0f};
    float32_t pout_mW[N];

    for (int32_t i = 0; i < N; i++) {
        pout_mW[i] = P_sat_true * tanh(k_true * powf(10.0f, -att_dB[i] / 10.0f));
    }

    // Try different initial guesses
    FitResult result1 = FitPowerCurve(att_dB, pout_mW, N, 10000.0f, 10.0f);
    FitResult result2 = FitPowerCurve(att_dB, pout_mW, N, 20000.0f, 20.0f);

    // Both should converge to similar values
    EXPECT_NEAR(result1.P_sat, result2.P_sat, 100.0f);
    EXPECT_NEAR(result1.k, result2.k, 0.5f);

    // Both should be close to true values
    EXPECT_NEAR(result1.P_sat, P_sat_true, 100.0f);
    EXPECT_NEAR(result1.k, k_true, 0.5f);
}

/**
 * FitPowerCurve on radio 2's level 2 sweeps (2026-11-02, autocal_r2_level2c):
 * the detector's 12-point sweep read through the band's new forward line, with
 * the wattmeter's ceiling at 0 dB. The plain tanh fitted in mW missed these by up
 * to 1.2 dB (160 m), 1.6 dB (40 m) and 2.6 dB (6 m), and always ran to 100
 * iterations; fitted in dB, by 0.69, 0.59 and 1.31 dB. With the shape exponent
 * the best fit (scipy, code/tools/swr/pa_curve_fits.ipynb) misses them by 0.49,
 * 0.22 and 0.74 dB.
 */
TEST_F(PowerCalibrationTest, FitPowerCurve_FitsRadio2sSweepsWithTheShape) {
    const int32_t N = 12;
    float32_t att_dB[N] = {0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 31.5f};
    struct Sweep { const char *band; float32_t P_mW[N]; float32_t Psat, k, shape, worst_dB, rms_dB; };
    Sweep sweeps[] = {
        {"160m", {13176.78f, 12145.47f, 11540.72f, 10976.56f, 10031.12f, 8937.56f, 7130.86f, 4142.34f,
                  2166.4f, 1166.42f, 567.03f, 443.94f}, 11775.3f, 52.011f, 0.5754f, 0.488f, 0.255f},
        {"40m",  {16983.51f, 16188.73f, 15836.31f, 15248.9f, 12428.17f, 8829.58f, 5085.15f, 2814.62f,
                  1555.84f, 805.94f, 424.38f, 306.3f}, 16636.7f, 25.313f, 0.5077f, 0.215f, 0.122f},
        {"6m",   {1866.43f, 1754.95f, 1268.01f, 886.21f, 701.84f, 437.88f, 224.18f, 117.23f, 51.27f,
                  24.55f, 11.62f, 7.93f}, 1778.3f, 7.387f, 0.5489f, 0.740f, 0.501f},
    };
    for (const Sweep &w : sweeps){
        SCOPED_TRACE(w.band);
        FitResult fit = FitPowerCurve(att_dB, (float32_t *)w.P_mW, N, 15000.0f, 6.0f);
        EXPECT_LT(fit.iterations, 100) << "converged";
        // The least-squares optimum, as scipy finds it
        EXPECT_NEAR(fit.P_sat, w.Psat, 0.002f * w.Psat);
        EXPECT_NEAR(fit.k, w.k, 0.005f * w.k);
        EXPECT_NEAR(fit.shape, w.shape, 0.002f);
        float32_t worst = 0, sumsq = 0;
        for (int32_t i = 0; i < N; i++){
            float32_t e = 10.0f * log10f(attenToPower_mW(att_dB[i], fit.P_sat, fit.k, fit.shape) / w.P_mW[i]);
            worst = fmaxf(worst, fabsf(e));
            sumsq += e * e;
        }
        EXPECT_NEAR(worst, w.worst_dB, 0.005f);
        EXPECT_NEAR(fit.rms_dB, w.rms_dB, 0.002f);
        EXPECT_NEAR(fit.rms_dB, sqrtf(sumsq / N), 1e-3f) << "rms_dB reports the dB residuals";
        // The inverse finds the attenuation for each fitted power again
        for (int32_t i = 1; i < N; i++){
            float32_t P = attenToPower_mW(att_dB[i], fit.P_sat, fit.k, fit.shape);
            EXPECT_NEAR(powerToAtten_dB(P, fit.P_sat, fit.k, fit.shape), att_dB[i], 0.01f) << "point " << i;
        }
    }
}

/** The manual calibration's three points hold the shape at 1: the plain tanh */
TEST_F(PowerCalibrationTest, FitPowerCurve_ThreePointsKeepThePlainTanh) {
    float32_t att_dB[3] = {3.0f, 16.5f, 24.0f};
    float32_t pout_mW[3] = {14791.08f, 5248.07f, 1047.13f};
    FitResult fit = FitPowerCurve(att_dB, pout_mW, 3, 15000.0f, 6.0f);
    EXPECT_FLOAT_EQ(fit.shape, 1.0f);
    EXPECT_NEAR(fit.P_sat, 14743.0f, 50.0f);
    EXPECT_NEAR(fit.k, 17.30f, 0.05f);
}

/** A shape of 0 (a configuration saved before the shape existed) is the plain tanh */
TEST_F(PowerCalibrationTest, PowerCurveShapeZeroIsThePlainTanh) {
    for (float32_t att : {0.0f, 6.0f, 15.0f, 31.5f}){
        EXPECT_FLOAT_EQ(attenToPower_mW(att, 14680.0f, 16.2f, 0.0f), attenToPower_mW(att, 14680.0f, 16.2f, 1.0f));
        float32_t P = attenToPower_mW(att, 14680.0f, 16.2f, 1.0f);
        EXPECT_NEAR(14680.0f * tanhf(16.2f * powf(10.0f, -att / 10.0f)), P, 0.01f);
    }
    EXPECT_NEAR(powerToAtten_dB(5000.0f, 14680.0f, 16.2f, 0.0f), powerToAtten_dB(5000.0f, 14680.0f, 16.2f), 1e-4f);
}

/**
 * Test FitPowerCurve with real-world hardware measurements
 *
 * This test uses actual power calibration data collected from a real radio
 * across all amateur radio bands. It evaluates how well the curve fitting
 * works with real-world data that includes measurement noise and hardware
 * variations.
 *
 * For each band, we have 4 measurements:
 * - A0, P0: Attenuation and power at maximum output (0 dB attenuation)
 * - A1, P1: First measurement point (typically -6 dB from max)
 * - A2, P2: Second measurement point (typically -12 dB from max)
 * - A3, P3: Third measurement (SSB level calibration point)
 *
 * The first three points are used for curve fitting, and the fourth is
 * for SSB gain calibration.
 */
TEST_F(PowerCalibrationTest, FitPowerCurve_RealWorldData) {
    // Real-world measurement data from actual hardware
    // Format: Band name, A0, P0, A1, P1, A2, P2, A3, P3
    struct BandMeasurement {
        const char* band_name;
        float32_t att_dB[3];     // Attenuations: A0, A1, A2
        float32_t power_dBm[3];    // Powers: P0, P1, P2
        float32_t ssb_att_dB;    // A3
        float32_t ssb_power_W;   // P3
    };

    BandMeasurement measurements[] = {
        {"160m", {0.0f, 21.5f, 28.0f}, {40.7f, 34.0f, 28.0f}, 0.0f, 39.0f},
        {"80m",  {0.0f, 11.5f, 18.5f}, {40.8f, 34.3f, 27.9f}, 0.0f, 31.7f},
        {"60m",  {0.0f, 12.0f, 18.0f}, {41.7f, 34.0f, 28.1f}, 0.0f, 33.2f},
        {"40m",  {0.0f, 20.0f, 27.0f}, {41.7f, 34.0f, 27.7f}, 0.0f, 38.6f},
        {"30m",  {0.0f, 17.0f, 24.0f}, {40.2f, 34.0f, 27.7f}, 0.0f, 35.9f},
        {"20m",  {0.0f, 11.5f, 18.0f}, {39.9f, 33.9f, 28.0f}, 0.0f, 30.8f},
        {"17m",  {0.0f, 12.0f, 18.5f}, {40.6f, 33.9f, 28.0f}, 0.0f, 30.4f},
        {"15m",  {0.0f, 14.5f, 21.0f}, {39.6f, 33.9f, 28.1f}, 0.0f, 26.1f},
        {"12m",  {0.0f, 12.5f, 19.0f}, {39.7f, 33.8f, 28.2f}, 0.0f, 30.4f},
        {"10m",  {0.0f, 16.5f, 23.5f}, {37.2f, 34.0f, 28.0f}, 0.0f, 32.9f},
        {"6m",   {0.0f,  7.0f, 13.5f}, {39.0f, 34.0f, 28.0f}, 0.0f, 20.5f},
    };

    printf("\n=== Real-World Power Curve Fit Evaluation ===\n\n");
    printf("%-6s | %8s | %6s | %5s | %9s | %9s\n",
           "Band", "P_sat[mW]", "k", "Iters", "RMS[mW]", "RMS[%%]");
    printf("-------|----------|--------|-------|-----------|----------\n");

    int num_bands = sizeof(measurements) / sizeof(measurements[0]);

    // Open CSV file for output
    FILE* csv_file = fopen("power_calibration_fits.csv", "w");
    if (csv_file) {
        fprintf(csv_file, "Band,A0,P0,A1,P1,A2,P2,P_sat_mW,k,iterations,rms_error_mW,rms_percent\n");
    }

    // Store fit results for summary table
    struct FitSummary {
        const char* band_name;
        float32_t P_sat_mW;
        float32_t k;
        float32_t rms_percent;
    };
    FitSummary fit_summary[11];  // 11 bands

    for (int i = 0; i < num_bands; i++) {
        BandMeasurement& m = measurements[i];

        // Convert power from Watts to milliwatts for fitting
        float32_t power_mW[3];
        for (int j = 0; j < 3; j++) {
            power_mW[j] = pow(10.0,m.power_dBm[j]/10);
        }

        // Perform curve fit
        // Initial guesses: P_sat ~15W, k ~10 (typical for this PA)
        FitResult fit = FitPowerCurve(m.att_dB, power_mW, 3, 15000.0f, 6.0f);

        // Calculate RMS error as percentage of max power
        float32_t max_power_mW = power_mW[0];
        float32_t rms_percent = (fit.rms_error / max_power_mW) * 100.0f;

        // Store for summary
        fit_summary[i].band_name = m.band_name;
        fit_summary[i].P_sat_mW = fit.P_sat ;
        fit_summary[i].k = fit.k;
        fit_summary[i].rms_percent = rms_percent;

        // Write to CSV file
        if (csv_file) {
            fprintf(csv_file, "%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.2f\n",
                    m.band_name,
                    m.att_dB[0], power_mW[0],
                    m.att_dB[1], power_mW[1],
                    m.att_dB[2], power_mW[2],
                    fit.P_sat, fit.k, fit.iterations,
                    fit.rms_error, rms_percent);
        }

        // Print results
        printf("%-6s | %8.2f | %6.2f | %5d | %9.2f | %8.2f%%\n",
               m.band_name,
               fit.P_sat,
               fit.k,
               fit.iterations,
               fit.rms_error,
               rms_percent);

        // Basic sanity checks
        EXPECT_GT(fit.P_sat, 0.0f) << "Band " << m.band_name << ": P_sat must be positive";
        EXPECT_LT(fit.P_sat, 100000.0f) << "Band " << m.band_name << ": P_sat unreasonably high";
        EXPECT_GT(fit.k, 0.0f) << "Band " << m.band_name << ": k must be positive";

        // Validate fit quality in dB, which the fit minimises: three points and two
        // parameters, so the misses are small but not zero. Fitted in mW (before
        // 2026-11-02) the lowest point missed by up to 0.76 dB (12 m); in dB it is 0.40
        EXPECT_LT(fit.rms_dB, 0.35f) << "Band " << m.band_name;
        for (int j = 0; j < 3; j++)
            EXPECT_LT(fabsf(10.0f * log10f(attenToPower_mW(m.att_dB[j], fit.P_sat, fit.k) / power_mW[j])), 0.45f)
                << "Band " << m.band_name << " point " << j;
        EXPECT_LT(fit.iterations, 100) << "Band " << m.band_name << ": converged";
    }

    // Close CSV file
    if (csv_file) {
        fclose(csv_file);
        printf("\nData written to: power_calibration_fits.csv\n");
    }

    // Print summary table
    printf("\n=== Fit Parameters Summary ===\n\n");
    printf("%-6s | %10s | %10s | %10s\n", "Band", "P_sat[mW]", "k", "RMS[%%]");
    printf("-------|------------|------------|------------\n");
    for (int i = 0; i < num_bands; i++) {
        printf("%-6s | %10.2f | %10.2f | %9.2f%%\n",
               fit_summary[i].band_name,
               fit_summary[i].P_sat_mW,
               fit_summary[i].k,
               fit_summary[i].rms_percent);
    }
    printf("\n");
}

/**
 * Test fixture for complete power calibration walkthrough
 * Simulates a user performing the full calibration procedure across all bands
 */
class PowerCalibrationWalkthroughTest : public ::testing::Test {
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

    /**
     * Navigate to power calibration through the menu system
     * Simulates: Home -> Main Menu -> Calibration -> Power
     */
    void NavigateToPowerCalibration(void) {
        extern size_t primaryMenuIndex;
        extern size_t secondaryMenuIndex;

        // Start from home screen
        loop(); MyDelay(10);
        EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);

        // Press MAIN_MENU_UP button to enter main menu
        SetButton(MAIN_MENU_UP);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);
        EXPECT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU);

        // Navigate to "Calibration" menu (index 6 in primaryMenu)
        // Primary menu order: RF Options(0), CW Options(1), Microphone(2),
        //                     Audio Options(3), Display(4), EEPROM(5),
        //                     Calibration(6), Diagnostics(7)
        // Use FILTER encoder to scroll through main menu
        for (int i = 0; i < 6; i++) {
            SetInterrupt(iFILTER_INCREASE);
            loop(); MyDelay(10);
        }
        EXPECT_EQ(primaryMenuIndex, 6u);  // Should be on Calibration menu

        // Press MENU_OPTION_SELECT to enter the Calibration submenu
        SetButton(MENU_OPTION_SELECT);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);
        EXPECT_EQ(uiSM.state_id, UISm_StateId_SECONDARY_MENU);

        // Navigate to "Power" option (index 4 in CalOptions)
        // CalOptions order: S meter level(0), Frequency(1), Receive IQ(2),
        //                   Transmit IQ(3), Power(4)
        // Use FILTER encoder to scroll through secondary menu
        for (int i = 0; i < 4; i++) {
            SetInterrupt(iFILTER_INCREASE);
            loop(); MyDelay(10);
        }
        EXPECT_EQ(secondaryMenuIndex, 4u);  // Should be on Power option

        // Press MENU_OPTION_SELECT to trigger the Power calibration
        // This will:
        //   1. Transition to UPDATE state
        //   2. Call StartPowerCal() which queues iCALIBRATE_POWER interrupt
        //   3. Transition back to HOME state
        SetButton(MENU_OPTION_SELECT);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);

        // At this point we're back at HOME, but iCALIBRATE_POWER interrupt is queued
        // Run loop again to process the queued interrupt
        loop(); MyDelay(10);

        // Should now be in power calibration mode
        EXPECT_EQ(uiSM.state_id, UISm_StateId_CALIBRATE_POWER);
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
    }

    /**
     * Perform the 4-step power calibration for a single band
     * @param A1-A4: Attenuation values in dB for steps 1-4
     * @param P1-P4: Power values in dBm for steps 1-4
     */
    void CalibrateBand(float32_t A1, float32_t P1,
                       float32_t A2, float32_t P2,
                       float32_t A3, float32_t P3,
                       float32_t A4, float32_t P4) {
        // Verify we're starting in CALIBRATE_POWER_SPACE state
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE)
            << "Should be in CALIBRATE_POWER_SPACE state at start of calibration";

        // State machine should be in POWERPOINT1 at the start
        EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1)
            << "Should be in POWERPOINT1 state when starting calibration";

        // Step 1: Record power at 0 dB attenuation
        // Set attenuation to A1 (should be 0)
        ED.XAttenCW[ED.currentBand[ED.activeVFO]] = A1;
        SetTXAttenuation(A1);

        // Set measured power to P1 (in dBm, convert to W)
        measuredPower = pow(10.0f, P1 / 10.0f) / 1000.0f;  // Convert dBm to W

        // Press SELECT button to record point 1
        SetButton(MENU_OPTION_SELECT);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);

        EXPECT_EQ(Npoints, 1u) << "Should have recorded 1 data point after step 1";
        EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT2)
            << "Should transition to POWERPOINT2 after recording first point";

        // Step 2: Adjust attenuation to drop power by 6dB
        // Set attenuation to A2
        ED.XAttenCW[ED.currentBand[ED.activeVFO]] = A2;
        SetTXAttenuation(A2);

        // Set measured power to P2
        measuredPower = pow(10.0f, P2 / 10.0f) / 1000.0f;

        // Press SELECT button to record point 2
        SetButton(MENU_OPTION_SELECT);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);

        EXPECT_EQ(Npoints, 2u) << "Should have recorded 2 data points after step 2";
        EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT3)
            << "Should transition to POWERPOINT3 after recording second point";

        // Step 3: Adjust attenuation to drop power by another 6dB
        // Set attenuation to A3
        ED.XAttenCW[ED.currentBand[ED.activeVFO]] = A3;
        SetTXAttenuation(A3);

        // Set measured power to P3
        measuredPower = pow(10.0f, P3 / 10.0f) / 1000.0f;

        // Press SELECT button to record point 3
        SetButton(MENU_OPTION_SELECT);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);

        EXPECT_EQ(Npoints, 3u) << "Should have recorded 3 data points after step 3";
        EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERCOMPLETE)
            << "Should transition to POWERCOMPLETE and trigger curve fit after third point";

        // Curve fit should have been calculated automatically in POWERCOMPLETE state
        // Press button 12 to proceed to SSB gain calibration (step 4)
        SetButton(12);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);

        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE)
            << "Should transition to CALIBRATE_OFFSET_SPACE after button 12";

        // After transition to OFFSET_SPACE, state machine should be in SSBPOINT
        EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_SSBPOINT)
            << "Should be in SSBPOINT state after CURVE_COMPLETE event (button 12)";

        // Step 4: Record measured power for SSB calibration
        // Set attenuation to A4 (should be 0)
        ED.XAttenCW[ED.currentBand[ED.activeVFO]] = A4;
        SetTXAttenuation(A4);

        // Set measured power to P4
        measuredPower = pow(10.0f, P4 / 10.0f) / 1000.0f;

        // Press SELECT button to record SSB calibration point
        SetButton(MENU_OPTION_SELECT);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);

        // State machine should transition to MEASUREMENTCOMPLETE
        EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_MEASUREMENTCOMPLETE)
            << "Should transition to MEASUREMENTCOMPLETE after recording SSB point";
    }

    /**
     * Change to the next band (band up)
     */
    void ChangeBandUp(void) {
        SetButton(BAND_UP);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);

        // The button handler triggers state machine reset and transitions back to start
        // Verify we're back in POWER_SPACE with state machine reset
        EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE)
            << "Should be in POWER_SPACE after band change";
        EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1)
            << "State machine should reset to POWERPOINT1 after band change";
        EXPECT_EQ(Npoints, 0u)
            << "Npoints should be reset to 0 after band change";
    }
};

/**
 * Complete power calibration walkthrough test
 *
 * This test simulates a user performing the full power calibration routine
 * for all amateur radio bands from 160m through 6m. It follows the exact
 * procedure a user would follow, including:
 * - Navigating to the power calibration screen from the home screen
 * - Performing the 4-step calibration procedure for each band
 * - Recording attenuation and power measurements at each step
 * - Advancing through bands using the band up button
 *
 * The test validates that calibration values are correctly stored and that
 * the curve fitting produces reasonable results for each band.
 */
TEST_F(PowerCalibrationWalkthroughTest, CompleteCalibrationAllBands) {
    // Test data: Band, A1, P1, A2, P2, A3, P3, A4, P4, Expected P_sat, Expected k
    struct BandCalData {
        int bandIndex;
        const char* bandName;
        float32_t A1, P1, A2, P2, A3, P3, A4, P4;
        float32_t expected_Psat_mW;  // Expected P_sat from curve fit
        float32_t expected_k;        // Expected k from curve fit
    };

    // P_sat and k: the least-squares tanh fit in dB (FitPowerCurve, 2026-11-02; scipy agrees)
    BandCalData calData[] = {
        {BAND_160M, "160m", 0.0f, 40.7f, 21.5f, 34.0f, 28.0f, 28.0f, 0.0f, 39.0f, 11730.0f, 32.36f},
        {BAND_80M,  "80m",  0.0f, 40.8f, 11.5f, 34.3f, 18.5f, 27.9f, 0.0f, 31.7f, 12021.0f,  3.43f},
        {BAND_60M,  "60m",  0.0f, 41.7f, 12.0f, 34.0f, 18.0f, 28.1f, 0.0f, 33.2f, 14920.0f,  2.71f},
        {BAND_40M,  "40m",  0.0f, 41.7f, 20.0f, 34.0f, 27.0f, 27.7f, 0.0f, 38.6f, 14767.0f, 18.56f},
        {BAND_30M,  "30m",  0.0f, 40.2f, 17.0f, 34.0f, 24.0f, 27.7f, 0.0f, 35.9f, 10439.0f, 13.25f},
        {BAND_20M,  "20m",  0.0f, 39.9f, 11.5f, 33.9f, 18.0f, 28.0f, 0.0f, 30.8f, 9755.0f,  3.86f},
        {BAND_17M,  "17m",  0.0f, 40.6f, 12.0f, 33.9f, 18.5f, 28.0f, 0.0f, 30.4f, 11473.0f,  3.67f},
        {BAND_15M,  "15m",  0.0f, 39.6f, 14.5f, 33.9f, 21.0f, 28.1f, 0.0f, 26.1f, 9086.0f,  8.40f},
        {BAND_12M,  "12m",  0.0f, 39.7f, 12.5f, 33.8f, 19.0f, 28.2f, 0.0f, 30.4f, 9288.0f,  5.18f},
        {BAND_10M,  "10m",  0.0f, 37.2f, 16.5f, 34.0f, 23.5f, 28.0f, 0.0f, 32.9f, 5178.0f, 25.83f},
        {BAND_6M,   "6m",   0.0f, 39.0f,  7.0f, 34.0f, 13.5f, 28.0f, 0.0f, 20.5f, 8622.0f,  1.58f},
    };

    // Set starting band to 160m
    ED.currentBand[ED.activeVFO] = BAND_160M;

    // Navigate to power calibration screen
    NavigateToPowerCalibration();

    printf("\n=== Power Calibration Walkthrough Test ===\n\n");
    printf("Simulating user calibration procedure for all bands...\n\n");

    // Calibrate each band
    for (size_t i = 0; i < sizeof(calData) / sizeof(calData[0]); i++) {
        BandCalData& data = calData[i];

        printf("Calibrating %s band...\n", data.bandName);

        // Verify we're on the correct band
        EXPECT_EQ(ED.currentBand[ED.activeVFO], data.bandIndex)
            << "Expected to be on band " << data.bandName;

        // Perform 4-step calibration
        CalibrateBand(data.A1, data.P1, data.A2, data.P2,
                     data.A3, data.P3, data.A4, data.P4);

        // Verify calibration values were stored
        EXPECT_GT(ED.PowerCal_20W_Psat_mW[data.bandIndex], 0.0f)
            << "P_sat not set for " << data.bandName;
        EXPECT_GT(ED.PowerCal_20W_kindex[data.bandIndex], 0.0f)
            << "k index not set for " << data.bandName;

        // Verify calibration values match expected results from curve fit
        // Use 1% tolerance for P_sat (allows for numerical precision)
        EXPECT_NEAR(ED.PowerCal_20W_Psat_mW[data.bandIndex], data.expected_Psat_mW,
                    data.expected_Psat_mW * 0.01f)
            << "P_sat mismatch for " << data.bandName
            << ": expected " << data.expected_Psat_mW
            << " mW, got " << ED.PowerCal_20W_Psat_mW[data.bandIndex] << " mW";

        // Use 1% tolerance for k index
        EXPECT_NEAR(ED.PowerCal_20W_kindex[data.bandIndex], data.expected_k,
                    data.expected_k * 0.01f)
            << "k index mismatch for " << data.bandName
            << ": expected " << data.expected_k
            << ", got " << ED.PowerCal_20W_kindex[data.bandIndex];

        printf("  P_sat = %.2f mW (expected %.2f), k = %.2f (expected %.2f), Gain_corr = %.2f dB\n",
               ED.PowerCal_20W_Psat_mW[data.bandIndex],
               data.expected_Psat_mW,
               ED.PowerCal_20W_kindex[data.bandIndex],
               data.expected_k,
               ED.PowerCal_20W_DSP_Gain_correction_dB[data.bandIndex]);

        // Move to next band (except for the last one)
        if (i < sizeof(calData) / sizeof(calData[0]) - 1) {
            ChangeBandUp();
            loop(); MyDelay(10);
        }
    }

    printf("\nCalibration complete for all bands.\n");

    // Exit calibration and return to home screen
    // The HOME_SCREEN button triggers iCALIBRATE_EXIT which performs the full exit sequence
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(50);  // Give extra time for state transitions

    // Consume the interrupt that was queued
    loop(); MyDelay(10);

    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    printf("\nReturned to home screen.\n");

    // Final verification: Check all bands have correct calibration values
    printf("\n=== Final Calibration Validation ===\n\n");
    printf("%-6s | %10s | %10s | %10s | %10s\n",
           "Band", "P_sat[mW]", "Expected", "k", "Expected");
    printf("-------|------------|------------|------------|------------\n");

    for (size_t i = 0; i < sizeof(calData) / sizeof(calData[0]); i++) {
        BandCalData& data = calData[i];

        printf("%-6s | %10.2f | %10.2f | %10.2f | %10.2f\n",
               data.bandName,
               ED.PowerCal_20W_Psat_mW[data.bandIndex],
               data.expected_Psat_mW,
               ED.PowerCal_20W_kindex[data.bandIndex],
               data.expected_k);

        // Final validation that all parameters are within tolerance
        EXPECT_NEAR(ED.PowerCal_20W_Psat_mW[data.bandIndex], data.expected_Psat_mW,
                    data.expected_Psat_mW * 0.01f)
            << "Final check: P_sat out of tolerance for " << data.bandName;

        EXPECT_NEAR(ED.PowerCal_20W_kindex[data.bandIndex], data.expected_k,
                    data.expected_k * 0.01f)
            << "Final check: k out of tolerance for " << data.bandName;

        // Verify DSP gain correction was also set (not checking exact value as it depends on SSB cal point)
        EXPECT_NE(ED.PowerCal_20W_DSP_Gain_correction_dB[data.bandIndex], -3.0f)
            << "Final check: DSP gain correction not updated for " << data.bandName;
    }

    printf("\nAll calibration parameters validated successfully.\n");
    printf("\n=== Calibration Walkthrough Test Complete ===\n\n");
}

/**
 * Test that re-measuring data points correctly resets state machine
 *
 * This test verifies that when a user presses ZOOM to restart measurements,
 * the state machine properly resets to POWERPOINT1 and allows re-measurement
 * without skipping states or advancing incorrectly.
 */
TEST_F(PowerCalibrationWalkthroughTest, RemeasuringDataPoints_StateRemainsCorrect) {
    // Navigate to power calibration
    NavigateToPowerCalibration();

    printf("\n=== Testing Re-measurement of Data Points ===\n\n");

    // Verify initial state
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    EXPECT_EQ(Npoints, 0u);

    printf("Initial state: PowerSM=%s, Npoints=%u\n",
           PowerCalSm_state_id_to_string(powerSM.state_id), Npoints);

    // Record first data point
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 0.0f;
    SetTXAttenuation(0.0f);
    measuredPower = 10.0f;  // 10W

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    EXPECT_EQ(Npoints, 1u);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT2);
    printf("After 1st measurement: PowerSM=%s, Npoints=%u\n",
           PowerCalSm_state_id_to_string(powerSM.state_id), Npoints);

    // Record second data point
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 6.0f;
    SetTXAttenuation(6.0f);
    measuredPower = 2.5f;  // ~6dB down

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    EXPECT_EQ(Npoints, 2u);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT3);
    printf("After 2nd measurement: PowerSM=%s, Npoints=%u\n",
           PowerCalSm_state_id_to_string(powerSM.state_id), Npoints);

    // Now press ZOOM to reset and re-measure (simulating user wanting to redo)
    printf("\nPressing ZOOM to reset measurements...\n");
    SetButton(ZOOM);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should be back at POWERPOINT1
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1)
        << "State machine should reset to POWERPOINT1 after ZOOM";
    EXPECT_EQ(Npoints, 0u) << "Npoints should reset to 0";
    printf("After ZOOM reset: PowerSM=%s, Npoints=%u\n",
           PowerCalSm_state_id_to_string(powerSM.state_id), Npoints);

    // Re-record first point
    measuredPower = 10.1f;  // Slightly different measurement
    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    // Should advance to POWERPOINT2, not skip ahead
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT2)
        << "State machine should advance to POWERPOINT2 after first re-measurement";
    EXPECT_EQ(Npoints, 1u);
    printf("After re-measurement: PowerSM=%s, Npoints=%u\n",
           PowerCalSm_state_id_to_string(powerSM.state_id), Npoints);

    // Re-measure second point
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 6.0f;
    SetTXAttenuation(6.0f);
    measuredPower = 2.6f;

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT3)
        << "State machine should advance to POWERPOINT3 after second re-measurement";
    EXPECT_EQ(Npoints, 2u);
    printf("After 2nd re-measurement: PowerSM=%s, Npoints=%u\n",
           PowerCalSm_state_id_to_string(powerSM.state_id), Npoints);

    // Verify we're still in POWER_SPACE and haven't skipped to SSB calibration
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE)
        << "Should still be in CALIBRATE_POWER_SPACE";

    printf("\n✓ Verified: State machine properly resets and advances through states\n");
    printf("  Re-measurement works correctly without skipping states\n\n");
}

/**
 * Test state transitions between CALIBRATE_POWER_SPACE and CALIBRATE_OFFSET_SPACE
 *
 * This test verifies the state machine behavior during mode transitions:
 * 1. CURVE_COMPLETE event (Button 12) transitions from POWERCOMPLETE to SSBPOINT
 * 2. Recording SSB point transitions to MEASUREMENTCOMPLETE
 * 3. RESET event returns to POWERPOINT1 from any state
 */
TEST_F(PowerCalibrationWalkthroughTest, StateTransitions_ResetBehavior) {
    // Navigate to power calibration
    NavigateToPowerCalibration();

    printf("\n=== Testing State Transitions and RESET Event ===\n\n");

    // Verify initial state
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    printf("Initial state: ModeSM=CALIBRATE_POWER_SPACE, PowerSM=%s\n",
           PowerCalSm_state_id_to_string(powerSM.state_id));

    // Record 3 data points to reach POWERCOMPLETE
    for (int i = 0; i < 3; i++) {
        ED.XAttenCW[ED.currentBand[ED.activeVFO]] = i * 6.0f;
        SetTXAttenuation(i * 6.0f);
        measuredPower = 10.0f / pow(2.0f, i * 2.0f);  // Roughly -6dB each step

        SetButton(MENU_OPTION_SELECT);
        SetInterrupt(iBUTTON_PRESSED);
        loop(); MyDelay(10);
    }

    EXPECT_EQ(Npoints, 3u);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERCOMPLETE)
        << "Should be in POWERCOMPLETE after 3 measurements";
    printf("After 3 measurements: Npoints=%u, PowerSM=%s\n",
           Npoints, PowerCalSm_state_id_to_string(powerSM.state_id));

    // Press button 12 to issue CURVE_COMPLETE event and transition to SSBPOINT
    printf("\nPressing button 12 to enter CALIBRATE_OFFSET_SPACE...\n");
    SetButton(12);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_SSBPOINT)
        << "Should transition to SSBPOINT after CURVE_COMPLETE event";
    printf("Entered CALIBRATE_OFFSET_SPACE, PowerSM=%s\n",
           PowerCalSm_state_id_to_string(powerSM.state_id));

    // Record SSB offset measurement
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 0.0f;
    SetTXAttenuation(0.0f);
    measuredPower = 3.0f;

    SetButton(MENU_OPTION_SELECT);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_MEASUREMENTCOMPLETE)
        << "Should transition to MEASUREMENTCOMPLETE after recording SSB point";
    printf("After recording offset: PowerSM=%s\n",
           PowerCalSm_state_id_to_string(powerSM.state_id));

    // Test RESET event: Press ZOOM to return to start
    printf("\nPressing ZOOM to issue RESET event...\n");
    SetButton(ZOOM);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);

    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE)
        << "Should return to CALIBRATE_POWER_SPACE after RESET";
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1)
        << "State machine should RESET to POWERPOINT1";
    EXPECT_EQ(Npoints, 0u) << "Npoints should reset to 0";
    printf("After RESET: ModeSM=CALIBRATE_POWER_SPACE, PowerSM=%s, Npoints=%u\n",
           PowerCalSm_state_id_to_string(powerSM.state_id), Npoints);

    printf("\n✓ Verified: State machine properly handles transitions and RESET event\n");
    printf("  RESET returns to POWERPOINT1 from any state\n\n");
}

/**
 * Tests of the AUTO branch of PowerCalSm: the automatic power and SWR bridge
 * calibration. These drive the state machine directly, one DO tick at a time
 * followed by the main loop's PowerCalAutoService(), instead of through the
 * 1 ms timer thread, so the paths are deterministic.
 *
 * The AUTO actions are still stubs, so these check the state machine's paths:
 * the order of the standards, the number of points per band and stage, the
 * wattmeter entries, the timeouts and the aborts.
 */
void ResetPowerCal(void);   // HardwareSm_PowerCalibration.cpp
void HandleButtonPress(int32_t button); // Loop.cpp
char *BR_write(char* cmd);  // CAT.cpp
bool AutoAnyFailed(void);   // HardwareSm_PowerCalibration.cpp
float32_t GetPowerIncrement(void); // MainBoard_DisplayCalibration_Power.cpp

class PowerCalAutoTest : public ::testing::Test {
protected:
    void SetUp() override {
        StartMillis();
        InitializeStorage();
        InitializeRFHardware();
        modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
        modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
        ModeSm_start(&modeSM);
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_CALIBRATE_POWER);
        ED.PA100Wactive = false;
        ED.SWR_CouplerTurns = 10;
        for (int32_t band = FIRST_BAND; band <= LAST_BAND; band++){
            ED.PowerCal_20W_DSP_Gain_correction_dB[band] = 0;
            ED.SWR_CalLevel[band] = 0;
        }
        autoCal.level = 1;
        InitializePowerCalibration();
    }

    /**
     * A simulated radio: the bridge's true forward and reflected lines, the PA
     * curve and the SSB gain. It answers the bridge readings and the wattmeter.
     */
    struct SimRadio {
        float32_t m_f = 24.6f;          // true forward slope, mV/dB
        float32_t o_f_excess = 0.5f;    // how far the level 1 default reads low at 1700 mV, dB
        float32_t m_r = 25.3f;          // true reflected slope, mV/dB
        float32_t o_r = -2.7f;          // true reflected offset, dB
        float32_t Psat_mW = 15000.0f;   // PA curve
        float32_t k = 6.0f;
        float32_t ssbGainFor3W_dB = 1.0f; // DSP gain that gives 3 W
        float32_t gamma50 = 0.03f;      // the load's reflection
        float32_t leak_dB = 0.15f;      // adds on the open, subtracts on the short
        int32_t oscillateBand = -1;     // this band oscillates...
        AutoCalStage oscillateStage = AUTO_STAGE_SHORT; // ...on this standard...
        bool oscillateBothStandards = false; // ...or on the open and the short
        uint32_t oscillateFromPoint = 0; // ...from this point of the sweep on...
        bool oscillateUnsteady = false; // ...as readings scattered 60 mV about the normal V_f
        mutable uint32_t scatter = 0;
        int32_t compressBand = -1;      // the PA compresses on this band...
        AutoCalStage compressStage = AUTO_STAGE_OPEN; // ...into this standard...
        float32_t compressCeiling_dBm = 99.0f; // ...at this power
        int32_t loadMissingBand = -1;   // on this band the load stage sees an open
        float32_t meterFactor = 1.0f;   // wattmeter entries are this times the truth
        bool saturationOverRead = false; // the forward detector over-reads in saturation (radio 1, 10 m)

        /** True forward intercept. Like the Section 2 defaults, defined at mid-range,
         *  1700 mV: there the defaults read o_f_excess low */
        float32_t b_f(int32_t band) const {
            return SWRCalLevel1ForwardOffset(band, 10) - SWRCAL_OFFSET_CONSTANT_DB + o_f_excess
                   + 1700.0f / 25.0f - 1700.0f / m_f;
        }
        float32_t b_r(void) const { return o_r - SWRCAL_OFFSET_CONSTANT_DB; }
        /** Forward power on the line now, in dBm */
        float32_t ForwardPower_dBm(void) const {
            float32_t P;
            if (autoCal.ssbMode)
                P = 10.0f * log10f(3000.0f) + autoCal.drive_dB - ssbGainFor3W_dB;
            else
                P = 10.0f * log10f(Psat_mW * tanhf(k * powf(10.0f, -autoCal.drive_dB / 10.0f)));
            if ((autoCal.band == compressBand) && (autoCal.stage == compressStage) && (P > compressCeiling_dBm))
                P = compressCeiling_dBm;
            return P;
        }
        bool Oscillating(void) const {
            bool stage = (autoCal.stage == oscillateStage) ||
                         (oscillateBothStandards && (autoCal.stage != AUTO_STAGE_LOAD));
            return (autoCal.band == oscillateBand) && stage && (autoCal.point >= oscillateFromPoint);
        }
        float32_t Vf(void) const {
            if (Oscillating() && oscillateUnsteady)
                return m_f * (ForwardPower_dBm() - b_f(autoCal.band)) + ((scatter++ & 1) ? 60.0f : -60.0f);
            if (Oscillating())
                return 1950.0f; // stuck whatever the drive
            return m_f * (ForwardPower_dBm() + OverRead_dB() - b_f(autoCal.band));
        }
        /** Radio 1's 10 m: the forward detector keeps rising through saturation,
         *  2.28 dB high at 0 dB (bench sheet radio1_adcfix, ten_m_ceiling/ceil10m.json) */
        float32_t OverRead_dB(void) const {
            if (!saturationOverRead || autoCal.ssbMode)
                return 0.0f;
            static const float32_t att[5] = {0, 1, 3, 6, 12};
            static const float32_t over[5] = {2.28f, 1.92f, 1.54f, 0.94f, 0.0f};
            float32_t a = autoCal.drive_dB;
            if (a >= att[4]) return 0.0f;
            int i = 0;
            while (a > att[i + 1]) i++;
            return over[i] + (over[i + 1] - over[i]) * (a - att[i]) / (att[i + 1] - att[i]);
        }
        float32_t Vr(void) const {
            float32_t Pr = ForwardPower_dBm();
            if ((autoCal.stage == AUTO_STAGE_LOAD) && (autoCal.band == loadMissingBand))
                Pr += leak_dB;  // nothing on the connector: an open
            else if (autoCal.stage == AUTO_STAGE_LOAD)
                Pr += 20.0f * log10f(gamma50);
            else
                Pr += (autoCal.stage == AUTO_STAGE_OPEN) ? leak_dB : -leak_dB;
            return m_r * (Pr - b_r());
        }
    } radio;

    /** One 1 ms tick: the timer's DO event, then the main loop's service and bridge */
    void Tick(void) {
        PowerCalSm_dispatch_event(&powerSM, PowerCalSm_EventId_DO);
        PowerCalAutoService();
        // While collecting, the main loop reads the bridge about every 16 ms; here
        // every tick adds enough readings for a point
        if (!bridgeSilent && (powerSM.state_id == PowerCalSm_StateId_COLLECT) && !autoCal.servicePending){
            for (int k = 0; k < 10; k++)
                AccumulateSWRBridgeStats(radio.Vf(), radio.Vr());
            if (failedReads)
                AccumulateSWRBridgeStats(65535.0f, 65535.0f); // how a failed I2C read arrives
        }
    }

    /** Enter AUTO and shorten its timing so a session takes a few hundred ticks */
    void EnterAuto(uint8_t level) {
        StartPowerAutoCal();
        ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_SETUP);
        powerSM.vars.bandSettle_ms = 2;
        powerSM.vars.settle_ms = 3;
        powerSM.vars.collect_ms = 4;
        powerSM.vars.rest_ms = 2;
        powerSM.vars.meterTimeout_ms = 50;
        autoCal.collectLoad_ms = 4;
        autoCal.collectReflection_ms = 4;
        autoCal.level = level;
        autoCal.bandMask = (1u << BAND_40M) | (1u << BAND_20M);
    }

    /** From SETUP through the three standards to REVIEW */
    void RunToReviewForLog(void) {
        Select();
        Select();
        ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
        Select();
        ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
        Select();
        ASSERT_TRUE(RunUntil(PowerCalSm_StateId_REVIEW));
    }

    /** Run a whole session from SETUP to REVIEW */
    void RunSession(bool skipLoad = false) {
        Select(); // SETUP -> PROMPT
        if (skipLoad)
            SkipPowerCalStage();
        else {
            Select(); // load fitted
            ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
        }
        Select(); // open fitted
        ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
        Select(); // short fitted
        ASSERT_TRUE(RunUntil(PowerCalSm_StateId_REVIEW));
    }

    /** SWR the stored calibration reads for a load with the given |Gamma| at 5 W */
    float32_t StoredSWR(int32_t band, float32_t gamma) {
        float32_t Pf = 10.0f * log10f(5000.0f);
        float32_t Vf = radio.m_f * (Pf - radio.b_f(band));
        float32_t Vr = radio.m_r * (Pf + 20.0f * log10f(gamma) - radio.b_r());
        return SWRCalSWR(Vf, Vr, 25.0f + ED.SWR_F_SlopeAdj[band], ED.SWR_F_Offset[band] - SWRCAL_OFFSET_CONSTANT_DB,
                         25.0f + ED.SWR_R_SlopeAdj[band], ED.SWR_R_Offset[band] - SWRCAL_OFFSET_CONSTANT_DB);
    }

    void Select(void) {
        PowerCalDispatch(PowerCalSm_EventId_RECORD_DATA_POINT);
    }

    /**
     * Tick until the state machine reaches target. Answers every wattmeter
     * entry on the way and counts what happened.
     * @return false if target was not reached within maxTicks
     */
    bool RunUntil(PowerCalSm_StateId target, uint32_t maxTicks = 5000) {
        PowerCalSm_StateId last = powerSM.state_id;
        for (uint32_t k = 0; k < maxTicks; k++) {
            if (powerSM.state_id == target)
                return true;
            if (powerSM.state_id == PowerCalSm_StateId_METER_ENTRY) {
                meterEntries++;
                // The operator reads the wattmeter and dials it in
                SetMeasuredPower(radio.meterFactor * powf(10.0f, radio.ForwardPower_dBm() / 10.0f) / 1000.0f);
                Select();
            } else {
                Tick();
            }
            if (powerSM.state_id != last) {
                if (powerSM.state_id == PowerCalSm_StateId_COLLECT)
                    collects[autoCal.stage]++;
                if (powerSM.state_id == PowerCalSm_StateId_FIT_FORWARD)
                    forwardFits++;
                if (powerSM.state_id == PowerCalSm_StateId_REST)
                    rests++;
                last = powerSM.state_id;
            }
        }
        return powerSM.state_id == target;
    }

    bool bridgeSilent = false;  // the bridge readout gives no readings
    bool failedReads = false;   // some bridge readings are failed I2C reads
    uint32_t meterEntries = 0;
    uint32_t collects[3] = {0, 0, 0};
    uint32_t forwardFits = 0;
    uint32_t rests = 0;
};

TEST_F(PowerCalAutoTest, AutoEntersSetupAndResetReturnsToManual) {
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    EnterAuto(1);
    EXPECT_TRUE(PowerCalAutoActive());

    PowerCalDispatch(PowerCalSm_EventId_RESET);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    EXPECT_FALSE(PowerCalAutoActive());
}

TEST_F(PowerCalAutoTest, AutoNotAllowedWith100WPA) {
    ED.PA100Wactive = true;
    StartPowerAutoCal();
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    EXPECT_FALSE(PowerCalAutoActive());
}

TEST_F(PowerCalAutoTest, LevelIsChosenOnlyInSetup) {
    EnterAuto(1);
    ChangeAutoCalLevel(+1);
    EXPECT_EQ(autoCal.level, 2);
    ChangeAutoCalLevel(+1);
    ChangeAutoCalLevel(+1);
    EXPECT_EQ(autoCal.level, 3) << "Level is limited to 3";
    ChangeAutoCalLevel(-5);
    EXPECT_EQ(autoCal.level, 1) << "Level is limited to 1";

    Select(); // to PROMPT
    ChangeAutoCalLevel(+1);
    EXPECT_EQ(autoCal.level, 1) << "Level cannot change once the session has started";
}

TEST_F(PowerCalAutoTest, Level1SessionVisitsLoadOpenShortWithoutOperatorReadings) {
    EnterAuto(1);
    Select(); // SETUP -> PROMPT
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_PROMPT);
    EXPECT_EQ(autoCal.stage, AUTO_STAGE_LOAD);

    Select(); // load fitted
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    EXPECT_EQ(autoCal.stage, AUTO_STAGE_OPEN);
    EXPECT_EQ(forwardFits, 2u) << "One forward fit per band";
    EXPECT_EQ(collects[AUTO_STAGE_LOAD], 2u * 13u) << "12 CW points and the SSB tone point per band";
    EXPECT_EQ(rests, 0u) << "The load sweep stays keyed between points";
    EXPECT_FALSE(autoCal.keyed) << "Unkeyed while the operator changes the connector";

    Select(); // open fitted
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    EXPECT_EQ(autoCal.stage, AUTO_STAGE_SHORT);
    EXPECT_EQ(collects[AUTO_STAGE_OPEN], 2u * 6u);
    EXPECT_EQ(rests, 2u * 5u) << "Receive between the open points";
    EXPECT_FALSE(autoCal.keyed);

    Select(); // short fitted
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_REVIEW));
    EXPECT_EQ(collects[AUTO_STAGE_SHORT], 2u * 6u);
    EXPECT_EQ(forwardFits, 2u) << "No forward fit on the open or the short";
    EXPECT_EQ(meterEntries, 0u) << "Level 1 needs no wattmeter";
    EXPECT_FALSE(autoCal.keyed);

    PowerCalDispatch(PowerCalSm_EventId_RETRY);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_REVIEW) << "Nothing failed, so nothing to retry";

    Select(); // accept
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    PowerCalDispatch(PowerCalSm_EventId_RESET);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
}

TEST_F(PowerCalAutoTest, Level2TakesTwoWattmeterReadingsPerBand) {
    EnterAuto(2);
    Select();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    EXPECT_EQ(meterEntries, 2u * 2u) << "Near 2 W and at the ceiling";
    EXPECT_EQ(collects[AUTO_STAGE_LOAD], 2u * 15u) << "Sweep, two wattmeter points and SSB point per band";
}

TEST_F(PowerCalAutoTest, WattmeterEntryUnitsAndStepWorkWhileKeyed) {
    // 2026-11-02, radio 2 at level 2: button 15 did nothing at the wattmeter
    // entry, because buttons are ignored while the radio is keyed
    EnterAuto(2);
    uiSM.state_id = UISm_StateId_CALIBRATE_POWER;
    Select();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_METER_ENTRY));
    ASSERT_TRUE(autoCal.keyed);
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_MARK);

    float32_t step = GetPowerIncrement();
    HandleButtonPress(15);
    EXPECT_NE(GetPowerIncrement(), step) << "button 15 changes the step";
    uint8_t unit = GetPowerUnit();
    HandleButtonPress(14);
    EXPECT_NE(GetPowerUnit(), unit) << "button 14 changes the units";
    HandleButtonPress(14);
    EXPECT_EQ(GetPowerUnit(), unit);

    // Other buttons stay locked out while keyed
    bool pa = ED.PA100Wactive;
    HandleButtonPress(16);
    EXPECT_EQ(ED.PA100Wactive, pa);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_METER_ENTRY);
    PowerCalDispatch(PowerCalSm_EventId_RESET);
}

TEST_F(PowerCalAutoTest, Level3TakesAWattmeterReadingAtEveryCWPoint) {
    EnterAuto(3);
    Select();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    EXPECT_EQ(meterEntries, 2u * 8u) << "7 levels and the drift repeat per band";
    EXPECT_EQ(collects[AUTO_STAGE_LOAD], 2u * 9u);
}

TEST_F(PowerCalAutoTest, OnlyTheLevel1LoadStageCanBeSkipped) {
    EnterAuto(2);
    Select();
    SkipPowerCalStage();
    EXPECT_EQ(autoCal.stage, AUTO_STAGE_LOAD) << "Level 2 needs the wattmeter stage";

    PowerCalDispatch(PowerCalSm_EventId_RESET);
    EnterAuto(1);
    Select();
    SkipPowerCalStage();
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_PROMPT);
    EXPECT_EQ(autoCal.stage, AUTO_STAGE_OPEN);
    EXPECT_TRUE(autoCal.loadSkipped);

    SkipPowerCalStage();
    EXPECT_EQ(autoCal.stage, AUTO_STAGE_OPEN) << "The open cannot be skipped";

    Select(); // open fitted
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    EXPECT_EQ(forwardFits, 0u);
    EXPECT_EQ(collects[AUTO_STAGE_OPEN], 2u * 6u);
}

TEST_F(PowerCalAutoTest, WattmeterEntryTimesOutUnkeyedAndRekeysOnSelect) {
    EnterAuto(3);
    Select();
    Select();
    // Reach the first wattmeter entry without answering it
    for (int k = 0; k < 100 && powerSM.state_id != PowerCalSm_StateId_METER_ENTRY; k++)
        Tick();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_METER_ENTRY);
    EXPECT_TRUE(autoCal.keyed);

    for (uint32_t k = 0; k <= powerSM.vars.meterTimeout_ms + 1; k++)
        Tick();
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_METER_PAUSED);
    EXPECT_FALSE(autoCal.keyed) << "Unkeyed after the timeout";

    Select();
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_KEYDOWN);
    EXPECT_TRUE(autoCal.keyed);
    for (int k = 0; k < 100 && powerSM.state_id != PowerCalSm_StateId_METER_ENTRY; k++)
        Tick();
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_METER_ENTRY) << "Same point asks again";
    EXPECT_EQ(autoCal.point, 0u);
}

TEST_F(PowerCalAutoTest, ResetDuringAPointUnkeys) {
    EnterAuto(1);
    Select();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    EXPECT_TRUE(autoCal.keyed);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_MARK);

    ResetPowerCal();
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    EXPECT_FALSE(autoCal.keyed);
    EXPECT_FALSE(PowerCalAutoActive());
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE)
        << "Unkeyed at once, without waiting for the service";
}

TEST_F(PowerCalAutoTest, OscillationAbandonsThatBandsSweepOnly) {
    EnterAuto(1);
    radio.oscillateBand = BAND_40M;
    radio.oscillateStage = AUTO_STAGE_OPEN;
    radio.oscillateBothStandards = true;
    Select();
    SkipPowerCalStage(); // straight to the open
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    EXPECT_EQ(autoCal.band, BAND_40M);

    // V_f stuck whatever the drive: three short falls running, so not compression
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_BAND_DONE, 400));
    EXPECT_EQ(autoCal.point, 4u) << "Abandoned at the third short fall running";
    EXPECT_FALSE(autoCal.keyed);
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_BAND));
    EXPECT_EQ(autoCal.band, BAND_20M) << "Moves on to the next band";
    EXPECT_EQ(autoCal.openFailMask, 1u << BAND_40M);

    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    Select(); // short
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_REVIEW));
    EXPECT_TRUE(AutoAnyFailed());

    Select(); // the passing band is accepted...
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
}

TEST_F(PowerCalAutoTest, RetryRunsTheSessionAgainForTheFailedBandsOnly) {
    EnterAuto(1);
    radio.oscillateBand = BAND_40M;
    radio.oscillateBothStandards = true;
    Select();
    SkipPowerCalStage();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_REVIEW));

    RetryPowerCal();
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_SETUP);
    EXPECT_EQ(autoCal.bandMask, 1u << BAND_40M);
    EXPECT_EQ(autoCal.level, 1) << "Same level";
}

TEST_F(PowerCalAutoTest, LoadUsesCWThenSSBAndTheOpenUsesTheSSBToneWithReceiveBetweenPoints) {
    EnterAuto(1);
    autoCal.bandMask = 1u << BAND_40M;
    Select();
    Select(); // load fitted
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_MARK) << "CW sweep on the load";
    EXPECT_EQ(ED.currentBand[ED.activeVFO], BAND_40M) << "The service tuned to the band";

    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_FIT_FORWARD));
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    EXPECT_TRUE(autoCal.ssbPoint);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_MARK) << "SSB tone point on the load";

    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE)
        << "Receive while the operator changes the connector";
    Select(); // open fitted
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_MARK) << "SSB tone on the open";
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_REST));
    Tick(); // the service unkeys
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE)
        << "True receive between open points";
    EXPECT_EQ(GetRFHardwareState(), RFReceive);
}

TEST_F(PowerCalAutoTest, StoreWaitsForTheMainLoop) {
    EnterAuto(1);
    Select();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    uint32_t point = autoCal.point;
    // Timer ticks only, no service: COLLECT's statistics clear is done in the
    // first Tick(), then the collect time runs out into STORE
    Tick();
    for (int k = 0; k < 100 && powerSM.state_id != PowerCalSm_StateId_STORE; k++)
        PowerCalSm_dispatch_event(&powerSM, PowerCalSm_EventId_DO);
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_STORE);
    for (int k = 0; k < 10; k++)
        PowerCalSm_dispatch_event(&powerSM, PowerCalSm_EventId_DO);
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_STORE);
    EXPECT_EQ(autoCal.point, point) << "The readings are kept by the service";

    Tick(); // the service runs, then the choice
    Tick();
    EXPECT_NE(powerSM.state_id, PowerCalSm_StateId_STORE);
    EXPECT_EQ(autoCal.point, point + 1);
}

TEST_F(PowerCalAutoTest, SelectAndZoomWorkWhileKeyed) {
    uiSM.state_id = UISm_StateId_CALIBRATE_POWER;
    EnterAuto(3);
    Select();
    Select();
    for (int k = 0; k < 100 && powerSM.state_id != PowerCalSm_StateId_METER_ENTRY; k++)
        Tick();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_METER_ENTRY);
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_MARK);

    HandleButtonPress(MENU_OPTION_SELECT); // wattmeter reading entered
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_COLLECT);

    HandleButtonPress(BAND_UP); // other buttons stay disabled while keyed
    EXPECT_EQ(ED.currentBand[ED.activeVFO], BAND_40M);

    HandleButtonPress(ZOOM); // abort
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_POWERPOINT1);
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_SPACE);
}

TEST_F(PowerCalAutoTest, OperatorPTTIsIgnored) {
    EnterAuto(1);
    Select();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_MARK);

    SetInterrupt(iPTT_RELEASED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_POWER_MARK) << "Release does not cut the point short";

    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    ASSERT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE);
    SetInterrupt(iPTT_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_CALIBRATE_OFFSET_SPACE) << "Press does not key the radio";
}

TEST_F(PowerCalAutoTest, CATBridgeStatisticsAreRefusedDuringAuto) {
    char clear[] = "BR0;";
    EnterAuto(1);
    EXPECT_STREQ(BR_write(clear), "?;");
    PowerCalDispatch(PowerCalSm_EventId_RESET);
    EXPECT_STREQ(BR_write(clear), "");
}

TEST_F(PowerCalAutoTest, Level3StoresTheTrueCalibration) {
    EnterAuto(3);
    RunSession();
    for (int32_t band : {BAND_40M, BAND_20M}) {
        const AutoCalBandResult *r = GetAutoCalResult(band);
        EXPECT_TRUE(r->checked);
        EXPECT_EQ(r->failBits, 0u) << bands[band].name;
        EXPECT_FALSE(r->openOnly);
    }
    EXPECT_FALSE(AutoAnyFailed());
    Select(); // store
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    Tick();   // the main loop stores
    for (int32_t band : {BAND_40M, BAND_20M}) {
        SCOPED_TRACE(bands[band].name);
        EXPECT_NEAR(ED.SWR_F_SlopeAdj[band], radio.m_f - 25.0f, 0.01);
        EXPECT_NEAR(ED.SWR_F_Offset[band], radio.b_f(band) + SWRCAL_OFFSET_CONSTANT_DB, 0.01);
        EXPECT_NEAR(ED.SWR_R_SlopeAdj[band], radio.m_r - 25.0f, 0.01);
        EXPECT_NEAR(ED.SWR_R_Offset[band], radio.o_r, 0.01) << "The leak cancels";
        EXPECT_EQ(ED.SWR_CalLevel[band], 3);
        EXPECT_NEAR(ED.PowerCal_20W_Psat_mW[band], radio.Psat_mW, 0.02f * radio.Psat_mW);
        EXPECT_NEAR(ED.PowerCal_20W_kindex[band], radio.k, 0.05f * radio.k);
        EXPECT_NEAR(ED.PowerCal_20W_DSP_Gain_correction_dB[band], radio.ssbGainFor3W_dB, 0.01);
        EXPECT_NEAR(StoredSWR(band, 1.0f / 3.0f), 2.0f, 0.01) << "A 100 Ohm load";
        EXPECT_NEAR(GetAutoCalResult(band)->S50, (1 + radio.gamma50) / (1 - radio.gamma50), 0.01);
    }
    EXPECT_EQ(ED.SWR_CalLevel[BAND_80M], 0) << "Bands outside the session are untouched";
}

TEST_F(PowerCalAutoTest, Level1StoresTheDefaultsAndReadsSWRWell) {
    EnterAuto(1);
    RunSession();
    EXPECT_FALSE(AutoAnyFailed());
    Select();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    Tick();   // the main loop stores
    for (int32_t band : {BAND_40M, BAND_20M}) {
        SCOPED_TRACE(bands[band].name);
        EXPECT_FLOAT_EQ(ED.SWR_F_SlopeAdj[band], 0.0f) << "Nominal slope";
        EXPECT_NEAR(ED.SWR_F_Offset[band], SWRCalLevel1ForwardOffset(band, 10), 1e-4) << "The default";
        EXPECT_EQ(ED.SWR_CalLevel[band], 1);
        EXPECT_NEAR(StoredSWR(band, 1.0f / 3.0f), 2.0f, 0.05) << "The SWR barely depends on the level";
        EXPECT_GT(ED.PowerCal_20W_Psat_mW[band], 0.0f) << "Level 1 stores the PA curve too";
        EXPECT_NEAR(ED.PowerCal_20W_DSP_Gain_correction_dB[band], radio.ssbGainFor3W_dB, 1.5);
    }
}

TEST_F(PowerCalAutoTest, Level2ReadsTwoWattsCorrectly) {
    EnterAuto(2);
    RunSession();
    EXPECT_FALSE(AutoAnyFailed());
    Select();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    Tick();   // the main loop stores
    for (int32_t band : {BAND_40M, BAND_20M}) {
        SCOPED_TRACE(bands[band].name);
        EXPECT_FLOAT_EQ(ED.SWR_F_SlopeAdj[band], 0.0f);
        // The stored line reads the wattmeter point's power exactly
        float32_t P2 = 10.0f * log10f(2000.0f);
        float32_t Vf2 = radio.m_f * (P2 - radio.b_f(band));
        float32_t read = SWRCalPower_dBm(Vf2, 25.0f, ED.SWR_F_Offset[band] - SWRCAL_OFFSET_CONSTANT_DB);
        EXPECT_NEAR(read, P2, 0.5) << "Near 2 W";
        EXPECT_EQ(ED.SWR_CalLevel[band], 2);
    }
}

TEST_F(PowerCalAutoTest, Level2WattmeterEntryErrorFailsTheBand) {
    EnterAuto(2);
    radio.meterFactor = 10.0f; // 20 W entered for 2 W
    float32_t before = ED.SWR_F_Offset[BAND_40M] = 1.25f;
    RunSession();
    EXPECT_TRUE(GetAutoCalResult(BAND_40M)->failBits & AUTO_FAIL_LEVEL2_OFFSET);
    Select();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    Tick();   // the main loop stores
    EXPECT_FLOAT_EQ(ED.SWR_F_Offset[BAND_40M], before) << "A failed band keeps its calibration";
}

TEST_F(PowerCalAutoTest, OscillationIntoTheShortLeavesTheOpenAlone) {
    EnterAuto(3);
    radio.oscillateBand = BAND_20M;
    radio.oscillateStage = AUTO_STAGE_SHORT;
    RunSession();
    const AutoCalBandResult *r = GetAutoCalResult(BAND_20M);
    EXPECT_EQ(r->failBits, 0u);
    EXPECT_TRUE(r->openOnly);
    Select();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    Tick();   // the main loop stores
    EXPECT_EQ(ED.SWR_CalLevel[BAND_20M], 3 | SWR_CAL_OPEN_ONLY);
    EXPECT_EQ(ED.SWR_CalLevel[BAND_40M], 3);
    // From the open alone the leak is not cancelled: the open reads it high, so
    // the line's offset comes out low by the leak
    EXPECT_NEAR(ED.SWR_R_Offset[BAND_20M], radio.o_r - radio.leak_dB, 0.01);
    EXPECT_NEAR(StoredSWR(BAND_20M, 1.0f / 3.0f), 2.0f, 0.1);
}

TEST_F(PowerCalAutoTest, OscillationIntoTheOpenLeavesTheShortAlone) {
    EnterAuto(3);
    radio.oscillateBand = BAND_40M;
    radio.oscillateStage = AUTO_STAGE_OPEN;
    RunSession();
    const AutoCalBandResult *r = GetAutoCalResult(BAND_40M);
    EXPECT_EQ(r->failBits, 0u);
    EXPECT_TRUE(r->shortOnly);
    EXPECT_FALSE(r->openOnly);
    Select();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    Tick();   // the main loop stores
    EXPECT_EQ(ED.SWR_CalLevel[BAND_40M], 3 | SWR_CAL_SHORT_ONLY);
    EXPECT_EQ(ED.SWR_CalLevel[BAND_20M], 3);
    // From the short alone the leak is not cancelled: the short reads it low, so
    // the line's offset comes out high by the leak
    EXPECT_NEAR(ED.SWR_R_Offset[BAND_40M], radio.o_r + radio.leak_dB, 0.01);
    EXPECT_NEAR(StoredSWR(BAND_40M, 1.0f / 3.0f), 2.0f, 0.1);
}

TEST_F(PowerCalAutoTest, OscillationIntoBothStandardsFailsOnlyThatBand) {
    EnterAuto(1);
    radio.oscillateBand = BAND_40M;
    radio.oscillateBothStandards = true;
    float32_t before = ED.SWR_R_Offset[BAND_40M] = -7.5f;
    RunSession();
    EXPECT_TRUE(GetAutoCalResult(BAND_40M)->failBits & AUTO_FAIL_OPEN);
    EXPECT_EQ(GetAutoCalResult(BAND_20M)->failBits, 0u);
    Select();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    Tick();   // the main loop stores
    EXPECT_FLOAT_EQ(ED.SWR_R_Offset[BAND_40M], before);
    EXPECT_EQ(ED.SWR_CalLevel[BAND_40M], 0);
    EXPECT_EQ(ED.SWR_CalLevel[BAND_20M], 1);
}

TEST_F(PowerCalAutoTest, SkippingTheLoadKeepsThePowerCalibration) {
    EnterAuto(1);
    ED.PowerCal_20W_Psat_mW[BAND_40M] = 12345.0f;
    RunSession(true);
    EXPECT_FALSE(AutoAnyFailed());
    EXPECT_FALSE(GetAutoCalResult(BAND_40M)->hasS50) << "No load, no S50";
    Select();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_DONE);
    Tick();   // the main loop stores
    EXPECT_FLOAT_EQ(ED.PowerCal_20W_Psat_mW[BAND_40M], 12345.0f);
    EXPECT_EQ(ED.SWR_CalLevel[BAND_40M], 1);
}

TEST_F(PowerCalAutoTest, LeavingAutoRestoresTheOperatorsBand) {
    SetCalibrationBand(BAND_15M);
    int64_t freq = ED.centerFreq_Hz[ED.activeVFO];
    EnterAuto(1);
    Select();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    EXPECT_EQ(ED.currentBand[ED.activeVFO], BAND_40M);
    ResetPowerCal();
    EXPECT_EQ(ED.currentBand[ED.activeVFO], BAND_15M);
    EXPECT_EQ(ED.centerFreq_Hz[ED.activeVFO], freq);
}

TEST_F(PowerCalAutoTest, EnteringPowerCalibrationKeepsTheSWRCalibration) {
    ED.SWR_F_Offset[BAND_20M] = -5.2f;
    ED.SWR_R_SlopeAdj[BAND_20M] = 0.3f;
    InitializePowerCalibration();
    EXPECT_FLOAT_EQ(ED.SWR_F_Offset[BAND_20M], -5.2f);
    EXPECT_FLOAT_EQ(ED.SWR_R_SlopeAdj[BAND_20M], 0.3f);
}

TEST_F(PowerCalAutoTest, MeterEntryStartsAtTheExpectedPower) {
    EnterAuto(3);
    Select();
    Select();
    for (int k = 0; k < 100 && powerSM.state_id != PowerCalSm_StateId_METER_ENTRY; k++)
        Tick();
    ASSERT_EQ(powerSM.state_id, PowerCalSm_StateId_METER_ENTRY);
    EXPECT_NEAR(GetMeasuredPower(), CalculateCWPowerLevel(autoCal.drive_dB, 0) / 1000.0f, 1e-4);
}

TEST_F(PowerCalAutoTest, SetupEncodersChooseLevelAndTurns) {
    EnterAuto(1);
    ChangeAutoCalTurns(+10);
    EXPECT_EQ(ED.SWR_CouplerTurns, 20);
    ChangeAutoCalTurns(-100);
    EXPECT_EQ(ED.SWR_CouplerTurns, 5);
    Select(); // no longer on SETUP
    ChangeAutoCalTurns(+1);
    EXPECT_EQ(ED.SWR_CouplerTurns, 5);
    ED.SWR_CouplerTurns = 10;
}

TEST_F(PowerCalAutoTest, TheScreenDrawsInEveryState) {
    uiSM.state_id = UISm_StateId_CALIBRATE_POWER;
    EnterAuto(3);
    DrawCalibratePower();
    Select();
    DrawCalibratePower();
    Select();
    for (int k = 0; k < 3000 && powerSM.state_id != PowerCalSm_StateId_REVIEW; k++){
        if (powerSM.state_id == PowerCalSm_StateId_METER_ENTRY){
            SetMeasuredPower(powf(10.0f, radio.ForwardPower_dBm() / 10.0f) / 1000.0f);
            Select();
        } else if (powerSM.state_id == PowerCalSm_StateId_PROMPT){
            Select();
        } else {
            Tick();
        }
        DrawCalibratePower();
    }
    EXPECT_EQ(powerSM.state_id, PowerCalSm_StateId_REVIEW);
    DrawCalibratePower();
    PowerCalDispatch(PowerCalSm_EventId_RESET);
    DrawCalibratePower(); // back to the manual screen
}

/**
 * A dead bridge readout on the load must not set the open and short's drive:
 * fitted to no readings, the SSB correction came out near +77 dB and the tone
 * was keyed into the open at +72 dB DSP gain.
 */
TEST_F(PowerCalAutoTest, NoBridgeReadingsKeepsTheStoredSSBDrive) {
    EnterAuto(1);
    autoCal.bandMask = 1u << BAND_40M;
    ED.PowerCal_20W_DSP_Gain_correction_dB[BAND_40M] = 0.5f;
    bridgeSilent = true;
    Select();
    Select(); // load
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    EXPECT_FALSE(GetAutoCalResult(BAND_40M)->hasPACurve);
    Select(); // open
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_COLLECT));
    EXPECT_NEAR(autoCal.drive_dB, 0.5f + 10.0f * log10f(1.2f / 3.0f), 1e-4)
        << "the first open point starts from the stored correction";
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    Select(); // short
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_REVIEW));
    EXPECT_TRUE(GetAutoCalResult(BAND_40M)->failBits & AUTO_FAIL_FEW_READINGS);
    EXPECT_TRUE(GetAutoCalResult(BAND_40M)->failBits & AUTO_FAIL_PA_CURVE);
}

/**
 * Radio 1's 10 m: the forward detector over-reads by up to 2.3 dB in saturation.
 * Level 2's wattmeter reading at 0 dB puts the PA ceiling right; level 1 cannot.
 */
TEST_F(PowerCalAutoTest, Level2TakesThePACeilingFromTheWattmeter) {
    float32_t trueCeiling_mW = radio.Psat_mW * tanhf(radio.k);
    for (uint8_t level : {1, 2}) {
        SCOPED_TRACE(level);
        PowerCalDispatch(PowerCalSm_EventId_RESET);
        EnterAuto(level);
        autoCal.bandMask = 1u << BAND_10M;
        radio.saturationOverRead = true;
        Select();
        Select(); // load
        ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
        const AutoCalBandResult *r = GetAutoCalResult(BAND_10M);
        ASSERT_TRUE(r->hasPACurve);
        float32_t ceilingError_dB = 10.0f * log10f(r->Psat_mW * tanhf(r->kindex) / trueCeiling_mW);
        if (level == 2) {
            // Relative to level 2's line, which is set at the 2 W point
            EXPECT_GT(r->ceilingOverRead_dB, 1.5f) << "the detector's over-read, logged";
            EXPECT_NEAR(ceilingError_dB, 0.0f, 0.2f) << "the ceiling from the wattmeter";
        } else {
            EXPECT_GT(ceilingError_dB, 1.0f) << "level 1 cannot see the over-read (documented limit)";
        }
    }
}

/** Collects what the firmware prints with Debug() (the Serial mock writes to std::cout) */
class LogCapture {
public:
    LogCapture() : old(std::cout.rdbuf(buffer.rdbuf())) {}
    ~LogCapture() { std::cout.rdbuf(old); }
    /** Number of lines that contain every one of the given pieces */
    int Count(std::initializer_list<const char *> pieces) {
        std::istringstream in(buffer.str());
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            bool all = true;
            for (const char *p : pieces)
                all = all && (line.find(p) != std::string::npos);
            n += all;
        }
        return n;
    }
    std::string Text(void) { return buffer.str(); }
private:
    std::stringstream buffer;
    std::streambuf *old;
};

TEST_F(PowerCalAutoTest, LogsTheOscillationAndItsConsequences) {
    EnterAuto(1);
    radio.oscillateBand = BAND_40M;
    radio.oscillateBothStandards = true;
    LogCapture log;
    Select();
    SkipPowerCalStage();
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
    Select();
    ASSERT_TRUE(RunUntil(PowerCalSm_StateId_REVIEW));
    Select();
    Tick();
    // Stuck whatever the drive: two points left out as if compressed, then abandoned
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,open,tone", "compression: the next point fell too little"}), 2)
        << log.Text();
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,open,tone", "oscillation: V_f fell too little at 3 points running"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,open,sweep,abandoned: reflected line from the short alone"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,short,tone", "oscillation: V_f fell too little at 3 points running"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,band,band fails,no reflected line: open and short sweeps both abandoned"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,open,open sweep"}), 0) << "no fallback to log";
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,band,not stored"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,20M"}), 0) << "nothing left out on 20 m";
}

TEST_F(PowerCalAutoTest, LogsTheShortOscillationAsOpenOnly) {
    EnterAuto(1);
    autoCal.bandMask = 1u << BAND_20M;
    radio.oscillateBand = BAND_20M;
    radio.oscillateStage = AUTO_STAGE_SHORT;
    LogCapture log;
    RunToReviewForLog();
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,20M,short,tone", "oscillation:"}), 1) << log.Text();
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,20M,short,sweep,abandoned: reflected line from the open alone"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,20M,short,short sweep,abandoned"}), 1);
    EXPECT_EQ(log.Count({"band fails"}), 0) << "open alone is not a failure";
}

TEST_F(PowerCalAutoTest, CompressionLeavesOutTheTopPointsAndExtendsTheSweep) {
    // Radio 2's 20, 17 and 12 m open on 2026-11-02: the PA at its ceiling into the
    // open at the sweep's first drive, so V_f hardly fell at the next point.
    // Level 3, so the stored reflected line can be checked against the truth
    EnterAuto(3);
    radio.compressBand = BAND_40M;
    radio.compressStage = AUTO_STAGE_OPEN;
    radio.compressCeiling_dBm = 10.0f * log10f(1200.0f) - 3.3f; // the first two points at the ceiling
    LogCapture log;
    RunToReviewForLog();
    const AutoCalBandResult *r = GetAutoCalResult(BAND_40M);
    EXPECT_EQ(r->failBits, 0u) << log.Text();
    EXPECT_FALSE(r->openOnly);
    EXPECT_FALSE(r->shortOnly) << "the open is used, without its compressed points";
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,open,tone", "compression: the next point fell too little"}), 2);
    EXPECT_EQ(log.Count({"oscillation"}), 0);
    // 6 points each on two bands, and on 40 m's open two more for the two left out
    EXPECT_EQ(collects[AUTO_STAGE_OPEN], 14u);
    EXPECT_EQ(collects[AUTO_STAGE_SHORT], 12u);
    EXPECT_EQ(log.Count({"AUTOCAL,40M,open,tone,"}), 8) << "each point is logged as it is measured";
    Select();
    Tick();
    EXPECT_EQ(ED.SWR_CalLevel[BAND_40M], 3);
    EXPECT_NEAR(ED.SWR_R_Offset[BAND_40M], radio.o_r, 0.01) << "the leak still cancels";
    EXPECT_NEAR(StoredSWR(BAND_40M, 1.0f / 3.0f), 2.0f, 0.05);
}

TEST_F(PowerCalAutoTest, ARiseInTheSweepAbandonsItAtOnce) {
    // Radio 2's 10 m short on 2026-11-02: three good points, then V_f jumped up
    EnterAuto(1);
    autoCal.bandMask = 1u << BAND_20M;
    radio.oscillateBand = BAND_20M;
    radio.oscillateStage = AUTO_STAGE_SHORT;
    radio.oscillateFromPoint = 3;
    LogCapture log;
    RunToReviewForLog();
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,20M,short,tone", "oscillation: V_f rose as the drive fell"}), 1)
        << log.Text();
    EXPECT_EQ(log.Count({"compression"}), 0);
    EXPECT_EQ(collects[AUTO_STAGE_SHORT], 4u) << "abandoned at the point that rose";
    const AutoCalBandResult *r = GetAutoCalResult(BAND_20M);
    EXPECT_EQ(r->failBits, 0u);
    EXPECT_TRUE(r->openOnly);
}

TEST_F(PowerCalAutoTest, AnOscillationStartingDuringAPointAbandonsTheSweepThere) {
    // Radio 2's 10 m short at level 2 on 2026-11-02: the point where the
    // oscillation began fell only 5.5 mV with a spread of 56 mV (4 mV before).
    // Taken for compression, it was kept and the good point before it dropped.
    EnterAuto(1);
    autoCal.bandMask = 1u << BAND_20M;
    radio.oscillateBand = BAND_20M;
    radio.oscillateStage = AUTO_STAGE_SHORT;
    radio.oscillateFromPoint = 2;
    radio.oscillateUnsteady = true;
    LogCapture log;
    RunToReviewForLog();
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,20M,short,tone", "oscillation: V_f unsteady as its spread jumped"}), 1)
        << log.Text();
    EXPECT_EQ(log.Count({"compression"}), 0) << "the good point before it is kept";
    EXPECT_EQ(collects[AUTO_STAGE_SHORT], 3u) << "abandoned at the unsteady point";
    const AutoCalBandResult *r = GetAutoCalResult(BAND_20M);
    EXPECT_EQ(r->failBits, 0u);
    EXPECT_TRUE(r->openOnly);
}

TEST_F(PowerCalAutoTest, AMissingLoadFailsTheBand) {
    // 2026-11-02: the load came off part-way through radio 2's 160 m sweep
    EnterAuto(1);
    radio.loadMissingBand = BAND_40M;
    LogCapture log;
    RunToReviewForLog();
    EXPECT_TRUE(GetAutoCalResult(BAND_40M)->failBits & AUTO_FAIL_NO_LOAD) << log.Text();
    EXPECT_EQ(GetAutoCalResult(BAND_20M)->failBits, 0u);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,load,cw 0.00 dB,no load: V_f minus V_r below the limit"}), 1)
        << "logged once, at the band's first point";
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,band,band fails,no 50 Ohm load", ",13.000,"}), 1)
        << "every CW point and the SSB point";
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,20M", "no load"}), 0);
    Select();
    Tick();
    EXPECT_EQ(ED.SWR_CalLevel[BAND_40M], 0) << "not stored";
    EXPECT_EQ(ED.SWR_CalLevel[BAND_20M], 1);
}

TEST_F(PowerCalAutoTest, LoadPointsAreKeyedForAtMostOneAndAHalfSeconds) {
    // The 50 Ohm load runs hot: settle and collect are its whole key-down per point
    StartPowerAutoCal();
    EXPECT_LE(powerSM.vars.settle_ms + autoCal.collectLoad_ms, 1500u);
    EXPECT_GE(autoCal.collectLoad_ms, 1000u) << "enough readings for the averages";
    PowerCalDispatch(PowerCalSm_EventId_RESET);
}

TEST_F(PowerCalAutoTest, LogsPointsWithTooFewOrRejectedReadings) {
    EnterAuto(1);
    autoCal.bandMask = 1u << BAND_40M;
    failedReads = true;
    autoCal.minReadings = 1000; // every point is short of readings
    LogCapture log;
    RunToReviewForLog();
    // The fixture adds 10 good readings and one failed read per tick of the collect
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,load,cw 0.00 dB,readings rejected", ",5.000,"}), 1) << log.Text();
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,load,cw 0.00 dB,too few bridge readings", ",50.000,1000.000"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,load,PA curve and SSB gain,SSB point missing or too few readings"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,band,band fails,no PA curve or SSB gain"}), 1);
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,band,band fails,a point had too few bridge readings"}), 1);
}

TEST_F(PowerCalAutoTest, LogsTheCeilingDecisions) {
    // Level 3: the over-reading 10 m ceiling points are left out of the forward fit
    {
        EnterAuto(3);
        autoCal.bandMask = 1u << BAND_10M;
        radio.saturationOverRead = true;
        LogCapture log;
        Select();
        Select();
        ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
        EXPECT_EQ(log.Count({"AUTOCAL-SKIP,10M,load,meter 0.00 dB,ceiling point off the line"}), 1) << log.Text();
    }
    // Level 2: the PA curve takes its ceiling from the wattmeter
    {
        PowerCalDispatch(PowerCalSm_EventId_RESET);
        EnterAuto(2);
        autoCal.bandMask = 1u << BAND_10M;
        LogCapture log;
        Select();
        Select();
        ASSERT_TRUE(RunUntil(PowerCalSm_StateId_PROMPT));
        EXPECT_EQ(log.Count({"AUTOCAL-SKIP,10M,load,cw 0.00 dB,PA curve uses the wattmeter's ceiling instead"}), 1)
            << log.Text();
        EXPECT_GT(log.Count({"AUTOCAL-SKIP,10M,load,cw", "above the wattmeter ceiling"}), 0);
    }
}

TEST_F(PowerCalAutoTest, LogsTheWeakPointsLeftOutOfS50) {
    EnterAuto(1);
    autoCal.bandMask = 1u << BAND_40M;
    LogCapture log;
    RunToReviewForLog();
    EXPECT_EQ(log.Count({"AUTOCAL-SKIP,40M,load,S50,load points below 0.1 W left out"}), 1) << log.Text();
}

void TXGain(DataBlock *data); // DSP.cpp
extern float32_t TXgainDSP;   // DSP.cpp

TEST(PowerCalSSBGainOverride, TXGainUsesTheOverride) {
    InitializeStorage();
    ED.PA100Wactive = false;
    float32_t I[4] = {1, 1, 1, 1}, Q[4] = {0, 0, 0, 0};
    DataBlock data = {4, 48000, I, Q};

    ClearSSBGainOverride();
    TXGain(&data);
    float32_t normal_dB = TXgainDSP;

    SetSSBGainOverride(-12.0f);
    I[0] = 1;
    TXGain(&data);
    EXPECT_FLOAT_EQ(TXgainDSP, -12.0f);
    EXPECT_NEAR(I[0], powf(10.0f, -12.0f/20.0f), 1e-5);

    ClearSSBGainOverride();
    TXGain(&data);
    EXPECT_FLOAT_EQ(TXgainDSP, normal_dB);
}

TEST_F(PowerCalAutoTest, LeavingRightAfterAcceptingStillStores) {
    EnterAuto(1);
    RunSession();
    Select(); // store...
    PowerCalDispatch(PowerCalSm_EventId_RESET); // ...and leave before the main loop runs
    EXPECT_EQ(ED.SWR_CalLevel[BAND_40M], 1);
    EXPECT_EQ(ED.SWR_CalLevel[BAND_20M], 1);
}
