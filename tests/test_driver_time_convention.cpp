// SPDX-License-Identifier: Apache-2.0
// Copyright (c) HELM Project Contributors

/**
 * @file test_driver_time_convention.cpp
 * @brief Unit test for CeceSimulation::step's time convention.
 *
 * Pins the two contract invariants that both drivers must share so their
 * output can be byte-for-byte identical:
 *
 *   * Ingest happens at the STEP-START instant: Step passes step_start_iso
 *     (verbatim) to the driver facade's advance_time, not step_end.
 *   * The output record is stamped at the STEP-END elapsed time: Step passes
 *     (step_end - configured start) seconds to write_step.
 *   * The completion signal is the core clock's rc == 1, surfaced as
 *     StepOutcome.complete; rc < 0 is an error.
 *
 * Method: the three C entry points Step calls are provided as spy
 * definitions in this test binary. Because the real ones live in shared
 * libraries (libcece_core.so / libcece_driver.so) and are default-visibility
 * (preemptible, and the build uses no -Bsymbolic), the executable's strong
 * definitions interpose them at dynamic-link time — the same link-time
 * override idiom as halo/tests/mpi_interposition.cpp. No production code
 * changes: the friend CeceSimulationTestAccess only grants compile-time
 * construction of an instance with a known clock anchor and sentinel
 * handles, mirroring the EndpointCacheTestAccess precedent.
 */

#include <gtest/gtest.h>

#include <string>
#include <tick/tick.hpp>
#include <vector>

#include "cece/cece_simulation.hpp"

// ---------------------------------------------------------------------------
// Spy recorders for the three interposed C entry points. extern "C" so the
// symbols match the real ones exactly (and thus interpose them).
// ---------------------------------------------------------------------------
namespace {

struct AdvanceCall {
    std::string iso;
    void* driver = nullptr;
    void* core = nullptr;
};

struct RunCall {
    int hour = -999;
    int day_of_week = -999;
};

struct WriteCall {
    double elapsed_seconds = -1.0;
    int step_index = -999;
};

// Process-wide spy state (tests are single-threaded).
std::vector<AdvanceCall> g_advance_calls;
std::vector<RunCall> g_run_calls;
std::vector<WriteCall> g_write_calls;

// Shared monotonic sequence across every spy, so a test can assert the ORDER
// of the ingest -> compute -> write calls, not merely that each happened.
std::vector<std::string> g_call_order;

// Controllable return codes for the interposed functions.
int g_core_run_rc = 0;
int g_advance_rc = 0;
int g_write_rc = 0;

// Teardown recorders: the destructor path of a test-built simulation reaches
// cece_driver_destroy / cece_core_finalize with sentinel handles, so those are
// interposed too rather than dereferencing the sentinels.
int g_destroy_calls = 0;
int g_finalize_calls = 0;

void ResetSpies() {
    g_advance_calls.clear();
    g_run_calls.clear();
    g_write_calls.clear();
    g_call_order.clear();
    g_core_run_rc = 0;
    g_advance_rc = 0;
    g_write_rc = 0;
    g_destroy_calls = 0;
    g_finalize_calls = 0;
}

}  // namespace

extern "C" {

void cece_driver_advance_time(void* driver_ptr, const char* time_iso8601, int time_len, void* cece_core_data_ptr, int* rc) {
    g_call_order.push_back("advance");
    g_advance_calls.push_back({std::string(time_iso8601, static_cast<size_t>(time_len)), driver_ptr, cece_core_data_ptr});
    if (rc != nullptr) {
        *rc = g_advance_rc;
    }
}

void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc) {
    (void)data_ptr;
    g_call_order.push_back("run");
    g_run_calls.push_back({hour, day_of_week});
    if (rc != nullptr) {
        *rc = g_core_run_rc;
    }
}

void cece_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc) {
    (void)data_ptr;
    g_call_order.push_back("write");
    g_write_calls.push_back({time_seconds, step_index});
    if (rc != nullptr) {
        *rc = g_write_rc;
    }
}

void cece_driver_destroy(void* driver_ptr, int* rc) {
    (void)driver_ptr;
    g_call_order.push_back("destroy");
    ++g_destroy_calls;
    if (rc != nullptr) {
        *rc = 0;
    }
}

void cece_core_finalize(void* data_ptr, int* rc) {
    (void)data_ptr;
    g_call_order.push_back("finalize");
    ++g_finalize_calls;
    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"

// ---------------------------------------------------------------------------
// Test-only construction of a CeceSimulation with a known clock anchor.
// Declared a friend inside CeceSimulation (include/cece/cece_simulation.hpp).
// ---------------------------------------------------------------------------
namespace cece {

struct CeceSimulationTestAccess {
    static std::unique_ptr<CeceSimulation> Make(const std::string& start_iso) {
        auto sim = std::unique_ptr<CeceSimulation>(new CeceSimulation());
        tick::Gregorian_Calendar cal;
        sim->start_time_ = cal.to_time_point(tick::parse_iso8601(start_iso));
        sim->driver_ptr_ = reinterpret_cast<void*>(0x1);  // sentinel, never dereferenced
        sim->core_data_ptr_ = reinterpret_cast<void*>(0x2);
        sim->finalized_ = false;
        return sim;
    }
};

}  // namespace cece

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class TimeConventionTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ResetSpies();
    }
    void TearDown() override {
        ResetSpies();
    }
};

