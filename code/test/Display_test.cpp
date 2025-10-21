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

/**
 * Test IncrementVariable() with TYPE_I8
 * Verifies that 8-bit integer variables are incremented correctly
 * with proper bounds checking
 */
TEST_F(DisplayTest, IncrementVariableI8_Normal) {
    int8_t test_var = 10;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 100, .step = 5}}
    };

    IncrementVariable(&param);

    // Should increment by step (5)
    EXPECT_EQ(test_var, 15);
}

/**
 * Test IncrementVariable() with TYPE_I8 at maximum limit
 * Verifies that incrementing doesn't exceed max value
 */
TEST_F(DisplayTest, IncrementVariableI8_AtMax) {
    int8_t test_var = 98;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 100, .step = 5}}
    };

    IncrementVariable(&param);

    // Should be clamped to max value
    EXPECT_EQ(test_var, 100);
}

/**
 * Test IncrementVariable() with TYPE_I8 exceeding maximum
 * Verifies proper clamping when increment would exceed max
 */
TEST_F(DisplayTest, IncrementVariableI8_ExceedMax) {
    int8_t test_var = 100;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 100, .step = 5}}
    };

    IncrementVariable(&param);

    // Should remain at max when already at max
    EXPECT_EQ(test_var, 100);
}

/**
 * Test IncrementVariable() with NULL pointer
 * Verifies safe handling of null variable pointer
 */
TEST_F(DisplayTest, IncrementVariableI8_NullPointer) {
    VariableParameter param = {
        .variable = NULL,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 100, .step = 5}}
    };

    // Should not crash when variable is NULL
    IncrementVariable(&param);

    // Test passes if no crash occurs
    SUCCEED();
}

/**
 * Test IncrementVariable() with negative values
 * Verifies correct handling of negative ranges
 */
TEST_F(DisplayTest, IncrementVariableI8_NegativeRange) {
    int8_t test_var = -50;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = -100, .max = 0, .step = 10}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, -40);
}

/**
 * Test IncrementVariable() with step size of 1
 * Verifies correct operation with minimal step
 */
TEST_F(DisplayTest, IncrementVariableI8_StepOne) {
    int8_t test_var = 42;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 127, .step = 1}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, 43);
}

/**
 * Test IncrementVariable() with TYPE_I16
 * Verifies that 16-bit integer variables are incremented correctly
 */
TEST_F(DisplayTest, IncrementVariableI16_Normal) {
    int16_t test_var = 100;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I16,
        .limits = {.i16 = {.min = 0, .max = 1000, .step = 50}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, 150);
}

/**
 * Test IncrementVariable() with TYPE_I16 at maximum
 */
TEST_F(DisplayTest, IncrementVariableI16_AtMax) {
    int16_t test_var = 980;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I16,
        .limits = {.i16 = {.min = 0, .max = 1000, .step = 50}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, 1000);
}

/**
 * Test IncrementVariable() with TYPE_I32
 * Verifies that 32-bit integer variables are incremented correctly
 */
TEST_F(DisplayTest, IncrementVariableI32_Normal) {
    int32_t test_var = 1000;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I32,
        .limits = {.i32 = {.min = 0, .max = 10000, .step = 100}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, 1100);
}

/**
 * Test IncrementVariable() with TYPE_I32 exceeding maximum
 */
TEST_F(DisplayTest, IncrementVariableI32_ExceedMax) {
    int32_t test_var = 9950;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I32,
        .limits = {.i32 = {.min = 0, .max = 10000, .step = 100}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, 10000);
}

/**
 * Test IncrementVariable() with TYPE_I64
 * Verifies that 64-bit integer variables are incremented correctly
 */
TEST_F(DisplayTest, IncrementVariableI64_Normal) {
    int64_t test_var = 1000000LL;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I64,
        .limits = {.i64 = {.min = 0, .max = 10000000LL, .step = 1000}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, 1001000LL);
}

/**
 * Test IncrementVariable() with TYPE_I64 at maximum
 */
TEST_F(DisplayTest, IncrementVariableI64_AtMax) {
    int64_t test_var = 9999500LL;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I64,
        .limits = {.i64 = {.min = 0, .max = 10000000LL, .step = 1000}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, 10000000LL);
}

/**
 * Test IncrementVariable() with TYPE_F32
 * Verifies that floating-point variables are incremented correctly
 */
TEST_F(DisplayTest, IncrementVariableF32_Normal) {
    float test_var = 1.5f;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_F32,
        .limits = {.f32 = {.min = 0.0f, .max = 10.0f, .step = 0.5f}}
    };

    IncrementVariable(&param);

    EXPECT_FLOAT_EQ(test_var, 2.0f);
}

/**
 * Test IncrementVariable() with TYPE_F32 exceeding maximum
 */
TEST_F(DisplayTest, IncrementVariableF32_ExceedMax) {
    float test_var = 9.8f;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_F32,
        .limits = {.f32 = {.min = 0.0f, .max = 10.0f, .step = 0.5f}}
    };

    IncrementVariable(&param);

    EXPECT_FLOAT_EQ(test_var, 10.0f);
}

