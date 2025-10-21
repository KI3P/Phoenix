/**
 * @file Display_test.cpp
 * @brief Unit tests for MainBoard_Display.cpp functions
 *
 * This file contains unit tests for the display drawing and updating functions
 * in MainBoard_Display.cpp. Tests verify proper display pane management, frequency
 * formatting, VFO display updates, and settings display.
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
 * Test fixture for Display tests
 * Sets up common test environment for display functions
 */
class DisplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test environment before each test
        // TODO: Add setup code (e.g., initialize ED structure, display state)
    }

    void TearDown() override {
        // Clean up after each test
        stop_timer1ms(); // Stop the timer thread to prevent crashes during teardown
    }
};

/**
 * Test ShowSpectrum() draws spectrum and waterfall
 */
TEST_F(DisplayTest, MenuRedrawn) {
    // Set up the queues so we get some simulated data through and start the "clock"
    Q_in_L.setChannel(0);
    Q_in_R.setChannel(1);
    Q_in_L.clear();
    Q_in_R.clear();
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
    uiSM.vars.splashDuration_ms = SPLASH_DURATION_MS;
    UISm_start(&uiSM);
    UpdateAudioIOState();

    // Now, start the 1ms timer interrupt to simulate hardware timer
    start_timer1ms();

    //-------------------------------------------------------------
    
    EXPECT_EQ(modeSM.state_id, ModeSm_StateId_SSB_RECEIVE);

    // Check the state before loop is invoked and then again after
    loop();MyDelay(10);
    for (int k = 0; k < 200; k++){
        loop();MyDelay(10);
    }
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME );
    SetButton(MAIN_MENU_UP);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_MAIN_MENU);

    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    loop(); MyDelay(10);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);

    loop(); MyDelay(10);
    
}