// Invariant 2: ingest uses step_start_iso verbatim.
TEST_F(TimeConventionTest, IngestsAtStepStart) {
    auto sim = cece::CeceSimulationTestAccess::Make("2023-01-01T00:00:00");

    const cece::StepOutcome result = sim->step("2023-02-01T00:00:00", "2023-03-02T00:00:00", /*step_index=*/1);

    ASSERT_EQ(g_advance_calls.size(), 1u);
    EXPECT_EQ(g_advance_calls[0].iso, "2023-02-01T00:00:00");  // step START, not end
    EXPECT_EQ(g_run_calls.size(), 1u);
    EXPECT_EQ(g_write_calls.size(), 1u);
    EXPECT_FALSE(result.error);
    EXPECT_FALSE(result.complete);
}

// Invariant 3: the output stamp is (step_end - configured start) seconds.
TEST_F(TimeConventionTest, StampsAtStepEndElapsed) {
    // start = 2023-01-01, step_end = 2023-02-01 -> 31 days = 2678400 s.
    auto sim = cece::CeceSimulationTestAccess::Make("2023-01-01T00:00:00");

    sim->step("2023-01-01T00:00:00", "2023-02-01T00:00:00", /*step_index=*/1);

    ASSERT_EQ(g_write_calls.size(), 1u);
    EXPECT_DOUBLE_EQ(g_write_calls[0].elapsed_seconds, 2678400.0);
    EXPECT_EQ(g_write_calls[0].step_index, 1);
}

// The step_index is forwarded to the writer unchanged.
TEST_F(TimeConventionTest, ForwardsStepIndex) {
    auto sim = cece::CeceSimulationTestAccess::Make("2023-01-01T00:00:00");

    sim->step("2023-01-01T00:00:00", "2023-01-02T00:00:00", /*step_index=*/7);

    ASSERT_EQ(g_write_calls.size(), 1u);
    EXPECT_EQ(g_write_calls[0].step_index, 7);
}

// Ordering: ingest -> core run -> write (the fixed sequence both drivers share).
TEST_F(TimeConventionTest, CallsInOrderIngestRunWrite) {
    auto sim = cece::CeceSimulationTestAccess::Make("2023-01-01T00:00:00");

    sim->step("2023-01-01T00:00:00", "2023-01-02T00:00:00", /*step_index=*/1);

    // The shared recorder captures the exact sequence, so a reordered call
    // (e.g. writing before running) fails this assertion rather than passing
    // on per-stage counts alone.
    EXPECT_EQ(g_call_order, (std::vector<std::string>{"advance", "run", "write"}));
}

// Invariant 4: the core clock's rc == 1 is surfaced as complete.
TEST_F(TimeConventionTest, CompletionSignalFromCoreRc) {
    auto sim = cece::CeceSimulationTestAccess::Make("2023-01-01T00:00:00");
    g_core_run_rc = 1;  // core clock reached the end time

    const cece::StepOutcome result = sim->step("2023-01-01T00:00:00", "2023-01-02T00:00:00", /*step_index=*/1);

    EXPECT_TRUE(result.complete);
    EXPECT_FALSE(result.error);
    // The writer still runs for the final step before completion is reported.
    EXPECT_EQ(g_write_calls.size(), 1u);
}

// rc < 0 from the core is an error, and short-circuits before the write.
TEST_F(TimeConventionTest, CoreErrorShortCircuits) {
    auto sim = cece::CeceSimulationTestAccess::Make("2023-01-01T00:00:00");
    g_core_run_rc = -5;

    const cece::StepOutcome result = sim->step("2023-01-01T00:00:00", "2023-01-02T00:00:00", /*step_index=*/1);

    EXPECT_TRUE(result.error);
    EXPECT_EQ(result.rc, -5);
    EXPECT_FALSE(result.complete);
    EXPECT_EQ(g_write_calls.size(), 0u);  // never reached the writer
}

// An ingest failure short-circuits before the core run.
TEST_F(TimeConventionTest, IngestErrorShortCircuits) {
    auto sim = cece::CeceSimulationTestAccess::Make("2023-01-01T00:00:00");
    g_advance_rc = -3;

    const cece::StepOutcome result = sim->step("2023-01-01T00:00:00", "2023-01-02T00:00:00", /*step_index=*/1);

    EXPECT_TRUE(result.error);
    EXPECT_EQ(result.rc, -3);
    EXPECT_EQ(g_run_calls.size(), 0u);
    EXPECT_EQ(g_write_calls.size(), 0u);
}
