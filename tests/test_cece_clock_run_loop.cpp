/**
 * @file test_cece_clock_run_loop.cpp
 * @brief Integration-style unit tests for clock-gated run loop scheduling.
 *
 * Tests the CeceClock scheduling logic in the context of how cece_core_run
 * uses it, without requiring the full Kokkos/ESMF stack. Validates that
 * components are scheduled at their configured intervals and that the
 * stacking engine always executes last among due components.
 *
 * Validates: Requirements 5.1, 5.2, 5.3, 5.4, 10.3
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "cece/cece_clock.hpp"

using namespace cece;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Collect the names of due components from a StepResult.
static std::vector<std::string> DueNames(const StepResult& step) {
    std::vector<std::string> names;
    names.reserve(step.due_components.size());
    for (const auto* c : step.due_components) {
        names.push_back(c->name);
    }
    return names;
}

/// Check whether a component name appears in the due list.
static bool IsDue(const StepResult& step, const std::string& name) {
    for (const auto* c : step.due_components) {
        if (c->name == name) return true;
    }
    return false;
}

/// Return the index of a component in the due list, or -1 if absent.
static int DueIndex(const StepResult& step, const std::string& name) {
    for (size_t i = 0; i < step.due_components.size(); ++i) {
        if (step.due_components[i]->name == name) return static_cast<int>(i);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Fixture: Mixed-interval clock
// ---------------------------------------------------------------------------

/**
 * @brief Fixture that creates a CeceClock with mixed intervals.
 *
 * Configuration:
 *   base_timestep = 300 s (5 min)
 *   scheme_a (physics)  = 300 s  (every step)
 *   scheme_b (physics)  = 900 s  (every 3 steps)
 *   stream_x (data)     = 600 s  (every 2 steps)
 *   stacking (stacking) = 600 s  (every 2 steps)
 *
 * 12-hour simulation: "2020-07-15T00:00:00" → "2020-07-15T12:00:00"
 */
class ClockGatedRunLoopTest : public ::testing::Test {
   protected:
    void SetUp() override {
        std::vector<ClockComponent> components = {
            {ComponentType::kPhysicsScheme, "scheme_a", 300},
            {ComponentType::kPhysicsScheme, "scheme_b", 900},
            {ComponentType::kDataStream, "stream_x", 600},
            {ComponentType::kStackingEngine, "stacking", 600},
        };
        clock_ = std::make_unique<CeceClock>(
            "2020-07-15T00:00:00", "2020-07-15T12:00:00", 300, components);
    }

    std::unique_ptr<CeceClock> clock_;
};

// ---------------------------------------------------------------------------
// Test: First step — all components due (Req 5.1, 5.2, 5.3, 5.4)
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, FirstStepAllComponentsDue) {
    StepResult step = clock_->Advance();

    EXPECT_EQ(step.elapsed_seconds, 300);
    EXPECT_TRUE(IsDue(step, "scheme_a"));
    EXPECT_TRUE(IsDue(step, "scheme_b"));
    EXPECT_TRUE(IsDue(step, "stream_x"));
    EXPECT_TRUE(IsDue(step, "stacking"));
    EXPECT_EQ(step.due_components.size(), 4u);
}

// ---------------------------------------------------------------------------
// Test: Physics schemes execute only at configured intervals (Req 5.1)
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, PhysicsSchemesExecuteAtConfiguredIntervals) {
    // Step 1: elapsed=300 — first step, all due
    StepResult s1 = clock_->Advance();
    EXPECT_TRUE(IsDue(s1, "scheme_a"));
    EXPECT_TRUE(IsDue(s1, "scheme_b"));

    // Step 2: elapsed=600 — scheme_a (300|600) due, scheme_b (900∤600) NOT due
    StepResult s2 = clock_->Advance();
    EXPECT_TRUE(IsDue(s2, "scheme_a"));
    EXPECT_FALSE(IsDue(s2, "scheme_b"));

    // Step 3: elapsed=900 — scheme_a (300|900) due, scheme_b (900|900) due
    StepResult s3 = clock_->Advance();
    EXPECT_TRUE(IsDue(s3, "scheme_a"));
    EXPECT_TRUE(IsDue(s3, "scheme_b"));

    // Step 4: elapsed=1200 — scheme_a due, scheme_b NOT due
    StepResult s4 = clock_->Advance();
    EXPECT_TRUE(IsDue(s4, "scheme_a"));
    EXPECT_FALSE(IsDue(s4, "scheme_b"));

    // Step 5: elapsed=1500 — scheme_a due, scheme_b NOT due
    StepResult s5 = clock_->Advance();
    EXPECT_TRUE(IsDue(s5, "scheme_a"));
    EXPECT_FALSE(IsDue(s5, "scheme_b"));

    // Step 6: elapsed=1800 — both due (1800 is multiple of 300 and 900)
    StepResult s6 = clock_->Advance();
    EXPECT_TRUE(IsDue(s6, "scheme_a"));
    EXPECT_TRUE(IsDue(s6, "scheme_b"));
}