/**
 * Test IncrementVariable() with TYPE_F32 negative values
 */
TEST_F(DisplayTest, IncrementVariableF32_NegativeRange) {
    float test_var = -5.0f;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_F32,
        .limits = {.f32 = {.min = -10.0f, .max = 0.0f, .step = 1.0f}}
    };

    IncrementVariable(&param);

    EXPECT_FLOAT_EQ(test_var, -4.0f);
}

/**
 * Test IncrementVariable() with TYPE_KeyTypeId
 * Verifies that KeyTypeId enum variables are incremented correctly
 */
TEST_F(DisplayTest, IncrementVariableKeyTypeId_Normal) {
    KeyTypeId test_var = KeyTypeId_Straight;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_KeyTypeId,
        .limits = {.keyType = {.min = KeyTypeId_Straight, .max = KeyTypeId_Keyer, .step = 1}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, KeyTypeId_Keyer);
}

/**
 * Test IncrementVariable() with TYPE_KeyTypeId at maximum
 */
TEST_F(DisplayTest, IncrementVariableKeyTypeId_AtMax) {
    KeyTypeId test_var = KeyTypeId_Keyer;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_KeyTypeId,
        .limits = {.keyType = {.min = KeyTypeId_Straight, .max = KeyTypeId_Keyer, .step = 1}}
    };

    IncrementVariable(&param);

    // Should be clamped to max
    EXPECT_EQ(test_var, KeyTypeId_Keyer);
}

/**
 * Test IncrementVariable() with TYPE_BOOL
 * Verifies that boolean variables toggle correctly
 */
TEST_F(DisplayTest, IncrementVariableBool_FalseToTrue) {
    bool test_var = false;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_BOOL,
        .limits = {.b = {.min = false, .max = true, .step = 1}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, true);
}

/**
 * Test IncrementVariable() with TYPE_BOOL toggling from true to false
 */
TEST_F(DisplayTest, IncrementVariableBool_TrueToFalse) {
    bool test_var = true;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_BOOL,
        .limits = {.b = {.min = false, .max = true, .step = 1}}
    };

    IncrementVariable(&param);

    EXPECT_EQ(test_var, false);
}

/**
 * Test IncrementVariable() with TYPE_BOOL multiple toggles
 */
TEST_F(DisplayTest, IncrementVariableBool_MultipleToggles) {
    bool test_var = false;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_BOOL,
        .limits = {.b = {.min = false, .max = true, .step = 1}}
    };

    IncrementVariable(&param);
    EXPECT_EQ(test_var, true);

    IncrementVariable(&param);
    EXPECT_EQ(test_var, false);

    IncrementVariable(&param);
    EXPECT_EQ(test_var, true);
}

///////////////////////////////////////////////////////////////////////////////
// DecrementVariable Tests
///////////////////////////////////////////////////////////////////////////////

/**
 * Test DecrementVariable() with TYPE_I8
 * Verifies that 8-bit integer variables are decremented correctly
 * with proper bounds checking
 */
TEST_F(DisplayTest, DecrementVariableI8_Normal) {
    int8_t test_var = 20;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 100, .step = 5}}
    };

    DecrementVariable(&param);

    // Should decrement by step (5)
    EXPECT_EQ(test_var, 15);
}

/**
 * Test DecrementVariable() with TYPE_I8 at minimum limit
 * Verifies that decrementing doesn't go below min value
 */
TEST_F(DisplayTest, DecrementVariableI8_AtMin) {
    int8_t test_var = 3;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 100, .step = 5}}
    };

    DecrementVariable(&param);

    // Should be clamped to min value
    EXPECT_EQ(test_var, 0);
}

/**
 * Test DecrementVariable() with TYPE_I8 going below minimum
 * Verifies proper clamping when decrement would go below min
 */
TEST_F(DisplayTest, DecrementVariableI8_BelowMin) {
    int8_t test_var = 0;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 100, .step = 5}}
    };

    DecrementVariable(&param);

    // Should remain at min when already at min
    EXPECT_EQ(test_var, 0);
}

/**
 * Test DecrementVariable() with NULL pointer
 * Verifies safe handling of null variable pointer
 */
TEST_F(DisplayTest, DecrementVariableI8_NullPointer) {
    VariableParameter param = {
        .variable = NULL,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 100, .step = 5}}
    };

    // Should not crash when variable is NULL
    DecrementVariable(&param);

    // Test passes if no crash occurs
    SUCCEED();
}

/**
 * Test DecrementVariable() with negative values
 * Verifies correct handling of negative ranges
 */
TEST_F(DisplayTest, DecrementVariableI8_NegativeRange) {
    int8_t test_var = -40;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = -100, .max = 0, .step = 10}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, -50);
}

/**
 * Test DecrementVariable() with step size of 1
 * Verifies correct operation with minimal step
 */
TEST_F(DisplayTest, DecrementVariableI8_StepOne) {
    int8_t test_var = 42;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 127, .step = 1}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, 41);
}

