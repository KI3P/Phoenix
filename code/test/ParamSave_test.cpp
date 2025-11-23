#include "gtest/gtest.h"
#include "../src/PhoenixSketch/ParamSave.h"

class ParamSaveTest : public ::testing::Test {
protected:
    void SetUp() override {
        ClearSavedParams();
    }
};

// Variable save/restore tests
TEST_F(ParamSaveTest, SaveAndRestoreVariable) {
    int32_t original = 12345;
    int32_t restored = 0;

    ASSERT_TRUE(SaveVariable(0, &original, sizeof(original)));
    ASSERT_TRUE(RestoreVariable(0, &restored, sizeof(restored)));
    EXPECT_EQ(original, restored);
}

TEST_F(ParamSaveTest, SaveMultipleVariables) {
    int32_t var0 = 100, var1 = 200, var2 = 300;
    int32_t res0 = 0, res1 = 0, res2 = 0;

    ASSERT_TRUE(SaveVariable(0, &var0, sizeof(var0)));
    ASSERT_TRUE(SaveVariable(1, &var1, sizeof(var1)));
    ASSERT_TRUE(SaveVariable(2, &var2, sizeof(var2)));

    ASSERT_TRUE(RestoreVariable(0, &res0, sizeof(res0)));
    ASSERT_TRUE(RestoreVariable(1, &res1, sizeof(res1)));
    ASSERT_TRUE(RestoreVariable(2, &res2, sizeof(res2)));

    EXPECT_EQ(var0, res0);
    EXPECT_EQ(var1, res1);
    EXPECT_EQ(var2, res2);
}

TEST_F(ParamSaveTest, SaveVariableInvalidSlot) {
    int32_t value = 42;
    EXPECT_FALSE(SaveVariable(MAX_SAVED_VARS, &value, sizeof(value)));
    EXPECT_FALSE(SaveVariable(255, &value, sizeof(value)));
}

TEST_F(ParamSaveTest, RestoreVariableNotSaved) {
    int32_t value = 0;
    EXPECT_FALSE(RestoreVariable(0, &value, sizeof(value)));
}

TEST_F(ParamSaveTest, RestoreVariableSizeMismatch) {
    int32_t original = 12345;
    int64_t restored = 0;

    ASSERT_TRUE(SaveVariable(0, &original, sizeof(original)));
    EXPECT_FALSE(RestoreVariable(0, &restored, sizeof(restored)));
}

TEST_F(ParamSaveTest, SaveVariableNullPointer) {
    EXPECT_FALSE(SaveVariable(0, nullptr, 4));
}

TEST_F(ParamSaveTest, RestoreVariableNullPointer) {
    int32_t original = 42;
    SaveVariable(0, &original, sizeof(original));
    EXPECT_FALSE(RestoreVariable(0, nullptr, sizeof(original)));
}

TEST_F(ParamSaveTest, SaveVariableZeroSize) {
    int32_t value = 42;
    EXPECT_FALSE(SaveVariable(0, &value, 0));
}

TEST_F(ParamSaveTest, SaveVariableOverwrite) {
    int32_t first = 100;
    int32_t second = 200;
    int32_t restored = 0;

    ASSERT_TRUE(SaveVariable(0, &first, sizeof(first)));
    ASSERT_TRUE(SaveVariable(0, &second, sizeof(second)));
    ASSERT_TRUE(RestoreVariable(0, &restored, sizeof(restored)));

    EXPECT_EQ(second, restored);
}

// Array save/restore tests
TEST_F(ParamSaveTest, SaveAndRestoreArray) {
    float original[12] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
    float restored[12] = {0};

    ASSERT_TRUE(SaveArray(0, original, sizeof(original)));
    ASSERT_TRUE(RestoreArray(0, restored, sizeof(restored)));

    for (int i = 0; i < 12; i++) {
        EXPECT_FLOAT_EQ(original[i], restored[i]);
    }
}

TEST_F(ParamSaveTest, SaveMultipleArrays) {
    int32_t arr0[5] = {1, 2, 3, 4, 5};
    int32_t arr1[5] = {10, 20, 30, 40, 50};
    int32_t res0[5] = {0};
    int32_t res1[5] = {0};

    ASSERT_TRUE(SaveArray(0, arr0, sizeof(arr0)));
    ASSERT_TRUE(SaveArray(1, arr1, sizeof(arr1)));

    ASSERT_TRUE(RestoreArray(0, res0, sizeof(res0)));
    ASSERT_TRUE(RestoreArray(1, res1, sizeof(res1)));

    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(arr0[i], res0[i]);
        EXPECT_EQ(arr1[i], res1[i]);
    }
}

TEST_F(ParamSaveTest, SaveArrayInvalidSlot) {
    int32_t arr[5] = {1, 2, 3, 4, 5};
    EXPECT_FALSE(SaveArray(MAX_SAVED_ARRAYS, arr, sizeof(arr)));
    EXPECT_FALSE(SaveArray(255, arr, sizeof(arr)));
}

