// SPDX-License-Identifier: Apache-2.0
// Teardown guard shared by CECE's test main() wrappers.
#ifndef CECE_TEST_TEARDOWN_GUARD_HPP
#define CECE_TEST_TEARDOWN_GUARD_HPP

#include <cstdio>
#include <cstdlib>

namespace cece::test {

/// Hard-exit a CECE test process after RUN_ALL_TESTS has returned.
///
/// Kokkos is linked statically into both the test executable and
/// libcece_core.so; a static object that exists in both DSOs but shares one
/// address through dynamic-symbol interposition (for example
/// Kokkos::Tools::InitArguments::unset_string_option) ends up with its
/// destructor registered by each DSO's teardown table, so process exit frees
/// the same buffer twice. Under Debug malloc checking the second free aborts
/// with "double free detected in tcache 2"; ordinary runs only sometimes
/// survive because heap churn between the frees reuses the freed chunk. That
/// makes `--gtest_list_tests` (used by `gtest_discover_tests` at CMake test
/// time, which allocates almost nothing) abort deterministically, and lets
/// passing test runs abort randomly at exit — either way CMake reports a
/// discovery or "Subprocess aborted" failure (CECE#124 review).
///
/// Everything a gtest run produces outlives this point: results are printed
/// (and any XML/report files written) inside RUN_ALL_TESTS, the
/// Kokkos-aware mains call Kokkos::finalize() before reaching here, and test
/// fixtures have torn down. The interposed statics own no state worth keeping
/// past process exit. So flush stdio buffers and leave through _Exit() to
/// skip the duplicated process-teardown entirely instead of returning from
/// main. _Exit preserves the exit code, so genuine test failures still reach
/// CMake. Caveat: if coverage instrumentation is ever added to test builds,
/// gcov's atexit flush handler would have to run before _Exit().
inline void exit_after_tests(int rc) {
    std::fflush(nullptr);
    std::_Exit(rc);
}

}  // namespace cece::test

#endif  // CECE_TEST_TEARDOWN_GUARD_HPP
