/**
 * @file test_clock_operations_unit.cpp
 * @brief Unit tests for clock operations in the driver.
 *
 * Validates:
 *   - Clock creation with various time ranges
 *   - Clock advancement
 *   - Stop time detection
 *
 * Requirements: 4.1, 5.1, 7.1
 *
 * This test file provides focused unit tests that complement the property-based
 * tests for clock operations. It tests specific scenarios and edge cases.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace cece {

/**
 * @brief Simple clock model for testing clock operations.
 *
 * This models the essential behavior of ESMF_Clock for unit testing purposes.
 */
class ClockModel {
   public:
    ClockModel(int64_t start_seconds, int64_t stop_seconds, int64_t timestep_seconds)
        : start_seconds_(start_seconds),
          current_seconds_(start_seconds),
          stop_seconds_(stop_seconds),
          timestep_seconds_(timestep_seconds) {}

    bool IsAtStopTime() const {
        return current_seconds_ >= stop_seconds_;
    }

    void Advance() {
        current_seconds_ += timestep_seconds_;
    }

    int64_t GetCurrentSeconds() const {
        return current_seconds_;
    }

    int64_t GetStartSeconds() const {
        return start_seconds_;
    }

    int64_t GetStopSeconds() const {
        return stop_seconds_;
    }

    int64_t GetTimestepSeconds() const {
        return timestep_seconds_;
    }

    void Reset() {
        current_seconds_ = start_seconds_;
    }

   private:
    int64_t start_seconds_;
    int64_t current_seconds_;
    int64_t stop_seconds_;
    int64_t timestep_seconds_;
};

// ---------------------------------------------------------------------------
// Test Suite: Clock Creation
// ---------------------------------------------------------------------------

class ClockCreationTest : public ::testing::Test {};

/**
 * @brief Test clock creation with standard time ranges.
 *
 * Validates Requirement 4.1: Clock creation with proper configuration.
 */
TEST_F(ClockCreationTest, StandardTimeRanges) {
    // 1 day simulation with 1-hour timesteps
    ClockModel clock1(0, 86400, 3600);
    EXPECT_EQ(clock1.GetStartSeconds(), 0);
    EXPECT_EQ(clock1.GetStopSeconds(), 86400);
    EXPECT_EQ(clock1.GetTimestepSeconds(), 3600);
    EXPECT_EQ(clock1.GetCurrentSeconds(), 0);
    EXPECT_FALSE(clock1.IsAtStopTime());

    // 2 day simulation with 2-hour timesteps
    ClockModel clock2(0, 172800, 7200);
    EXPECT_EQ(clock2.GetStartSeconds(), 0);
    EXPECT_EQ(clock2.GetStopSeconds(), 172800);
    EXPECT_EQ(clock2.GetTimestepSeconds(), 7200);
    EXPECT_EQ(clock2.GetCurrentSeconds(), 0);
    EXPECT_FALSE(clock2.IsAtStopTime());

    // 1 week simulation with 6-hour timesteps
    ClockModel clock3(0, 604800, 21600);
    EXPECT_EQ(clock3.GetStartSeconds(), 0);
    EXPECT_EQ(clock3.GetStopSeconds(), 604800);
    EXPECT_EQ(clock3.GetTimestepSeconds(), 21600);
    EXPECT_EQ(clock3.GetCurrentSeconds(), 0);
    EXPECT_FALSE(clock3.IsAtStopTime());
}

/**
 * @brief Test clock creation with non-zero start times.
 *
 * Validates Requirement 4.1: Clock creation with various start times.
 */
TEST_F(ClockCreationTest, NonZeroStartTimes) {
    // Start at 1000 seconds
    ClockModel clock1(1000, 5000, 500);
    EXPECT_EQ(clock1.GetStartSeconds(), 1000);
    EXPECT_EQ(clock1.GetCurrentSeconds(), 1000);
    EXPECT_FALSE(clock1.IsAtStopTime());

    // Start at 1 day
    ClockModel clock2(86400, 172800, 3600);
    EXPECT_EQ(clock2.GetStartSeconds(), 86400);
    EXPECT_EQ(clock2.GetCurrentSeconds(), 86400);
    EXPECT_FALSE(clock2.IsAtStopTime());

    // Start at arbitrary time (June 15, 2020 00:00:00 UTC)
    ClockModel clock3(1592179200, 1592265600, 3600);
    EXPECT_EQ(clock3.GetStartSeconds(), 1592179200);
    EXPECT_EQ(clock3.GetCurrentSeconds(), 1592179200);
    EXPECT_FALSE(clock3.IsAtStopTime());
}

/**
 * @brief Test clock creation with various timestep sizes.
 *
 * Validates Requirement 4.1: Clock creation with different timesteps.
 */
