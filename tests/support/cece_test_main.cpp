// SPDX-License-Identifier: Apache-2.0
// main() for CECE's MPI + Kokkos test executables (see cece_test_environment.hpp).
// Kokkos-only executables link cece_test_main_kokkos instead.
#include <gtest/gtest.h>

#include "support/cece_test_environment.hpp"
#include "support/cece_test_teardown_guard.hpp"

// gtest answers --gtest_list_tests before any Environment::SetUp runs, so
// listing never initializes MPI or Kokkos. The guard below hard-exits every
// run (listing or not) before process teardown; see
// cece_test_teardown_guard.hpp.
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new cece::test::TestEnvironment(argc, argv, /*with_mpi=*/true));
    for (const auto& make_environment : cece::test::queued_environments()) {
        ::testing::AddGlobalTestEnvironment(make_environment());
    }
    const int rc = RUN_ALL_TESTS();
    cece::test::exit_after_tests(rc);
}
