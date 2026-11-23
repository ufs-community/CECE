// SPDX-License-Identifier: Apache-2.0
// main() for CECE's default (no-MPI, no-Kokkos-environment) test executables.
// Behaviorally identical to GTest::gtest_main except that it hard-exits after
// RUN_ALL_TESTS instead of running process teardown; see
// support/cece_test_teardown_guard.hpp. MPI/Kokkos executables link
// cece_test_main or cece_test_main_kokkos, which apply the same guard.
#include <gtest/gtest.h>

#include "support/cece_test_teardown_guard.hpp"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    const int rc = RUN_ALL_TESTS();
    cece::test::exit_after_tests(rc);
}