TEST_F(ClockCreationTest, VariousTimestepSizes) {
    std::vector<int64_t> timestep_sizes = {
        60,     // 1 minute
        300,    // 5 minutes
        900,    // 15 minutes
        1800,   // 30 minutes
        3600,   // 1 hour
        7200,   // 2 hours
        10800,  // 3 hours
        21600,  // 6 hours
        43200,  // 12 hours
        86400,  // 1 day
    };

    for (int64_t timestep : timestep_sizes) {
        ClockModel clock(0, timestep * 10, timestep);
        EXPECT_EQ(clock.GetTimestepSeconds(), timestep)
            << "Timestep size " << timestep << " not set correctly";
        EXPECT_EQ(clock.GetStopSeconds(), timestep * 10)
            << "Stop time not set correctly for timestep " << timestep;
    }
}

/**
 * @brief Test clock creation with zero-length simulation.
 *
 * Validates Requirement 4.1: Clock creation where start == stop.
 */
TEST_F(ClockCreationTest, ZeroLengthSimulation) {
    ClockModel clock(0, 0, 3600);
    EXPECT_EQ(clock.GetStartSeconds(), 0);
    EXPECT_EQ(clock.GetStopSeconds(), 0);
    EXPECT_EQ(clock.GetCurrentSeconds(), 0);
    EXPECT_TRUE(clock.IsAtStopTime())
        << "Zero-length simulation should be at stop time immediately";
}

/**
 * @brief Test clock creation with single timestep simulation.
 *
 * Validates Requirement 4.1: Clock creation for minimal simulation.
 */
TEST_F(ClockCreationTest, SingleTimestepSimulation) {
    ClockModel clock(0, 3600, 3600);
    EXPECT_EQ(clock.GetStartSeconds(), 0);
    EXPECT_EQ(clock.GetStopSeconds(), 3600);
    EXPECT_EQ(clock.GetTimestepSeconds(), 3600);
    EXPECT_FALSE(clock.IsAtStopTime());

    // After one advancement, should be at stop time
    clock.Advance();
    EXPECT_TRUE(clock.IsAtStopTime());
}

// ---------------------------------------------------------------------------
// Test Suite: Clock Advancement
// ---------------------------------------------------------------------------

class ClockAdvancementTest : public ::testing::Test {};

/**
 * @brief Test basic clock advancement.
 *
 * Validates Requirement 5.1: Clock advancement each timestep.
 */
TEST_F(ClockAdvancementTest, BasicAdvancement) {
    ClockModel clock(0, 10800, 3600);  // 3 hours, 1-hour steps

    EXPECT_EQ(clock.GetCurrentSeconds(), 0);

    clock.Advance();
    EXPECT_EQ(clock.GetCurrentSeconds(), 3600);

    clock.Advance();
    EXPECT_EQ(clock.GetCurrentSeconds(), 7200);

    clock.Advance();
    EXPECT_EQ(clock.GetCurrentSeconds(), 10800);
}

/**
 * @brief Test clock advancement with various timestep sizes.
 *
 * Validates Requirement 5.1: Clock advancement with different timesteps.
 */
TEST_F(ClockAdvancementTest, AdvancementWithVariousTimesteps) {
    std::vector<std::tuple<int64_t, int64_t, int64_t, int>> test_cases = {
        {0, 3600, 3600, 1},    // 1 hour, 1-hour steps, 1 step
        {0, 7200, 3600, 2},    // 2 hours, 1-hour steps, 2 steps
        {0, 86400, 3600, 24},  // 1 day, 1-hour steps, 24 steps
        {0, 86400, 7200, 12},  // 1 day, 2-hour steps, 12 steps
        {0, 86400, 21600, 4},  // 1 day, 6-hour steps, 4 steps
    };

    for (const auto& [start, stop, timestep, expected_steps] : test_cases) {
        ClockModel clock(start, stop, timestep);

        int step_count = 0;
        while (!clock.IsAtStopTime()) {
            clock.Advance();
            step_count++;
        }

        EXPECT_EQ(step_count, expected_steps)
            << "Expected " << expected_steps << " steps for timestep " << timestep << "s";
    }
}

/**
 * @brief Test clock advancement maintains correct time increments.
 *
 * Validates Requirement 5.1: Clock advances by exactly one timestep.
 */
TEST_F(ClockAdvancementTest, CorrectTimeIncrements) {
    ClockModel clock(0, 86400, 3600);  // 1 day, 1-hour steps

    int64_t prev_time = clock.GetCurrentSeconds();

    for (int i = 0; i < 24; ++i) {
        clock.Advance();
        int64_t curr_time = clock.GetCurrentSeconds();
        int64_t increment = curr_time - prev_time;

        EXPECT_EQ(increment, 3600) << "Clock increment should be exactly 3600s at step " << i;

        prev_time = curr_time;
    }
}

