// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared process-level test environment for CECE's MPI + Kokkos test
// executables, plus the hook a test file uses to add its own environment.
//
// Link `cece_test_main` (MPI + HALO + Kokkos) or `cece_test_main_kokkos`
// (Kokkos only, for tests that never touch MPI and carry no MPI launcher)
// instead of `GTest::gtest_main`. Each supplies main(), which registers
// TestEnvironment first and then every environment queued with
// CECE_TEST_ADD_ENVIRONMENT. gtest sets environments up in registration
// order and tears them down in reverse, so a file's own environment always
// runs with MPI and Kokkos alive on both sides.
//
// Nothing here initializes anything before RUN_ALL_TESTS(): gtest answers
// `--gtest_list_tests` before it sets up any Environment, so test discovery
// (build-time or ctest-time, launched or not) never touches MPI or Kokkos.
// That is what makes a per-file "is this a discovery run?" guard unnecessary.

#include <gtest/gtest.h>

#include <functional>
#include <vector>

namespace cece::test {

/// Initializes Kokkos — and, with `with_mpi`, MPI (MPI_THREAD_MULTIPLE) and
/// HALO first — in SetUp, each only if not already initialized, and finalizes
/// in TearDown only what it initialized itself.
class TestEnvironment : public ::testing::Environment {
   public:
    TestEnvironment(int argc, char** argv, bool with_mpi) : argc_(argc), argv_(argv), with_mpi_(with_mpi) {}
    void SetUp() override;
    void TearDown() override;

   private:
    int argc_;
    char** argv_;
    bool with_mpi_;
    bool owns_mpi_ = false;
    bool owns_kokkos_ = false;
};

using EnvironmentFactory = std::function<::testing::Environment*()>;

/// Queue an additional global environment. main() registers the queued
/// factories after TestEnvironment, in the order they were added.
/// Returns true so it can be used in a static initializer.
bool add_environment(EnvironmentFactory factory);

/// The queued factories, consumed by main().
const std::vector<EnvironmentFactory>& queued_environments();

}  // namespace cece::test

/// File-scope registration of an extra ::testing::Environment subclass.
#define CECE_TEST_ADD_ENVIRONMENT(EnvType) \
    [[maybe_unused]] static const bool cece_test_env_registered_##EnvType = ::cece::test::add_environment([] { return new EnvType(); })