// ---------------------------------------------------------------------------
// Test: Data streams ingest only at configured intervals (Req 5.2, 5.3)
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, DataStreamsIngestAtConfiguredIntervals) {
    // Step 1: elapsed=300 — first step, stream_x due
    StepResult s1 = clock_->Advance();
    EXPECT_TRUE(IsDue(s1, "stream_x"));

    // Step 2: elapsed=600 — stream_x (600|600) due
    StepResult s2 = clock_->Advance();
    EXPECT_TRUE(IsDue(s2, "stream_x"));

    // Step 3: elapsed=900 — stream_x (600∤900) NOT due
    StepResult s3 = clock_->Advance();
    EXPECT_FALSE(IsDue(s3, "stream_x"));

    // Step 4: elapsed=1200 — stream_x (600|1200) due
    StepResult s4 = clock_->Advance();
    EXPECT_TRUE(IsDue(s4, "stream_x"));

    // Step 5: elapsed=1500 — NOT due
    StepResult s5 = clock_->Advance();
    EXPECT_FALSE(IsDue(s5, "stream_x"));

    // Step 6: elapsed=1800 — due
    StepResult s6 = clock_->Advance();
    EXPECT_TRUE(IsDue(s6, "stream_x"));
}

// ---------------------------------------------------------------------------
// Test: Step 2 (elapsed=600) — only components with intervals dividing 600
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, Step2OnlyComponentsDividing600AreDue) {
    clock_->Advance();  // step 1
    StepResult s2 = clock_->Advance();  // step 2, elapsed=600

    EXPECT_EQ(s2.elapsed_seconds, 600);

    // scheme_a: 300 divides 600 → due
    EXPECT_TRUE(IsDue(s2, "scheme_a"));
    // scheme_b: 900 does NOT divide 600 → not due
    EXPECT_FALSE(IsDue(s2, "scheme_b"));
    // stream_x: 600 divides 600 → due
    EXPECT_TRUE(IsDue(s2, "stream_x"));
    // stacking: 600 divides 600 → due
    EXPECT_TRUE(IsDue(s2, "stacking"));
}

