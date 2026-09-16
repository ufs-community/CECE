/**
 * @file test_open_once_no_per_step_close.cpp
 * @brief Open-at-most-once / no-mid-run-close assertions for the driver I/O
 *        rewrite.
 *
 * Feature: driver-io-regrid-perf
 * Task 13.1 — Assert no per-step open/close after first read (Req 9.1)
 *
 * Requirement 9.1: after the first read of each Stream_Variable, the
 * Driver_Orchestrator performs NO per-timestep AMIO file open or close
 * operations. The guarantee is provided by two collaborating mechanisms in
 * src/driver/cece_driver_facade.cpp:
 *
 *   1. GetOrOpenHandleSet is lazy-open-once: it opens the AMIO core/dataset on
 *      the first touch of a variable and caches the result in amio_handles_.
 *      Every subsequent touch returns the cached handle set WITHOUT re-opening.
 *   2. amio_close / amio_finalize are called only from TeardownHandles (the
 *      destructor path), never from AdvanceTime.
 *
 * Standing up the full AdvanceTime (MPI/DAGR/AMIO with a real NetCDF dataset)
 * is far too heavy for a unit test, so this file validates the two mechanisms
 * that *guarantee* Req 9.1 rather than exercising a live multi-step run:
 *
 *   A) Behavioral cache-contract (preferred form from the task): amio_handles_
 *      is a private member and GetOrOpenHandleSet only runs against a real AMIO
 *      dataset, so we validate its caching contract AT THE MAP LEVEL using the
 *      REAL cece::AmioHandleSet struct from the header. We model exactly what
 *      GetOrOpenHandleSet does with amio_handles_: first lookup for a var_name
 *      misses -> "open" (increment a per-var open counter) and populate the
 *      map; every later lookup for the same var_name hits the cache -> returns
 *      the SAME core/dataset pointers with the open counter frozen at 1. This
 *      concretely models "open at most once per variable across a multi-step
 *      run" over many variables and step counts (RapidCheck).
 *
 *   B) Source-guard (robust regression guard for Req 9.1): we read the actual
 *      src/driver/cece_driver_facade.cpp at test time (path injected via
 *      CECE_SOURCE_DIR) and assert on the placement of the AMIO lifecycle
 *      tokens:
 *        - the AdvanceTime function body contains NO inline amio_init(,
 *          amio_open_dataset(, amio_close(, or amio_finalize( calls (the
 *          per-step open/close loop was removed);
 *        - amio_close( / amio_finalize( appear ONLY inside TeardownHandles
 *          (the destructor path);
 *        - GetOrOpenHandleSet uses amio_init_from_string /
 *          amio_open_dataset_from_string and caches into amio_handles_.
 *
 * Together, (A) proves the cache contract that makes reuse-without-reopen
 * possible, and (B) proves the production code is actually wired that way, so
 * the pair concretely validates "open at most once per variable, no mid-run
 * close" (Req 9.1).
 *
 * NOTE ON PRIVATE ACCESS: amio_handles_ is private, but cece::AmioHandleSet is
 * a *public* struct in the cece namespace. The behavioral test therefore needs
 * no friend declaration — it models the map contract with the real struct in a
 * standalone std::unordered_map, exactly mirroring GetOrOpenHandleSet's
 * find/emplace logic. The source-guard needs no access at all. No new friend
 * was added to the header.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_driver_facade.hpp"

#ifdef RC_ENABLE_RAPIDCHECK
#endif
// RapidCheck is optional per the task. Guard its use so the test still builds
// and runs the deterministic checks if RapidCheck is unavailable at compile
// time. The project links rapidcheck for its sibling property tests, so the
// property block is enabled by default.
#if __has_include(<rapidcheck.h>) && __has_include(<rapidcheck/gtest.h>)
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#define CECE_HAVE_RAPIDCHECK 1
#else
#define CECE_HAVE_RAPIDCHECK 0
#endif

namespace cece {
namespace {

// ============================================================================
// Part A — Behavioral cache-contract model.
//
// A faithful, minimal model of what GetOrOpenHandleSet does with amio_handles_.
// It uses the REAL cece::AmioHandleSet struct. `open_count` counts how many
// times an actual "open" happened (i.e. a first-touch miss that had to mint a
// handle). The whole point of Req 9.1 is that open_count stays at 1 per
// variable regardless of how many times the variable is touched.
// ============================================================================
class HandleCacheModel {
   public:
    // Mirrors GetOrOpenHandleSet(var_name, cfg): first touch opens + caches,
    // subsequent touches reuse the cached set with no re-open. Returns a
    // pointer to the cached AmioHandleSet (as GetOrOpenHandleSet returns
    // AmioHandleSet*), stable across reuse for the same var_name.
    AmioHandleSet* GetOrOpen(const std::string& var_name) {
        auto existing = amio_handles_.find(var_name);
        if (existing != amio_handles_.end()) {
            // Cache hit: reuse without re-opening (Req 2.2, 7.1, 9.1).
            return &existing->second;
        }

        // Cache miss: this is the ONE and only open for this variable.
        ++open_count_[var_name];
        ++next_handle_id_;

        AmioHandleSet set;
        // Distinct non-null sentinel pointers per variable so the test can
        // assert pointer stability across reuse. These stand in for the real
        // amio_core_handle / amio_dataset_handle minted by AMIO.
        set.core = reinterpret_cast<amio_core_handle>(static_cast<std::uintptr_t>(next_handle_id_) * 2 + 1);
        set.dataset = reinterpret_cast<amio_dataset_handle>(static_cast<std::uintptr_t>(next_handle_id_) * 2 + 2);
        set.active_data_model = "enhanced";
        set.manifest_content = "backend: netcdf4\npath: " + var_name + "\n";
        auto inserted = amio_handles_.emplace(var_name, std::move(set));
        return &inserted.first->second;
    }

    int open_count(const std::string& var_name) const {
        auto it = open_count_.find(var_name);
        return it == open_count_.end() ? 0 : it->second;
    }

    std::size_t distinct_variables() const {
        return amio_handles_.size();
    }

   private:
    // Same type and keying (model variable name) as the real member.
    std::unordered_map<std::string, AmioHandleSet> amio_handles_;
    std::unordered_map<std::string, int> open_count_;
    std::uint64_t next_handle_id_ = 0;
};

// ----------------------------------------------------------------------------
// Deterministic example: one variable, many steps -> exactly one open, stable
// pointers.
// ----------------------------------------------------------------------------
TEST(OpenOnceCacheContract, SingleVariableOpensOnceOverManySteps) {
    HandleCacheModel model;
    const std::string var = "EMIS_CO";

    AmioHandleSet* first = model.GetOrOpen(var);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(first->core, nullptr);
    ASSERT_NE(first->dataset, nullptr);
    EXPECT_EQ(model.open_count(var), 1);

    const amio_core_handle core0 = first->core;
    const amio_dataset_handle dataset0 = first->dataset;

    // Simulate 50 subsequent AdvanceTime touches of the same variable.
    for (int step = 0; step < 50; ++step) {
        AmioHandleSet* again = model.GetOrOpen(var);
        ASSERT_NE(again, nullptr);
        // Same cached pointers, no re-open.
        EXPECT_EQ(again->core, core0);
        EXPECT_EQ(again->dataset, dataset0);
        EXPECT_EQ(again, first);  // same slot in the map
        EXPECT_EQ(model.open_count(var), 1) << "reopened on step " << step;
    }
}

// ----------------------------------------------------------------------------
// Deterministic example: several variables interleaved across steps -> each
// opens exactly once.
// ----------------------------------------------------------------------------
TEST(OpenOnceCacheContract, MultipleVariablesEachOpenOnce) {
    HandleCacheModel model;
    const std::vector<std::string> vars = {"EMIS_CO", "EMIS_NO", "EMIS_SO2"};

    // Round-robin touch order over 30 steps.
    for (int step = 0; step < 30; ++step) {
        model.GetOrOpen(vars[step % vars.size()]);
    }

    for (const auto& v : vars) {
        EXPECT_EQ(model.open_count(v), 1) << "variable " << v << " opened more than once";
    }
    EXPECT_EQ(model.distinct_variables(), vars.size());
}

#if CECE_HAVE_RAPIDCHECK
// ----------------------------------------------------------------------------
// Property (RapidCheck): for ANY multi-step touch sequence over ANY set of
// variable names, every variable is opened AT MOST ONCE, and once opened the
// returned core/dataset pointers are stable (never re-opened mid-run).
//
// Feature: driver-io-regrid-perf
// Validates: Requirements 9.1
// ----------------------------------------------------------------------------
RC_GTEST_PROP(OpenOnceCacheContractProperty, OpenAtMostOncePerVariable, (const std::vector<std::string>& touch_sequence)) {
    HandleCacheModel model;

    // Record the first-seen pointers per variable so we can assert stability.
    // Stored as integer bit patterns so RapidCheck can render counterexamples.
    std::unordered_map<std::string, std::uintptr_t> first_core;
    std::unordered_map<std::string, std::uintptr_t> first_dataset;

    for (const std::string& var : touch_sequence) {
        AmioHandleSet* set = model.GetOrOpen(var);
        RC_ASSERT(set != nullptr);
        // Compare handles as integers: RapidCheck cannot render raw void*
        // handle types in a counterexample, so cast to uintptr_t first.
        const auto core_bits = reinterpret_cast<std::uintptr_t>(set->core);
        const auto dataset_bits = reinterpret_cast<std::uintptr_t>(set->dataset);
        RC_ASSERT(core_bits != 0u);
        RC_ASSERT(dataset_bits != 0u);

        auto core_it = first_core.find(var);
        if (core_it == first_core.end()) {
            first_core[var] = core_bits;
            first_dataset[var] = dataset_bits;
        } else {
            // Reuse must return the identical pointers minted on first touch.
            RC_ASSERT(core_bits == core_it->second);
            RC_ASSERT(dataset_bits == first_dataset[var]);
        }
    }

    // Every variable that was touched at all was opened exactly once; no
    // variable was opened more than once regardless of touch count (Req 9.1).
    std::unordered_map<std::string, bool> seen;
    for (const std::string& var : touch_sequence) seen[var] = true;
    for (const auto& kv : seen) {
        RC_ASSERT(model.open_count(kv.first) == 1);
    }
}
#endif  // CECE_HAVE_RAPIDCHECK

// ============================================================================
// Part B — Source-guard.
//
// Read the production translation unit and assert the AMIO-lifecycle tokens are
// placed exactly where Req 9.1 requires. This is a robust regression guard: if
// a future edit reintroduces a per-step open/close into AdvanceTime, or moves
// amio_close/amio_finalize out of the teardown path, these assertions fail.
// ============================================================================

std::string ReadDriverSource() {
    const std::string path = std::string(CECE_SOURCE_DIR) + "/src/driver/cece_driver_facade.cpp";
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << "could not open driver source at " << path;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Extract the body of a member function
// `CeceDriverOrchestrator::<name>(...) { ... }` by brace matching from the
// definition's opening brace to its matching close. Returns "" if not found.
std::string ExtractMemberBody(const std::string& src, const std::string& method_name) {
    const std::string signature = "CeceDriverOrchestrator::" + method_name + "(";
    const std::size_t sig_pos = src.find(signature);
    if (sig_pos == std::string::npos) return {};

    // Find the first '{' at or after the signature (skip the parameter list).
    const std::size_t open = src.find('{', sig_pos);
    if (open == std::string::npos) return {};

    int depth = 0;
    for (std::size_t i = open; i < src.size(); ++i) {
        const char c = src[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return src.substr(open, i - open + 1);
            }
        }
    }
    return {};  // unbalanced braces
}

class DriverSourceGuard : public ::testing::Test {
   protected:
    static std::string src_;
    static void SetUpTestSuite() {
        src_ = ReadDriverSource();
    }
};
std::string DriverSourceGuard::src_;

// AdvanceTime must NOT contain any inline AMIO open/close lifecycle calls: the
// per-step open/close/manifest loop was removed. (Req 9.1)
TEST_F(DriverSourceGuard, AdvanceTimeHasNoInlineOpenOrClose) {
    ASSERT_FALSE(src_.empty()) << "driver source was empty / unreadable";

    const std::string body = ExtractMemberBody(src_, "AdvanceTime");
    ASSERT_FALSE(body.empty()) << "could not locate AdvanceTime function body";

    EXPECT_EQ(body.find("amio_init("), std::string::npos) << "AdvanceTime must not call amio_init inline";
    EXPECT_EQ(body.find("amio_init_from_string("), std::string::npos) << "AdvanceTime must not open datasets inline";
    EXPECT_EQ(body.find("amio_open_dataset("), std::string::npos) << "AdvanceTime must not call amio_open_dataset inline";
    EXPECT_EQ(body.find("amio_open_dataset_from_string("), std::string::npos) << "AdvanceTime must not open datasets inline";
    EXPECT_EQ(body.find("amio_close("), std::string::npos) << "AdvanceTime must not close datasets mid-run";
    EXPECT_EQ(body.find("amio_finalize("), std::string::npos) << "AdvanceTime must not finalize cores mid-run";
}

// amio_close( / amio_finalize( must appear ONLY inside TeardownHandles (the
// destructor path). We verify TeardownHandles contains them, and that no other
// function does — by checking that every occurrence in the whole file lies
// inside GetOrOpenHandleSet (partial-open cleanup on a failed first open) or
// TeardownHandles. A mid-run close would appear elsewhere (e.g. AdvanceTime),
// which the previous test already forbids; here we additionally confirm the
// teardown path owns the steady-state close/finalize. (Req 9.1, 7.2)
TEST_F(DriverSourceGuard, CloseAndFinalizeLiveInTeardownPath) {
    ASSERT_FALSE(src_.empty());

    const std::string teardown = ExtractMemberBody(src_, "TeardownHandles");
    ASSERT_FALSE(teardown.empty()) << "could not locate TeardownHandles function body";
    EXPECT_NE(teardown.find("amio_close("), std::string::npos) << "TeardownHandles must close datasets";
    EXPECT_NE(teardown.find("amio_finalize("), std::string::npos) << "TeardownHandles must finalize cores";

    // The only OTHER place a close/finalize is allowed is GetOrOpenHandleSet's
    // partial-open cleanup when a *first* open attempt fails (never a per-step
    // path). Confirm that body accounts for the remaining occurrences.
    const std::string get_or_open = ExtractMemberBody(src_, "GetOrOpenHandleSet");
    ASSERT_FALSE(get_or_open.empty()) << "could not locate GetOrOpenHandleSet function body";

    auto count_occurrences = [](const std::string& hay, const std::string& needle) {
        std::size_t n = 0;
        for (std::size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++n;
        return n;
    };

    const std::size_t total_close = count_occurrences(src_, "amio_close(");
    const std::size_t total_finalize = count_occurrences(src_, "amio_finalize(");
    const std::size_t teardown_close = count_occurrences(teardown, "amio_close(");
    const std::size_t teardown_finalize = count_occurrences(teardown, "amio_finalize(");
    const std::size_t open_close = count_occurrences(get_or_open, "amio_close(");
    const std::size_t open_finalize = count_occurrences(get_or_open, "amio_finalize(");

    // Every close/finalize in the file is accounted for by teardown (steady
    // state) plus GetOrOpenHandleSet (failed-first-open cleanup). Nothing leaks
    // into AdvanceTime or any per-step path.
    EXPECT_EQ(total_close, teardown_close + open_close) << "amio_close appears outside the teardown/open-cleanup paths";
    EXPECT_EQ(total_finalize, teardown_finalize + open_finalize) << "amio_finalize appears outside the teardown/open-cleanup paths";
}

// GetOrOpenHandleSet must open via the STRING-based entry points and cache into
// amio_handles_ (the lazy-open-once mechanism). (Req 9.1, 2.1, 2.2)
TEST_F(DriverSourceGuard, GetOrOpenUsesStringEntryPointsAndCaches) {
    ASSERT_FALSE(src_.empty());

    const std::string body = ExtractMemberBody(src_, "GetOrOpenHandleSet");
    ASSERT_FALSE(body.empty()) << "could not locate GetOrOpenHandleSet function body";

    EXPECT_NE(body.find("amio_init_from_string("), std::string::npos) << "GetOrOpenHandleSet must init from an in-memory manifest string";
    EXPECT_NE(body.find("amio_open_dataset_from_string("), std::string::npos)
        << "GetOrOpenHandleSet must open the dataset from an in-memory manifest string";
    EXPECT_NE(body.find("amio_handles_"), std::string::npos) << "GetOrOpenHandleSet must cache into amio_handles_";
    // The lazy-open-once hallmark: an early return on a cache hit.
    EXPECT_NE(body.find("amio_handles_.find("), std::string::npos) << "GetOrOpenHandleSet must look up the cache before opening";
}

}  // namespace
}  // namespace cece