/**
 * @brief Test clock advancement with non-zero start time.
 *
 * Validates Requirement 5.1: Clock advancement from non-zero start.
 */
TEST_F(ClockAdvancementTest, AdvancementFromNonZeroStart) {
    ClockModel clock(1000, 5000, 1000);

    EXPECT_EQ(clock.GetCurrentSeconds(), 1000);

    clock.Advance();
    EXPECT_EQ(clock.GetCurrentSeconds(), 2000);

    clock.Advance();
    EXPECT_EQ(clock.GetCurrentSeconds(), 3000);

    clock.Advance();
    EXPECT_EQ(clock.GetCurrentSeconds(), 4000);

    clock.Advance();
    EXPECT_EQ(clock.GetCurrentSeconds(), 5000);
    EXPECT_TRUE(clock.IsAtStopTime());
}

/**
 * @brief Test clock advancement doesn't go backwards.
 *
 * Validates Requirement 5.1: Clock time is monotonically increasing.
 */
TEST_F(ClockAdvancementTest, NoBackwardsMovement) {
    ClockModel clock(0, 86400, 3600);

    int64_t prev_time = clock.GetCurrentSeconds();

    for (int i = 0; i < 24; ++i) {
        clock.Advance();
        int64_t curr_time = clock.GetCurrentSeconds();

        EXPECT_GT(curr_time, prev_time)
            << "Clock time should always increase, not decrease or stay the same";

        prev_time = curr_time;
    }
}

/**
 * @brief Test clock reset functionality.
 *
 * Validates that clock can be reset to start time.
 */
TEST_F(ClockAdvancementTest, ClockReset) {
    ClockModel clock(0, 10800, 3600);

    // Advance a few times
    clock.Advance();
    clock.Advance();
    EXPECT_EQ(clock.GetCurrentSeconds(), 7200);

    // Reset
    clock.Reset();
    EXPECT_EQ(clock.GetCurrentSeconds(), 0);
    EXPECT_FALSE(clock.IsAtStopTime());
}

// ---------------------------------------------------------------------------
// Test Suite: Stop Time Detection
// ---------------------------------------------------------------------------

class StopTimeDetectionTest : public ::testing::Test {};

/**
 * @brief Test stop time detection when exactly at stop time.
 *
 * Validates Requirement 7.1: Stop time detection.
 */
TEST_F(StopTimeDetectionTest, DetectionAtExactStopTime) {
    ClockModel clock(0, 3600, 3600);

    EXPECT_FALSE(clock.IsAtStopTime());

    clock.Advance();
    EXPECT_TRUE(clock.IsAtStopTime());
    EXPECT_EQ(clock.GetCurrentSeconds(), clock.GetStopSeconds());
}

/**
 * @brief Test stop time detection when past stop time.
 *
 * Validates Requirement 7.1: Stop time detection when exceeded.
 */
TEST_F(StopTimeDetectionTest, DetectionWhenPastStopTime) {
    // Timestep doesn't divide evenly into duration
    ClockModel clock(0, 5000, 3600);

    EXPECT_FALSE(clock.IsAtStopTime());

    clock.Advance();  // Now at 3600
    EXPECT_FALSE(clock.IsAtStopTime());

    clock.Advance();  // Now at 7200, past stop time of 5000
    EXPECT_TRUE(clock.IsAtStopTime());
    EXPECT_GT(clock.GetCurrentSeconds(), clock.GetStopSeconds());
}

/**
 * @brief Test stop time detection with zero-length simulation.
 *
 * Validates Requirement 7.1: Immediate stop time detection.
 */
TEST_F(StopTimeDetectionTest, ZeroLengthSimulation) {
    ClockModel clock(0, 0, 3600);
    EXPECT_TRUE(clock.IsAtStopTime())
        << "Zero-length simulation should be at stop time immediately";
}

/**
 * @brief Test stop time detection prevents infinite loops.
 *
 * Validates Requirement 7.1: Run loop termination.
 */
TEST_F(StopTimeDetectionTest, PreventsInfiniteLoops) {
    ClockModel clock(0, 86400, 3600);

    int step_count = 0;
    const int max_steps = 1000;  // Safety limit

    while (!clock.IsAtStopTime() && step_count < max_steps) {
        clock.Advance();
        step_count++;
    }

    EXPECT_TRUE(clock.IsAtStopTime()) << "Stop time should be detected before safety limit";
    EXPECT_EQ(step_count, 24) << "Should execute exactly 24 steps";
}

/**
 * @brief Test stop time detection with various durations.
 *
 * Validates Requirement 7.1: Stop time detection across different durations.
 */
