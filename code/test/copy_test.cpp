#include "gtest/gtest.h"

extern "C" {
#include "../src/PhoenixSketch/copy.h"
}

TEST(copy, Check_Return_Destination) {
    char src[] = "hello";
    char dst[sizeof(src)];
    void *rv = copy(dst,src,sizeof(src));
    ASSERT_EQ(rv, dst);
}

TEST(DummyTest, AlwaysPasses) {
    EXPECT_EQ(1, 1);
}