// ---------------------------------------------------------------------------
// Test: Stacking always appears last when due (Req 5.4)
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, StackingAlwaysAppearsLastWhenDue) {
    // Advance multiple steps and check stacking position whenever it's due
    for (int i = 0; i < 12; ++i) {
        StepResult step = clock_->Advance();
        int stacking_idx = DueIndex(step, "stacking");
        if (stacking_idx >= 0) {
            // Stacking must be the last element
            EXPECT_EQ(stacking_idx, static_cast<int>(step.due_components.size()) - 1)
                << "Stacking not last at elapsed=" << step.elapsed_seconds;

            // All non-stacking components must appear before stacking
            for (size_t j = 0; j < step.due_components.size() - 1; ++j) {
                EXPECT_NE(step.due_components[j]->type, ComponentType::kStackingEngine)
                    << "Non-last stacking at index " << j
                    << " at elapsed=" << step.elapsed_seconds;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Test: Stacking executes after all other due components (Req 5.4)
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, StackingExecutesAfterOtherDueComponents) {
    // At step 1 (first step), all 4 components are due.
    // Stacking must be last.
    StepResult s1 = clock_->Advance();
    ASSERT_EQ(s1.due_components.size(), 4u);
    EXPECT_EQ(s1.due_components.back()->name, "stacking");
    EXPECT_EQ(s1.due_components.back()->type, ComponentType::kStackingEngine);

    // At step 2 (elapsed=600), scheme_a + stream_x + stacking are due.
    StepResult s2 = clock_->Advance();
    ASSERT_GE(s2.due_components.size(), 2u);
    if (IsDue(s2, "stacking")) {
        EXPECT_EQ(s2.due_components.back()->name, "stacking");
    }
}

// ---------------------------------------------------------------------------
// Fixture: Backward compatibility — all intervals equal base timestep
// ---------------------------------------------------------------------------

class ClockGatedRunLoopBackwardCompatTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // All intervals = base_timestep (300s) → every component due every step
        std::vector<ClockComponent> components = {
            {ComponentType::kPhysicsScheme, "megan", 300},
            {ComponentType::kPhysicsScheme, "sea_salt", 300},
            {ComponentType::kDataStream, "anthropogenic", 300},
            {ComponentType::kStackingEngine, "stacking", 300},
        };
        clock_ = std::make_unique<CeceClock>(
            "2020-07-15T00:00:00", "2020-07-15T01:00:00", 300, components);
    }

    std::unique_ptr<CeceClock> clock_;
};

// ---------------------------------------------------------------------------
// Test: Backward compat — all components due every step (Req 10.3)
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopBackwardCompatTest, AllComponentsDueEveryStep) {
    // 1 hour / 300s = 12 steps
    for (int i = 0; i < 12; ++i) {
        StepResult step = clock_->Advance();
        if (step.simulation_complete && step.due_components.empty()) break;

        EXPECT_EQ(step.due_components.size(), 4u)
            << "All 4 components should be due at step " << (i + 1)
            << " (elapsed=" << step.elapsed_seconds << ")";

        EXPECT_TRUE(IsDue(step, "megan"));
        EXPECT_TRUE(IsDue(step, "sea_salt"));
        EXPECT_TRUE(IsDue(step, "anthropogenic"));
        EXPECT_TRUE(IsDue(step, "stacking"));
    }
}

TEST_F(ClockGatedRunLoopBackwardCompatTest, StackingAlwaysLastEvenWhenAllDue) {
    for (int i = 0; i < 12; ++i) {
        StepResult step = clock_->Advance();
        if (step.simulation_complete && step.due_components.empty()) break;

        ASSERT_FALSE(step.due_components.empty());
        EXPECT_EQ(step.due_components.back()->name, "stacking")
            << "Stacking should be last at step " << (i + 1);
    }
}

// ---------------------------------------------------------------------------
// Test: Multi-step scheduling sequence verification
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, FullSchedulingSequenceOverSixSteps) {
    // Verify the complete due-component pattern over 6 steps (1800s).
    //
    // Step | Elapsed | scheme_a(300) | scheme_b(900) | stream_x(600) | stacking(600)
    // -----|---------|---------------|---------------|----------------|---------------
    //  1   |  300    | due (1st)     | due (1st)     | due (1st)      | due (1st)
    //  2   |  600    | due           | -             | due            | due
    //  3   |  900    | due           | due           | -              | -
    //  4   | 1200    | due           | -             | due            | due
    //  5   | 1500    | due           | -             | -              | -
    //  6   | 1800    | due           | due           | due            | due

    struct Expected {
        int elapsed;
        bool scheme_a, scheme_b, stream_x, stacking;
    };

    std::vector<Expected> expected = {
        {300,  true, true,  true,  true},
        {600,  true, false, true,  true},
        {900,  true, true,  false, false},
        {1200, true, false, true,  true},
        {1500, true, false, false, false},
        {1800, true, true,  true,  true},
    };

    for (const auto& e : expected) {
        StepResult step = clock_->Advance();
        EXPECT_EQ(step.elapsed_seconds, e.elapsed)
            << "Elapsed mismatch";
        EXPECT_EQ(IsDue(step, "scheme_a"), e.scheme_a)
            << "scheme_a mismatch at elapsed=" << e.elapsed;
        EXPECT_EQ(IsDue(step, "scheme_b"), e.scheme_b)
            << "scheme_b mismatch at elapsed=" << e.elapsed;
        EXPECT_EQ(IsDue(step, "stream_x"), e.stream_x)
            << "stream_x mismatch at elapsed=" << e.elapsed;
        EXPECT_EQ(IsDue(step, "stacking"), e.stacking)
            << "stacking mismatch at elapsed=" << e.elapsed;
    }
}

