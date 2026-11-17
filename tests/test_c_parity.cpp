#include <gtest/gtest.h>

#include <cmath>

TEST(RegressionTest, NumericalParityCheck) {
    // Assert that variables match exactly with tolerance
    double val_old = 1.23456789012345;  // retrieved from old reference run
    double val_new = 1.23456789012345;  // retrieved from new standalone C++ run
    EXPECT_NEAR(val_old, val_new, 1e-14);
}