TEST_F(ParamSaveTest, RestoreArrayNotSaved) {
    int32_t arr[5] = {0};
    EXPECT_FALSE(RestoreArray(0, arr, sizeof(arr)));
}

TEST_F(ParamSaveTest, RestoreArraySizeMismatch) {
    int32_t original[5] = {1, 2, 3, 4, 5};
    int32_t restored[10] = {0};

    ASSERT_TRUE(SaveArray(0, original, sizeof(original)));
    EXPECT_FALSE(RestoreArray(0, restored, sizeof(restored)));
}

TEST_F(ParamSaveTest, SaveArrayTooLarge) {
    uint8_t large_array[MAX_ARRAY_SIZE + 1];
    EXPECT_FALSE(SaveArray(0, large_array, sizeof(large_array)));
}

// IsVariableSaved / IsArraySaved tests
TEST_F(ParamSaveTest, IsVariableSavedInitiallyFalse) {
    for (uint8_t i = 0; i < MAX_SAVED_VARS; i++) {
        EXPECT_FALSE(IsVariableSaved(i));
    }
}

TEST_F(ParamSaveTest, IsVariableSavedAfterSave) {
    int32_t value = 42;
    SaveVariable(2, &value, sizeof(value));

    EXPECT_FALSE(IsVariableSaved(0));
    EXPECT_FALSE(IsVariableSaved(1));
    EXPECT_TRUE(IsVariableSaved(2));
    EXPECT_FALSE(IsVariableSaved(3));
    EXPECT_FALSE(IsVariableSaved(4));
}

TEST_F(ParamSaveTest, IsVariableSavedInvalidSlot) {
    EXPECT_FALSE(IsVariableSaved(MAX_SAVED_VARS));
}

TEST_F(ParamSaveTest, IsArraySavedInitiallyFalse) {
    for (uint8_t i = 0; i < MAX_SAVED_ARRAYS; i++) {
        EXPECT_FALSE(IsArraySaved(i));
    }
}

TEST_F(ParamSaveTest, IsArraySavedAfterSave) {
    int32_t arr[5] = {1, 2, 3, 4, 5};
    SaveArray(1, arr, sizeof(arr));

    EXPECT_FALSE(IsArraySaved(0));
    EXPECT_TRUE(IsArraySaved(1));
}

TEST_F(ParamSaveTest, IsArraySavedInvalidSlot) {
    EXPECT_FALSE(IsArraySaved(MAX_SAVED_ARRAYS));
}

// ClearSavedParams tests
TEST_F(ParamSaveTest, ClearSavedParamsResetsVariables) {
    int32_t value = 42;
    SaveVariable(0, &value, sizeof(value));
    SaveVariable(1, &value, sizeof(value));

    ASSERT_TRUE(IsVariableSaved(0));
    ASSERT_TRUE(IsVariableSaved(1));

    ClearSavedParams();

    EXPECT_FALSE(IsVariableSaved(0));
    EXPECT_FALSE(IsVariableSaved(1));
}

TEST_F(ParamSaveTest, ClearSavedParamsResetsArrays) {
    int32_t arr[5] = {1, 2, 3, 4, 5};
    SaveArray(0, arr, sizeof(arr));
    SaveArray(1, arr, sizeof(arr));

    ASSERT_TRUE(IsArraySaved(0));
    ASSERT_TRUE(IsArraySaved(1));

    ClearSavedParams();

    EXPECT_FALSE(IsArraySaved(0));
    EXPECT_FALSE(IsArraySaved(1));
}

// Test with different data types
TEST_F(ParamSaveTest, SaveDifferentDataTypes) {
    float f = 3.14159f;
    double d = 2.71828;
    uint8_t u8 = 255;
    int64_t i64 = -9223372036854775807LL;

    float rf = 0;
    double rd = 0;
    uint8_t ru8 = 0;
    int64_t ri64 = 0;

    ASSERT_TRUE(SaveVariable(0, &f, sizeof(f)));
    ASSERT_TRUE(SaveVariable(1, &d, sizeof(d)));
    ASSERT_TRUE(SaveVariable(2, &u8, sizeof(u8)));
    ASSERT_TRUE(SaveVariable(3, &i64, sizeof(i64)));

    ASSERT_TRUE(RestoreVariable(0, &rf, sizeof(rf)));
    ASSERT_TRUE(RestoreVariable(1, &rd, sizeof(rd)));
    ASSERT_TRUE(RestoreVariable(2, &ru8, sizeof(ru8)));
    ASSERT_TRUE(RestoreVariable(3, &ri64, sizeof(ri64)));

    EXPECT_FLOAT_EQ(f, rf);
    EXPECT_DOUBLE_EQ(d, rd);
    EXPECT_EQ(u8, ru8);
    EXPECT_EQ(i64, ri64);
}
