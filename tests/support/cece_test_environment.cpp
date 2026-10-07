// SPDX-License-Identifier: Apache-2.0
#include "support/cece_test_environment.hpp"

#include <mpi.h>

#include <Kokkos_Core.hpp>
#include <halo/environment.hpp>
#include <utility>
#include <vector>

namespace cece::test {

void TestEnvironment::SetUp() {
    if (with_mpi_) {
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc_, &argv_, MPI_THREAD_MULTIPLE, &provided);
            owns_mpi_ = true;
        }
        // Records the MPI thread level for HALO; idempotent, and the same call
        // the production driver makes right after MPI init.
        halo::Environment::initialize();
    }
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize(argc_, argv_);
        owns_kokkos_ = true;
    }
}

void TestEnvironment::TearDown() {
    if (owns_kokkos_ && Kokkos::is_initialized()) {
        Kokkos::finalize();
    }
    if (owns_mpi_) {
        int finalized = 0;
        MPI_Finalized(&finalized);
        if (!finalized) MPI_Finalize();
    }
}

namespace {
std::vector<EnvironmentFactory>& registry() {
    static std::vector<EnvironmentFactory> factories;
    return factories;
}
}  // namespace

bool add_environment(EnvironmentFactory factory) {
    registry().push_back(std::move(factory));
    return true;
}

const std::vector<EnvironmentFactory>& queued_environments() {
    return registry();
}

}  // namespace cece::test