/**
 * Test DecrementVariable() with TYPE_I16
 * Verifies that 16-bit integer variables are decremented correctly
 */
TEST_F(DisplayTest, DecrementVariableI16_Normal) {
    int16_t test_var = 200;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I16,
        .limits = {.i16 = {.min = 0, .max = 1000, .step = 50}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, 150);
}

/**
 * Test DecrementVariable() with TYPE_I16 at minimum
 */
TEST_F(DisplayTest, DecrementVariableI16_AtMin) {
    int16_t test_var = 30;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I16,
        .limits = {.i16 = {.min = 0, .max = 1000, .step = 50}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, 0);
}

/**
 * Test DecrementVariable() with TYPE_I32
 * Verifies that 32-bit integer variables are decremented correctly
 */
TEST_F(DisplayTest, DecrementVariableI32_Normal) {
    int32_t test_var = 1100;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I32,
        .limits = {.i32 = {.min = 0, .max = 10000, .step = 100}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, 1000);
}

/**
 * Test DecrementVariable() with TYPE_I32 going below minimum
 */
TEST_F(DisplayTest, DecrementVariableI32_BelowMin) {
    int32_t test_var = 50;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I32,
        .limits = {.i32 = {.min = 0, .max = 10000, .step = 100}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, 0);
}

/**
 * Test DecrementVariable() with TYPE_I64
 * Verifies that 64-bit integer variables are decremented correctly
 */
TEST_F(DisplayTest, DecrementVariableI64_Normal) {
    int64_t test_var = 1001000LL;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I64,
        .limits = {.i64 = {.min = 0, .max = 10000000LL, .step = 1000}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, 1000000LL);
}

/**
 * Test DecrementVariable() with TYPE_I64 at minimum
 */
TEST_F(DisplayTest, DecrementVariableI64_AtMin) {
    int64_t test_var = 500LL;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I64,
        .limits = {.i64 = {.min = 0, .max = 10000000LL, .step = 1000}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, 0LL);
}

/**
 * Test DecrementVariable() with TYPE_F32
 * Verifies that floating-point variables are decremented correctly
 */
TEST_F(DisplayTest, DecrementVariableF32_Normal) {
    float test_var = 2.0f;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_F32,
        .limits = {.f32 = {.min = 0.0f, .max = 10.0f, .step = 0.5f}}
    };

    DecrementVariable(&param);

    EXPECT_FLOAT_EQ(test_var, 1.5f);
}

/**
 * Test DecrementVariable() with TYPE_F32 going below minimum
 */
TEST_F(DisplayTest, DecrementVariableF32_BelowMin) {
    float test_var = 0.2f;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_F32,
        .limits = {.f32 = {.min = 0.0f, .max = 10.0f, .step = 0.5f}}
    };

    DecrementVariable(&param);

    EXPECT_FLOAT_EQ(test_var, 0.0f);
}

/**
 * Test DecrementVariable() with TYPE_F32 negative values
 */
TEST_F(DisplayTest, DecrementVariableF32_NegativeRange) {
    float test_var = -4.0f;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_F32,
        .limits = {.f32 = {.min = -10.0f, .max = 0.0f, .step = 1.0f}}
    };

    DecrementVariable(&param);

    EXPECT_FLOAT_EQ(test_var, -5.0f);
}

/**
 * Test DecrementVariable() with TYPE_KeyTypeId
 * Verifies that KeyTypeId enum variables are decremented correctly
 */
TEST_F(DisplayTest, DecrementVariableKeyTypeId_Normal) {
    KeyTypeId test_var = KeyTypeId_Keyer;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_KeyTypeId,
        .limits = {.keyType = {.min = KeyTypeId_Straight, .max = KeyTypeId_Keyer, .step = 1}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, KeyTypeId_Straight);
}

/**
 * Test DecrementVariable() with TYPE_KeyTypeId at minimum
 */
TEST_F(DisplayTest, DecrementVariableKeyTypeId_AtMin) {
    KeyTypeId test_var = KeyTypeId_Straight;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_KeyTypeId,
        .limits = {.keyType = {.min = KeyTypeId_Straight, .max = KeyTypeId_Keyer, .step = 1}}
    };

    DecrementVariable(&param);

    // Should be clamped to min
    EXPECT_EQ(test_var, KeyTypeId_Straight);
}

/**
 * Test DecrementVariable() with TYPE_BOOL
 * Verifies that boolean variables toggle correctly (same as increment)
 */
TEST_F(DisplayTest, DecrementVariableBool_FalseToTrue) {
    bool test_var = false;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_BOOL,
        .limits = {.b = {.min = false, .max = true, .step = 1}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, true);
}

/**
 * Test DecrementVariable() with TYPE_BOOL toggling from true to false
 */
TEST_F(DisplayTest, DecrementVariableBool_TrueToFalse) {
    bool test_var = true;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_BOOL,
        .limits = {.b = {.min = false, .max = true, .step = 1}}
    };

    DecrementVariable(&param);

    EXPECT_EQ(test_var, false);
}

/**
 * Test combined IncrementVariable() and DecrementVariable()
 * Verifies that increment and decrement are inverses of each other
 */
