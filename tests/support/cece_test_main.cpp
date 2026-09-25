// SPDX-License-Identifier: Apache-2.0
// main() for CECE's MPI + Kokkos test executables (see cece_test_environment.hpp).
// Kokkos-only executables link cece_test_main_kokkos instead.
#include <gtest/gtest.h>

#include "support/cece_test_environment.hpp"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new cece::test::TestEnvironment(argc, argv, /*with_mpi=*/true));
    for (const auto& make_environment : cece::test::queued_environments()) {
        ::testing::AddGlobalTestEnvironment(make_environment());
    }
    return RUN_ALL_TESTS();
}
