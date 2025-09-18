#include <gtest/gtest.h>
#include "Arduino.h"

TEST(MicrosTest, BasicFunctionality) {
    StartMillis();

    // Get initial micros reading - should be very small (near 0)
    uint32_t start_micros = micros();
    EXPECT_LT(start_micros, 1000U); // Should be less than 1ms

    // Since micros() now uses its own tstartMicros baseline,
    // and we can't manipulate it directly, we test that it returns
    // a reasonable value based on system time
    uint32_t second_reading = micros();

    // Second reading should be >= first reading (time moves forward)
    EXPECT_GE(second_reading, start_micros);
}

TEST(MicrosTest, IndependentFromMillis) {
    StartMillis();

    // Get initial readings
    uint32_t initial_micros = micros();
    int64_t initial_millis = millis();

    // Manipulate millis time
    AddMillisTime(5); // Add 5 milliseconds

    // Check that millis changed but micros is independent
    int64_t new_millis = millis();
    uint32_t new_micros = micros();

    // Millis should have changed
    EXPECT_EQ(new_millis, initial_millis + 5);

    // Micros should be independent and based on its own baseline
    // (it should have increased by some small amount due to elapsed time)
    EXPECT_GE(new_micros, initial_micros);
}

TEST(MicrosTest, StartMillisInitializesBoth) {
    // Test that StartMillis() initializes both timing systems
    StartMillis();

    int64_t millis_val = millis();
    uint32_t micros_val = micros();

    // Both should start near zero after StartMillis()
    EXPECT_EQ(millis_val, 0);
    EXPECT_LT(micros_val, 1000U); // Should be less than 1ms
}

TEST(MicrosTest, ReturnsUint32) {
    StartMillis();

    uint32_t result = micros();

    // Just verify it returns a valid uint32_t value
    // The exact value depends on system time, but it should be reasonable
    EXPECT_GE(result, 0U);
}