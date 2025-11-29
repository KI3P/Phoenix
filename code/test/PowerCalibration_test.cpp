
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
    extern struct PrimaryMenuOption primaryMenu[8];
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

