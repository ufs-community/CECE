// SPDX-License-Identifier: Apache-2.0
// main() for CECE's Kokkos-only test executables (no MPI, no MPI launcher);
// see cece_test_environment.hpp. MPI test executables link cece_test_main.
#include <gtest/gtest.h>

#include "support/cece_test_environment.hpp"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new cece::test::TestEnvironment(argc, argv, /*with_mpi=*/false));
    for (const auto& make_environment : cece::test::queued_environments()) {
        ::testing::AddGlobalTestEnvironment(make_environment());
    }
    return RUN_ALL_TESTS();
}