TEST_F(DisplayTest, IncrementDecrementVariable_Inverse) {
    int32_t test_var = 50;
    int32_t original_value = test_var;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I32,
        .limits = {.i32 = {.min = 0, .max = 100, .step = 10}}
    };

    IncrementVariable(&param);
    EXPECT_EQ(test_var, 60);

    DecrementVariable(&param);
    EXPECT_EQ(test_var, original_value);
}

/**
 * Test combined increment/decrement with boundary conditions
 * Verifies correct behavior when crossing boundaries
 */
TEST_F(DisplayTest, IncrementDecrementVariable_Boundaries) {
    int8_t test_var = 5;

    VariableParameter param = {
        .variable = &test_var,
        .type = TYPE_I8,
        .limits = {.i8 = {.min = 0, .max = 10, .step = 8}}
    };

    // Increment should clamp to max (5 + 8 = 13, clamped to 10)
    IncrementVariable(&param);
    EXPECT_EQ(test_var, 10);

    // Decrement should go to 2 (10 - 8 = 2)
    DecrementVariable(&param);
    EXPECT_EQ(test_var, 2);

    // Decrement should clamp to min (2 - 8 = -6, clamped to 0)
    DecrementVariable(&param);
    EXPECT_EQ(test_var, 0);
}

///////////////////////////////////////////////////////////////////////////////
// SecondaryMenuOption Tests - RFSet Menu
///////////////////////////////////////////////////////////////////////////////

/**
 * Test RFSet menu - SSB Power option
 * Verifies SSB power variable parameter is configured correctly
 */
TEST_F(DisplayTest, RFSetMenu_SSBPower_Configuration) {
    // Initialize the radio state
    

    // Access the SSB power variable parameter from RFSet[0]
    extern struct SecondaryMenuOption RFSet[7];
    extern VariableParameter ssbPower;

    // Verify menu option is configured correctly
    EXPECT_STREQ(RFSet[0].label, "SSB Power");
    EXPECT_EQ(RFSet[0].action, variableOption);
    EXPECT_EQ(RFSet[0].varPam, &ssbPower);
    EXPECT_EQ(RFSet[0].func, nullptr);
    EXPECT_EQ(RFSet[0].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(ssbPower.type, TYPE_F32);
    EXPECT_FLOAT_EQ(ssbPower.limits.f32.min, 0.0f);
    EXPECT_FLOAT_EQ(ssbPower.limits.f32.max, 20.0f);
    EXPECT_FLOAT_EQ(ssbPower.limits.f32.step, 0.5f);
}

/**
 * Test RFSet menu - SSB Power increment/decrement
 * Verifies SSB power can be adjusted within limits
 */
TEST_F(DisplayTest, RFSetMenu_SSBPower_IncrementDecrement) {
    

    extern struct SecondaryMenuOption RFSet[7];
    extern VariableParameter ssbPower;
    extern void UpdateArrayVariables(void);

    // Update array variables to point to current band
    UpdateArrayVariables();

    // Set initial power value
    ED.powerOutSSB[ED.currentBand[ED.activeVFO]] = 5.0f;

    // Increment power
    IncrementVariable(&ssbPower);
    EXPECT_FLOAT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 5.5f);

    // Decrement power
    DecrementVariable(&ssbPower);
    EXPECT_FLOAT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 5.0f);
}

/**
 * Test RFSet menu - SSB Power boundary conditions
 * Verifies SSB power respects min/max limits
 */
TEST_F(DisplayTest, RFSetMenu_SSBPower_Boundaries) {
    

    extern VariableParameter ssbPower;
    extern void UpdateArrayVariables(void);

    UpdateArrayVariables();

    // Test max boundary
    ED.powerOutSSB[ED.currentBand[ED.activeVFO]] = 19.8f;
    IncrementVariable(&ssbPower);
    EXPECT_FLOAT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 20.0f);

    IncrementVariable(&ssbPower); // Should stay at max
    EXPECT_FLOAT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 20.0f);

    // Test min boundary
    ED.powerOutSSB[ED.currentBand[ED.activeVFO]] = 0.3f;
    DecrementVariable(&ssbPower);
    EXPECT_FLOAT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 0.0f);

    DecrementVariable(&ssbPower); // Should stay at min
    EXPECT_FLOAT_EQ(ED.powerOutSSB[ED.currentBand[ED.activeVFO]], 0.0f);
}

/**
 * Test RFSet menu - CW Power option
 * Verifies CW power variable parameter is configured correctly
 */
