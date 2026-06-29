# include <gtest/gtest.h>
// Include the header of the code you want to test!
#include "likegit/core.hpp"
// TEST( TestSuiteName , IndividualTestName )

TEST(CoreTests , MathWorksCorrectly ) {
int result = 2 + 2;
// Use EXPECT_EQ for equality . If it fails , the test continues .

EXPECT_EQ (result , 4);
// Use ASSERT_TRUE for booleans . If it fails , the test immediately halts .
ASSERT_TRUE ( result > 0);
}


TEST(CoreTests , StringComparison ) {
std::string text = "hello";
EXPECT_EQ(text, "hello");
}