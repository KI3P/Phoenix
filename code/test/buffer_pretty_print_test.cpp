#include "gtest/gtest.h"
#include "../src/PhoenixSketch/SDT.h"

class BufferPrettyPrintTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset buffer state
        buffer.head = 0;
        buffer.count = 0;
        hardwareRegister = 0;
    }
};

TEST_F(BufferPrettyPrintTest, PrintEmptyBuffer) {
    // Test printing an empty buffer
    EXPECT_NO_THROW(buffer_pretty_print());
}

TEST_F(BufferPrettyPrintTest, PrintBufferWithData) {
    // Add some test data to the buffer
    hardwareRegister = 0x12345678;
    buffer_add();

    hardwareRegister = 0xDEADBEEF;
    buffer_add();

    hardwareRegister = 0x00FF00FF;
    buffer_add();

    // Test printing buffer with data
    EXPECT_NO_THROW(buffer_pretty_print());

    // Verify buffer still has the correct data
    EXPECT_EQ(buffer.count, 3);
    EXPECT_EQ(buffer.entries[0].register_value, 0x12345678);
    EXPECT_EQ(buffer.entries[1].register_value, 0xDEADBEEF);
    EXPECT_EQ(buffer.entries[2].register_value, 0x00FF00FF);
}

TEST_F(BufferPrettyPrintTest, PrintFullBuffer) {
    // Fill the buffer to capacity
    for (int i = 0; i < REGISTER_BUFFER_SIZE; i++) {
        hardwareRegister = 0x1000 + i;
        buffer_add();
    }

    // Test printing full buffer
    EXPECT_NO_THROW(buffer_pretty_print());
    EXPECT_EQ(buffer.count, REGISTER_BUFFER_SIZE);
}

TEST_F(BufferPrettyPrintTest, PrintOverflowBuffer) {
    // Add more entries than buffer capacity to test wraparound
    for (int i = 0; i < REGISTER_BUFFER_SIZE + 10; i++) {
        hardwareRegister = 0x2000 + i;
        buffer_add();
    }

    // Test printing buffer with wraparound
    EXPECT_NO_THROW(buffer_pretty_print());
    EXPECT_EQ(buffer.count, REGISTER_BUFFER_SIZE);

    // Verify oldest entries were overwritten (wraparound)
    // The head should point to the oldest remaining entry
    EXPECT_EQ(buffer.head, 10); // (REGISTER_BUFFER_SIZE + 10) % REGISTER_BUFFER_SIZE
}