TEST_F(DisplayTest, RFSetMenu_CWPower_Configuration) {
    

    extern struct SecondaryMenuOption RFSet[7];
    extern VariableParameter cwPower;

    // Verify menu option is configured correctly
    EXPECT_STREQ(RFSet[1].label, "CW Power");
    EXPECT_EQ(RFSet[1].action, variableOption);
    EXPECT_EQ(RFSet[1].varPam, &cwPower);
    EXPECT_EQ(RFSet[1].func, nullptr);
    EXPECT_EQ(RFSet[1].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(cwPower.type, TYPE_F32);
    EXPECT_FLOAT_EQ(cwPower.limits.f32.min, 0.0f);
    EXPECT_FLOAT_EQ(cwPower.limits.f32.max, 20.0f);
    EXPECT_FLOAT_EQ(cwPower.limits.f32.step, 0.5f);
}

/**
 * Test RFSet menu - CW Power increment/decrement
 * Verifies CW power can be adjusted within limits
 */
TEST_F(DisplayTest, RFSetMenu_CWPower_IncrementDecrement) {
    

    extern VariableParameter cwPower;
    extern void UpdateArrayVariables(void);

    UpdateArrayVariables();

    // Set initial power value
    ED.powerOutCW[ED.currentBand[ED.activeVFO]] = 10.0f;

    // Increment power
    IncrementVariable(&cwPower);
    EXPECT_FLOAT_EQ(ED.powerOutCW[ED.currentBand[ED.activeVFO]], 10.5f);

    // Decrement power
    DecrementVariable(&cwPower);
    EXPECT_FLOAT_EQ(ED.powerOutCW[ED.currentBand[ED.activeVFO]], 10.0f);
}

/**
 * Test RFSet menu - Gain option
 * Verifies gain variable parameter is configured correctly
 */
TEST_F(DisplayTest, RFSetMenu_Gain_Configuration) {
    

    extern struct SecondaryMenuOption RFSet[7];
    extern VariableParameter gain;

    // Verify menu option is configured correctly
    EXPECT_STREQ(RFSet[2].label, "Gain");
    EXPECT_EQ(RFSet[2].action, variableOption);
    EXPECT_EQ(RFSet[2].varPam, &gain);
    EXPECT_EQ(RFSet[2].func, nullptr);
    EXPECT_EQ(RFSet[2].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(gain.type, TYPE_F32);
    EXPECT_EQ(gain.limits.f32.min, -5);
    EXPECT_EQ(gain.limits.f32.max, 20);
    EXPECT_EQ(gain.limits.f32.step, 0.5);
}

/**
 * Test RFSet menu - Gain increment/decrement
 * Verifies gain can be adjusted within limits
 */
TEST_F(DisplayTest, RFSetMenu_Gain_IncrementDecrement) {
    

    extern VariableParameter gain;

    // Set initial gain value
    ED.rfGainAllBands_dB = 10;

    // Increment gain
    IncrementVariable(&gain);
    EXPECT_EQ(ED.rfGainAllBands_dB, 10.5);

    // Decrement gain
    DecrementVariable(&gain);
    EXPECT_EQ(ED.rfGainAllBands_dB, 10);

    // Test max boundary
    ED.rfGainAllBands_dB = 20;
    IncrementVariable(&gain);
    EXPECT_EQ(ED.rfGainAllBands_dB, 20);

    // Test min boundary
    ED.rfGainAllBands_dB = -5;
    DecrementVariable(&gain);
    EXPECT_EQ(ED.rfGainAllBands_dB, -5);
}

/**
 * Test RFSet menu - RX Attenuation option
 * Verifies RX attenuation variable parameter is configured correctly
 */
TEST_F(DisplayTest, RFSetMenu_RXAttenuation_Configuration) {
    

    extern struct SecondaryMenuOption RFSet[7];
    extern VariableParameter rxAtten;

    // Verify menu option is configured correctly
    EXPECT_STREQ(RFSet[3].label, "RX Attenuation");
    EXPECT_EQ(RFSet[3].action, variableOption);
    EXPECT_EQ(RFSet[3].varPam, &rxAtten);
    EXPECT_EQ(RFSet[3].func, nullptr);
    EXPECT_EQ(RFSet[3].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(rxAtten.type, TYPE_F32);
    EXPECT_EQ(rxAtten.limits.f32.min, 0);
    EXPECT_EQ(rxAtten.limits.f32.max, 31.5);
    EXPECT_EQ(rxAtten.limits.f32.step, 0.5);
}

/**
 * Test RFSet menu - RX Attenuation increment/decrement
 * Verifies RX attenuation can be adjusted within limits
 */
TEST_F(DisplayTest, RFSetMenu_RXAttenuation_IncrementDecrement) {
    

    extern VariableParameter rxAtten;
    extern void UpdateArrayVariables(void);

    UpdateArrayVariables();

    // Set initial attenuation value
    ED.RAtten[ED.currentBand[ED.activeVFO]] = 10;

    // Increment attenuation
    IncrementVariable(&rxAtten);
    EXPECT_EQ(ED.RAtten[ED.currentBand[ED.activeVFO]], 10.5);

    // Decrement attenuation
    DecrementVariable(&rxAtten);
    EXPECT_EQ(ED.RAtten[ED.currentBand[ED.activeVFO]], 10);
}

/**
 * Test RFSet menu - TX Attenuation (CW) option
 * Verifies TX CW attenuation variable parameter is configured correctly
 */
TEST_F(DisplayTest, RFSetMenu_TXAttenuationCW_Configuration) {
    

    extern struct SecondaryMenuOption RFSet[7];
    extern VariableParameter txAttenCW;

    // Verify menu option is configured correctly
    EXPECT_STREQ(RFSet[4].label, "TX Attenuation (CW)");
    EXPECT_EQ(RFSet[4].action, variableOption);
    EXPECT_EQ(RFSet[4].varPam, &txAttenCW);
    EXPECT_EQ(RFSet[4].func, nullptr);
    EXPECT_EQ(RFSet[4].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(txAttenCW.type, TYPE_F32);
    EXPECT_EQ(txAttenCW.limits.f32.min, 0);
    EXPECT_EQ(txAttenCW.limits.f32.max, 31.5);
    EXPECT_EQ(txAttenCW.limits.f32.step, 0.5);
}

/**
 * Test RFSet menu - TX Attenuation (CW) increment/decrement
 * Verifies TX CW attenuation can be adjusted within limits
 */
TEST_F(DisplayTest, RFSetMenu_TXAttenuationCW_IncrementDecrement) {
    

    extern VariableParameter txAttenCW;
    extern void UpdateArrayVariables(void);

    UpdateArrayVariables();

    // Set initial attenuation value
    ED.XAttenCW[ED.currentBand[ED.activeVFO]] = 15;

    // Increment attenuation
    IncrementVariable(&txAttenCW);
    EXPECT_EQ(ED.XAttenCW[ED.currentBand[ED.activeVFO]], 15.5);

    // Decrement attenuation
    DecrementVariable(&txAttenCW);
    EXPECT_EQ(ED.XAttenCW[ED.currentBand[ED.activeVFO]], 15);
}

/**
 * Test RFSet menu - TX Attenuation (SSB) option
 * Verifies TX SSB attenuation variable parameter is configured correctly
 */
TEST_F(DisplayTest, RFSetMenu_TXAttenuationSSB_Configuration) {
    

    extern struct SecondaryMenuOption RFSet[7];
    extern VariableParameter txAttenSSB;

    // Verify menu option is configured correctly
    EXPECT_STREQ(RFSet[5].label, "TX Attenuation (SSB)");
    EXPECT_EQ(RFSet[5].action, variableOption);
    EXPECT_EQ(RFSet[5].varPam, &txAttenSSB);
    EXPECT_EQ(RFSet[5].func, nullptr);
    EXPECT_EQ(RFSet[5].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(txAttenSSB.type, TYPE_F32);
    EXPECT_EQ(txAttenSSB.limits.f32.min, 0);
    EXPECT_EQ(txAttenSSB.limits.f32.max, 31.5);
    EXPECT_EQ(txAttenSSB.limits.f32.step, 0.5);
}

/**
 * Test RFSet menu - TX Attenuation (SSB) increment/decrement
 * Verifies TX SSB attenuation can be adjusted within limits
 */
TEST_F(DisplayTest, RFSetMenu_TXAttenuationSSB_IncrementDecrement) {
    

    extern VariableParameter txAttenSSB;
    extern void UpdateArrayVariables(void);

    UpdateArrayVariables();

    // Set initial attenuation value
    ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = 8;

    // Increment attenuation
    IncrementVariable(&txAttenSSB);
    EXPECT_EQ(ED.XAttenSSB[ED.currentBand[ED.activeVFO]], 8.5);

    // Decrement attenuation
    DecrementVariable(&txAttenSSB);
    EXPECT_EQ(ED.XAttenSSB[ED.currentBand[ED.activeVFO]], 8);
}

/**
 * Test RFSet menu - Antenna option
 * Verifies antenna variable parameter is configured correctly
 */
TEST_F(DisplayTest, RFSetMenu_Antenna_Configuration) {
    

    extern struct SecondaryMenuOption RFSet[7];
    extern VariableParameter antenna;

    // Verify menu option is configured correctly
    EXPECT_STREQ(RFSet[6].label, "Antenna");
    EXPECT_EQ(RFSet[6].action, variableOption);
    EXPECT_EQ(RFSet[6].varPam, &antenna);
    EXPECT_EQ(RFSet[6].func, nullptr);
    EXPECT_EQ(RFSet[6].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(antenna.type, TYPE_I32);
    EXPECT_EQ(antenna.limits.i32.min, 0);
    EXPECT_EQ(antenna.limits.i32.max, 3);
    EXPECT_EQ(antenna.limits.i32.step, 1);
}

/**
 * Test RFSet menu - Antenna increment/decrement
 * Verifies antenna selection can be adjusted within limits (0-2)
 */
TEST_F(DisplayTest, RFSetMenu_Antenna_IncrementDecrement) {

    extern VariableParameter antenna;
    extern void UpdateArrayVariables(void);

    UpdateArrayVariables();

    // Set initial antenna value
    ED.antennaSelection[ED.currentBand[ED.activeVFO]] = 0;

    // Increment antenna
    IncrementVariable(&antenna);
    EXPECT_EQ(ED.antennaSelection[ED.currentBand[ED.activeVFO]], 1);

    IncrementVariable(&antenna);
    EXPECT_EQ(ED.antennaSelection[ED.currentBand[ED.activeVFO]], 2);

    // Test max boundary
    IncrementVariable(&antenna);
    IncrementVariable(&antenna);
    EXPECT_EQ(ED.antennaSelection[ED.currentBand[ED.activeVFO]], 3);

    // Decrement antenna
    DecrementVariable(&antenna);
    EXPECT_EQ(ED.antennaSelection[ED.currentBand[ED.activeVFO]], 2);
}

///////////////////////////////////////////////////////////////////////////////
// SecondaryMenuOption Tests - CWOptions Menu
///////////////////////////////////////////////////////////////////////////////

/**
 * Test CWOptions menu - WPM option
 * Verifies WPM variable parameter is configured correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_WPM_Configuration) {
    

    extern struct SecondaryMenuOption CWOptions[6];
    extern VariableParameter wpm;
    extern void UpdateDitLength(void);

    // Verify menu option is configured correctly
    EXPECT_STREQ(CWOptions[0].label, "WPM");
    EXPECT_EQ(CWOptions[0].action, variableOption);
    EXPECT_EQ(CWOptions[0].varPam, &wpm);
    EXPECT_EQ(CWOptions[0].func, nullptr);
    EXPECT_EQ(CWOptions[0].postUpdateFunc, (void *)UpdateDitLength);

    // Verify variable parameter configuration
    EXPECT_EQ(wpm.type, TYPE_I32);
    EXPECT_EQ(wpm.limits.i32.min, 5);
    EXPECT_EQ(wpm.limits.i32.max, 50);
    EXPECT_EQ(wpm.limits.i32.step, 1);
}

/**
 * Test CWOptions menu - WPM increment/decrement
 * Verifies WPM can be adjusted within limits
 */
TEST_F(DisplayTest, CWOptionsMenu_WPM_IncrementDecrement) {
    

    extern VariableParameter wpm;

    // Set initial WPM value
    ED.currentWPM = 20;

    // Increment WPM
    IncrementVariable(&wpm);
    EXPECT_EQ(ED.currentWPM, 21);

    // Decrement WPM
    DecrementVariable(&wpm);
    EXPECT_EQ(ED.currentWPM, 20);

    // Test max boundary
    ED.currentWPM = 50;
    IncrementVariable(&wpm);
    EXPECT_EQ(ED.currentWPM, 50);

    // Test min boundary
    ED.currentWPM = 5;
    DecrementVariable(&wpm);
    EXPECT_EQ(ED.currentWPM, 5);
}

/**
 * Test CWOptions menu - Straight key option
 * Verifies straight key function option is configured correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_StraightKey_Configuration) {
    

    extern struct SecondaryMenuOption CWOptions[6];
    extern void SelectStraightKey(void);

    // Verify menu option is configured correctly
    EXPECT_STREQ(CWOptions[1].label, "Straight key");
    EXPECT_EQ(CWOptions[1].action, functionOption);
    EXPECT_EQ(CWOptions[1].varPam, nullptr);
    EXPECT_EQ(CWOptions[1].func, (void *)SelectStraightKey);
    EXPECT_EQ(CWOptions[1].postUpdateFunc, nullptr);
}

/**
 * Test CWOptions menu - Straight key function
 * Verifies SelectStraightKey function sets keyType correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_StraightKey_Function) {
    

    extern void SelectStraightKey(void);

    // Set initial key type to keyer
    ED.keyType = KeyTypeId_Keyer;

    // Call straight key selection function
    SelectStraightKey();

    // Verify key type changed to straight
    EXPECT_EQ(ED.keyType, KeyTypeId_Straight);
}

/**
 * Test CWOptions menu - Keyer option
 * Verifies keyer function option is configured correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_Keyer_Configuration) {
    

    extern struct SecondaryMenuOption CWOptions[6];
    extern void SelectKeyer(void);

    // Verify menu option is configured correctly
    EXPECT_STREQ(CWOptions[2].label, "Keyer");
    EXPECT_EQ(CWOptions[2].action, functionOption);
    EXPECT_EQ(CWOptions[2].varPam, nullptr);
    EXPECT_EQ(CWOptions[2].func, (void *)SelectKeyer);
    EXPECT_EQ(CWOptions[2].postUpdateFunc, nullptr);
}

/**
 * Test CWOptions menu - Keyer function
 * Verifies SelectKeyer function sets keyType correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_Keyer_Function) {
    

    extern void SelectKeyer(void);

    // Set initial key type to straight
    ED.keyType = KeyTypeId_Straight;

    // Call keyer selection function
    SelectKeyer();

    // Verify key type changed to keyer
    EXPECT_EQ(ED.keyType, KeyTypeId_Keyer);
}

/**
 * Test CWOptions menu - Flip paddle option
 * Verifies flip paddle function option is configured correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_FlipPaddle_Configuration) {
    

    extern struct SecondaryMenuOption CWOptions[6];
    extern void FlipPaddle(void);

    // Verify menu option is configured correctly
    EXPECT_STREQ(CWOptions[3].label, "Flip paddle");
    EXPECT_EQ(CWOptions[3].action, functionOption);
    EXPECT_EQ(CWOptions[3].varPam, nullptr);
    EXPECT_EQ(CWOptions[3].func, (void *)FlipPaddle);
    EXPECT_EQ(CWOptions[3].postUpdateFunc, nullptr);
}

/**
 * Test CWOptions menu - Flip paddle function
 * Verifies FlipPaddle function toggles keyerFlip correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_FlipPaddle_Function) {
    

    extern void FlipPaddle(void);

    // Set initial flip state to false
    ED.keyerFlip = false;

    // Call flip paddle function
    FlipPaddle();

    // Verify flip state toggled to true
    EXPECT_EQ(ED.keyerFlip, true);

    // Call again to toggle back
    FlipPaddle();

    // Verify flip state toggled back to false
    EXPECT_EQ(ED.keyerFlip, false);
}

/**
 * Test CWOptions menu - CW Filter option
 * Verifies CW filter variable parameter is configured correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_CWFilter_Configuration) {
    

    extern struct SecondaryMenuOption CWOptions[6];
    extern VariableParameter cwf;

    // Verify menu option is configured correctly
    EXPECT_STREQ(CWOptions[4].label, "CW Filter");
    EXPECT_EQ(CWOptions[4].action, variableOption);
    EXPECT_EQ(CWOptions[4].varPam, &cwf);
    EXPECT_EQ(CWOptions[4].func, nullptr);
    EXPECT_EQ(CWOptions[4].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(cwf.type, TYPE_I32);
    EXPECT_EQ(cwf.limits.i32.min, 0);
    EXPECT_EQ(cwf.limits.i32.max, 5);
    EXPECT_EQ(cwf.limits.i32.step, 1);
}

/**
 * Test CWOptions menu - CW Filter increment/decrement
 * Verifies CW filter index can be adjusted within limits
 */
TEST_F(DisplayTest, CWOptionsMenu_CWFilter_IncrementDecrement) {
    

    extern VariableParameter cwf;

    // Set initial filter index
    ED.CWFilterIndex = 2;

    // Increment filter index
    IncrementVariable(&cwf);
    EXPECT_EQ(ED.CWFilterIndex, 3);

    // Decrement filter index
    DecrementVariable(&cwf);
    EXPECT_EQ(ED.CWFilterIndex, 2);

    // Test max boundary
    ED.CWFilterIndex = 5;
    IncrementVariable(&cwf);
    EXPECT_EQ(ED.CWFilterIndex, 5);

    // Test min boundary
    ED.CWFilterIndex = 0;
    DecrementVariable(&cwf);
    EXPECT_EQ(ED.CWFilterIndex, 0);
}

/**
 * Test CWOptions menu - Sidetone volume option
 * Verifies sidetone volume variable parameter is configured correctly
 */
TEST_F(DisplayTest, CWOptionsMenu_SidetoneVolume_Configuration) {
    

    extern struct SecondaryMenuOption CWOptions[6];
    extern VariableParameter stv;

    // Verify menu option is configured correctly
    EXPECT_STREQ(CWOptions[5].label, "Sidetone volume");
    EXPECT_EQ(CWOptions[5].action, variableOption);
    EXPECT_EQ(CWOptions[5].varPam, &stv);
    EXPECT_EQ(CWOptions[5].func, nullptr);
    EXPECT_EQ(CWOptions[5].postUpdateFunc, nullptr);

    // Verify variable parameter configuration
    EXPECT_EQ(stv.type, TYPE_F32);
    EXPECT_FLOAT_EQ(stv.limits.f32.min, 0.0f);
    EXPECT_FLOAT_EQ(stv.limits.f32.max, 100.0f);
    EXPECT_FLOAT_EQ(stv.limits.f32.step, 0.5f);
}

/**
 * Test CWOptions menu - Sidetone volume increment/decrement
 * Verifies sidetone volume can be adjusted within limits
 */
TEST_F(DisplayTest, CWOptionsMenu_SidetoneVolume_IncrementDecrement) {
    

    extern VariableParameter stv;

    // Set initial sidetone volume
    ED.sidetoneVolume = 50.0f;

    // Increment volume
    IncrementVariable(&stv);
    EXPECT_FLOAT_EQ(ED.sidetoneVolume, 50.5f);

    // Decrement volume
    DecrementVariable(&stv);
    EXPECT_FLOAT_EQ(ED.sidetoneVolume, 50.0f);

    // Test max boundary
    ED.sidetoneVolume = 99.8f;
    IncrementVariable(&stv);
    EXPECT_FLOAT_EQ(ED.sidetoneVolume, 100.0f);
    IncrementVariable(&stv);
    EXPECT_FLOAT_EQ(ED.sidetoneVolume, 100.0f);

    // Test min boundary
    ED.sidetoneVolume = 0.3f;
    DecrementVariable(&stv);
    EXPECT_FLOAT_EQ(ED.sidetoneVolume, 0.0f);
    DecrementVariable(&stv);
    EXPECT_FLOAT_EQ(ED.sidetoneVolume, 0.0f);
}