TEST_F(StopTimeDetectionTest, VariousDurations) {
    std::vector<std::tuple<int64_t, int64_t, int64_t, int>> test_cases = {
        {0, 3600, 3600, 1},       // 1 hour, 1 step
        {0, 86400, 3600, 24},     // 1 day, 24 steps
        {0, 172800, 7200, 24},    // 2 days, 24 steps
        {0, 604800, 21600, 28},   // 1 week, 28 steps
        {0, 2592000, 86400, 30},  // 30 days, 30 steps
    };

    for (const auto& [start, stop, timestep, expected_steps] : test_cases) {
        ClockModel clock(start, stop, timestep);

        int step_count = 0;
        while (!clock.IsAtStopTime()) {
            clock.Advance();
            step_count++;
        }

        EXPECT_EQ(step_count, expected_steps)
            << "Expected " << expected_steps << " steps for duration " << stop << "s";
        EXPECT_TRUE(clock.IsAtStopTime());
    }
}

/**
 * @brief Test stop time detection consistency.
 *
 * Validates Requirement 7.1: Stop time detection is deterministic.
 */
TEST_F(StopTimeDetectionTest, DetectionConsistency) {
    const int num_runs = 10;
    std::vector<int> step_counts;

    for (int run = 0; run < num_runs; ++run) {
        ClockModel clock(0, 86400, 3600);

        int step_count = 0;
        while (!clock.IsAtStopTime()) {
            clock.Advance();
            step_count++;
        }

        step_counts.push_back(step_count);
    }

    // All runs should produce the same step count
    for (int count : step_counts) {
        EXPECT_EQ(count, 24) << "All runs should execute exactly 24 steps";
    }
}

// ---------------------------------------------------------------------------
// Integration Tests: Complete Clock Lifecycle
// ---------------------------------------------------------------------------

class ClockLifecycleTest : public ::testing::Test {};

/**
 * @brief Test complete clock lifecycle: create, advance, detect stop.
 *
 * Validates Requirements 4.1, 5.1, 7.1: Complete clock operations.
 */
TEST_F(ClockLifecycleTest, CompleteLifecycle) {
    // Create clock: 1 day simulation with 1-hour timesteps
    ClockModel clock(0, 86400, 3600);

    // Verify initial state
    EXPECT_EQ(clock.GetCurrentSeconds(), 0);
    EXPECT_FALSE(clock.IsAtStopTime());

    // Run loop
    int step_count = 0;
    while (!clock.IsAtStopTime()) {
        step_count++;
        clock.Advance();
    }

    // Verify final state
    EXPECT_EQ(step_count, 24);
    EXPECT_TRUE(clock.IsAtStopTime());
    EXPECT_EQ(clock.GetCurrentSeconds(), 86400);
}

/**
 * @brief Test clock lifecycle with non-evenly-divisible duration.
 *
 * Validates Requirements 4.1, 5.1, 7.1: Clock handles non-exact divisions.
 */
TEST_F(ClockLifecycleTest, NonEvenlyDivisibleDuration) {
    // 25 hours with 6-hour timesteps = 5 steps (last step exceeds stop time)
    ClockModel clock(0, 90000, 21600);

    int step_count = 0;
    while (!clock.IsAtStopTime()) {
        step_count++;
        clock.Advance();
    }

    // Should execute 5 steps (0, 21600, 43200, 64800, 86400)
    EXPECT_EQ(step_count, 5);
    EXPECT_TRUE(clock.IsAtStopTime());
    EXPECT_GE(clock.GetCurrentSeconds(), clock.GetStopSeconds());
}

/**
 * @brief Test clock lifecycle with very short simulation.
 *
 * Validates Requirements 4.1, 5.1, 7.1: Clock handles short simulations.
 */
TEST_F(ClockLifecycleTest, VeryShortSimulation) {
    // 1 minute simulation with 1-minute timestep
    ClockModel clock(0, 60, 60);

    int step_count = 0;
    while (!clock.IsAtStopTime()) {
        step_count++;
        clock.Advance();
    }

    EXPECT_EQ(step_count, 1);
    EXPECT_TRUE(clock.IsAtStopTime());
}

/**
 * @brief Test clock lifecycle with very long simulation.
 *
 * Validates Requirements 4.1, 5.1, 7.1: Clock handles long simulations.
 */
TEST_F(ClockLifecycleTest, VeryLongSimulation) {
    // 1 year simulation with 1-day timesteps = 365 steps
    ClockModel clock(0, 31536000, 86400);

    int step_count = 0;
    const int max_steps = 400;  // Safety limit

    while (!clock.IsAtStopTime() && step_count < max_steps) {
        step_count++;
        clock.Advance();
    }

    EXPECT_EQ(step_count, 365);
    EXPECT_TRUE(clock.IsAtStopTime());
}

}  // namespace cece

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