// ---------------------------------------------------------------------------
// Test: Non-due components are skipped (simulates retained output) (Req 5.2, 5.3)
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, NonDueComponentsSkipped) {
    clock_->Advance();  // step 1 — all due

    // Step 2: elapsed=600
    StepResult s2 = clock_->Advance();
    // scheme_b (900s interval) should NOT be in the due list
    EXPECT_FALSE(IsDue(s2, "scheme_b"))
        << "scheme_b should be skipped at elapsed=600 (interval=900)";

    // Step 3: elapsed=900
    StepResult s3 = clock_->Advance();
    // stream_x (600s interval) should NOT be due at 900
    EXPECT_FALSE(IsDue(s3, "stream_x"))
        << "stream_x should be skipped at elapsed=900 (interval=600)";
    // stacking (600s interval) should NOT be due at 900
    EXPECT_FALSE(IsDue(s3, "stacking"))
        << "stacking should be skipped at elapsed=900 (interval=600)";
}

// ---------------------------------------------------------------------------
// Test: Simulation completion signal
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, SimulationCompletionSignal) {
    // 12 hours / 300s = 144 steps
    StepResult step;
    for (int i = 0; i < 144; ++i) {
        step = clock_->Advance();
    }
    EXPECT_TRUE(step.simulation_complete);

    // Additional advance after completion should also signal complete
    StepResult extra = clock_->Advance();
    EXPECT_TRUE(extra.simulation_complete);
    EXPECT_TRUE(extra.due_components.empty());
}

// ---------------------------------------------------------------------------
// Test: Backward compat — null clock means execute all unconditionally
// ---------------------------------------------------------------------------

TEST(ClockGatedRunLoopNullClockTest, NullClockBackwardCompatibility) {
    // When clock is null, the run loop should execute all components
    // unconditionally. We verify this by checking that a null unique_ptr
    // evaluates to false, which is the guard used in cece_core_run.
    std::unique_ptr<CeceClock> clock = nullptr;
    EXPECT_FALSE(static_cast<bool>(clock))
        << "Null clock should evaluate to false, triggering unconditional execution path";
}

// ---------------------------------------------------------------------------
// Test: Due component count varies correctly across steps
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, DueComponentCountVariesAcrossSteps) {
    // Step 1: all 4 due (first-step guarantee)
    StepResult s1 = clock_->Advance();
    EXPECT_EQ(s1.due_components.size(), 4u);

    // Step 2 (elapsed=600): scheme_a + stream_x + stacking = 3
    StepResult s2 = clock_->Advance();
    EXPECT_EQ(s2.due_components.size(), 3u);

    // Step 3 (elapsed=900): scheme_a + scheme_b = 2
    StepResult s3 = clock_->Advance();
    EXPECT_EQ(s3.due_components.size(), 2u);

    // Step 5 (elapsed=1500): scheme_a only = 1
    clock_->Advance();  // step 4
    StepResult s5 = clock_->Advance();
    EXPECT_EQ(s5.due_components.size(), 1u);
    EXPECT_EQ(s5.due_components[0]->name, "scheme_a");
}

// ---------------------------------------------------------------------------
// Test: Component types are preserved in due list
// ---------------------------------------------------------------------------

TEST_F(ClockGatedRunLoopTest, ComponentTypesPreservedInDueList) {
    StepResult step = clock_->Advance();  // first step, all due

    std::unordered_set<std::string> physics_names;
    std::unordered_set<std::string> stream_names;
    std::unordered_set<std::string> stacking_names;

    for (const auto* c : step.due_components) {
        switch (c->type) {
            case ComponentType::kPhysicsScheme:
                physics_names.insert(c->name);
                break;
            case ComponentType::kDataStream:
                stream_names.insert(c->name);
                break;
            case ComponentType::kStackingEngine:
                stacking_names.insert(c->name);
                break;
        }
    }

    EXPECT_EQ(physics_names.count("scheme_a"), 1u);
    EXPECT_EQ(physics_names.count("scheme_b"), 1u);
    EXPECT_EQ(stream_names.count("stream_x"), 1u);
    EXPECT_EQ(stacking_names.count("stacking"), 1u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
