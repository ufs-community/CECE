/**
 * @file test_stop_time_detection_property.cpp
 * @brief Property-based test for stop time detection.
 *
 * **Validates: Requirements 7.1, 7.2**
 *
 * Property 8: Stop Time Detection
 *
 * FOR ALL clocks that have reached or exceeded their stop time, ESMF_ClockIsStopTime
 * MUST return true, causing the run loop to terminate.
 *
 * This property ensures that:
 * - Stop time is detected when current time equals stop time
 * - Stop time is detected when current time exceeds stop time
 * - Run loop terminates at the correct time
 * - No extra timesteps are executed after reaching stop time
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

namespace aces {

/**
 * @brief Simple clock model for testing stop time detection.
 *
 * This models the essential behavior of ESMF_Clock for testing purposes.
 */
class StopTimeTestClock {
   public:
    StopTimeTestClock(int64_t start_seconds, int64_t stop_seconds, int64_t timestep_seconds)
        : current_seconds_(start_seconds),
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

    int64_t GetStopSeconds() const {
        return stop_seconds_;
    }

    int64_t GetTimestepSeconds() const {
        return timestep_seconds_;
    }

   private:
    int64_t current_seconds_;
    int64_t stop_seconds_;
    int64_t timestep_seconds_;
};

/**
 * @brief Property-based test suite for stop time detection.
 */
class StopTimeDetectionPropertyTest : public ::testing::Test {
   protected:
    std::mt19937 rng{42};  // Deterministic seed for reproducibility
};

/**
 * @brief Test that stop time is detected when current time equals stop time.
 *
 * This test verifies that IsAtStopTime returns true when the clock
 * reaches exactly the stop time.
 */
TEST_F(StopTimeDetectionPropertyTest, StopTimeDetectedWhenEqual) {
    // Test various configurations where we reach exactly the stop time
    std::vector<std::tuple<int64_t, int64_t, int64_t>> test_cases = {
        {0, 3600, 3600},                  // 1 timestep, reaches exactly
        {0, 7200, 3600},                  // 2 timesteps, reaches exactly
        {0, 86400, 3600},                 // 24 timesteps, reaches exactly
        {0, 172800, 7200},                // 24 timesteps, reaches exactly
        {1000, 11000, 1000},              // 10 timesteps, reaches exactly
        {1592179200, 1592265600, 21600},  // 4 timesteps, reaches exactly
    };

    for (const auto& [start_seconds, stop_seconds, timestep_seconds] : test_cases) {
        StopTimeTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        // Advance until we should reach stop time
        int expected_steps = (stop_seconds - start_seconds) / timestep_seconds;
        int step_count = 0;

        while (step_count < expected_steps) {
            EXPECT_FALSE(clock.IsAtStopTime())
                << "Clock should not be at stop time before reaching it (step " << step_count
                << ")\n"
                << "  Config: start=" << start_seconds << "s, stop=" << stop_seconds
                << "s, timestep=" << timestep_seconds << "s\n"
                << "  Current time: " << clock.GetCurrentSeconds() << "s";

            clock.Advance();
            step_count++;
        }

        // Now we should be at stop time
        EXPECT_TRUE(clock.IsAtStopTime())
            << "Clock should be at stop time after " << expected_steps << " steps\n"
            << "  Config: start=" << start_seconds << "s, stop=" << stop_seconds
            << "s, timestep=" << timestep_seconds << "s\n"
            << "  Current time: " << clock.GetCurrentSeconds() << "s\n"
            << "  Stop time: " << clock.GetStopSeconds() << "s";

        // Verify current time equals stop time
        EXPECT_EQ(clock.GetCurrentSeconds(), stop_seconds) << "Current time should equal stop time";
    }
}

/**
 * @brief Test that stop time is detected when current time exceeds stop time.
 *
 * This test verifies that IsAtStopTime returns true when the clock
 * advances past the stop time (timestep doesn't divide evenly).
 */
TEST_F(StopTimeDetectionPropertyTest, StopTimeDetectedWhenExceeded) {
    // Test configurations where timestep doesn't divide evenly into duration
    std::vector<std::tuple<int64_t, int64_t, int64_t>> test_cases = {
        {0, 5000, 3600},      // Exceeds by 1400s
        {0, 10000, 3600},     // Exceeds by 2800s
        {0, 100000, 7200},    // Exceeds by 6400s
        {0, 86400, 10800},    // Exceeds by 3600s
        {1000, 10500, 3600},  // Exceeds by 1100s
    };

    for (const auto& [start_seconds, stop_seconds, timestep_seconds] : test_cases) {
        StopTimeTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int step_count = 0;
        const int max_steps = 1000;

        // Advance until we exceed stop time
        while (!clock.IsAtStopTime() && step_count < max_steps) {
            clock.Advance();
            step_count++;
        }

        // Should have detected stop time
        EXPECT_TRUE(clock.IsAtStopTime())
            << "Clock should detect stop time even when exceeded\n"
            << "  Config: start=" << start_seconds << "s, stop=" << stop_seconds
            << "s, timestep=" << timestep_seconds << "s\n"
            << "  Current time: " << clock.GetCurrentSeconds() << "s\n"
            << "  Stop time: " << clock.GetStopSeconds() << "s";

        // Verify current time is >= stop time
        EXPECT_GE(clock.GetCurrentSeconds(), stop_seconds) << "Current time should be >= stop time";

        // Verify we didn't run too many steps
        int max_expected_steps = ((stop_seconds - start_seconds) / timestep_seconds) + 1;
        EXPECT_LE(step_count, max_expected_steps)
            << "Should not execute more than " << max_expected_steps << " steps";
    }
}

/**
 * @brief Test that run loop terminates correctly at stop time.
 *
 * This test simulates a complete run loop and verifies it terminates
 * at the correct time without executing extra timesteps.
 */
TEST_F(StopTimeDetectionPropertyTest, RunLoopTerminatesCorrectly) {
    std::vector<std::tuple<int64_t, int64_t, int64_t>> test_cases = {
        {0, 86400, 3600},     // 1 day, 1-hour timesteps (24 steps)
        {0, 172800, 7200},    // 2 days, 2-hour timesteps (24 steps)
        {0, 259200, 10800},   // 3 days, 3-hour timesteps (24 steps)
        {0, 604800, 21600},   // 1 week, 6-hour timesteps (28 steps)
        {0, 2592000, 86400},  // 30 days, 1-day timesteps (30 steps)
    };

    for (const auto& [start_seconds, stop_seconds, timestep_seconds] : test_cases) {
        StopTimeTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int step_count = 0;
        const int max_steps = 10000;

        // Simulate run loop: while NOT at stop time, execute and advance
        while (!clock.IsAtStopTime() && step_count < max_steps) {
            step_count++;
            clock.Advance();
        }

        // Calculate expected steps
        int expected_steps = (stop_seconds - start_seconds) / timestep_seconds;

        // Verify we executed the correct number of steps
        EXPECT_EQ(step_count, expected_steps)
            << "Run loop should execute exactly " << expected_steps << " steps\n"
            << "  Config: start=" << start_seconds << "s, stop=" << stop_seconds
            << "s, timestep=" << timestep_seconds << "s\n"
            << "  Actual steps: " << step_count;

        // Verify we're at stop time
        EXPECT_TRUE(clock.IsAtStopTime())
            << "Clock should be at stop time after run loop completes";

        // Verify current time equals stop time (for evenly divisible cases)
        EXPECT_EQ(clock.GetCurrentSeconds(), stop_seconds) << "Current time should equal stop time";
    }
}

/**
 * @brief Test that zero-length simulations are detected immediately.
 *
 * This test verifies that when start time equals stop time,
 * IsAtStopTime returns true immediately without any timesteps.
 */
TEST_F(StopTimeDetectionPropertyTest, ZeroLengthSimulationsDetectedImmediately) {
    std::vector<int64_t> start_times = {0, 1000, 86400, 1000000, 1592179200};

    for (int64_t start_seconds : start_times) {
        StopTimeTestClock clock(start_seconds, start_seconds, 3600);  // start == stop

        // Should be at stop time immediately
        EXPECT_TRUE(clock.IsAtStopTime())
            << "Clock with start==stop should be at stop time immediately\n"
            << "  Start/Stop time: " << start_seconds << "s";

        // Verify no timesteps should be executed
        int step_count = 0;
        while (!clock.IsAtStopTime() && step_count < 10) {
            step_count++;
            clock.Advance();
        }

        EXPECT_EQ(step_count, 0) << "Zero-length simulation should execute 0 timesteps";
    }
}

/**
 * @brief Test stop time detection with random configurations.
 *
 * This is a true property-based test that generates random clock configurations
 * and verifies stop time detection works correctly for all of them.
 */
TEST_F(StopTimeDetectionPropertyTest, StopTimeDetectionWorksForRandomConfigurations) {
    std::uniform_int_distribution<int64_t> start_dist(0, 1000000);
    std::uniform_int_distribution<int64_t> duration_dist(1000, 1000000);
    std::uniform_int_distribution<int64_t> timestep_dist(100, 10000);

    const int num_tests = 200;

    for (int test = 0; test < num_tests; ++test) {
        int64_t start_seconds = start_dist(rng);
        int64_t duration_seconds = duration_dist(rng);
        int64_t stop_seconds = start_seconds + duration_seconds;
        int64_t timestep_seconds = timestep_dist(rng);

        StopTimeTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        // Should not be at stop time initially (unless zero-length)
        if (start_seconds < stop_seconds) {
            EXPECT_FALSE(clock.IsAtStopTime())
                << "Clock should not be at stop time initially (test " << test << ")\n"
                << "  Config: start=" << start_seconds << "s, stop=" << stop_seconds
                << "s, timestep=" << timestep_seconds << "s";
        }

        // Calculate expected steps and set safety limit
        int64_t expected_steps = ((stop_seconds - start_seconds) / timestep_seconds) + 1;
        int max_steps = static_cast<int>(expected_steps * 2);  // 2x safety margin
        if (max_steps < 100) max_steps = 100;                  // Minimum safety limit

        int step_count = 0;

        // Run until stop time detected
        while (!clock.IsAtStopTime() && step_count < max_steps) {
            clock.Advance();
            step_count++;
        }

        // Should have detected stop time
        EXPECT_TRUE(clock.IsAtStopTime())
            << "Stop time should be detected (test " << test << ")\n"
            << "  Config: start=" << start_seconds << "s, stop=" << stop_seconds
            << "s, timestep=" << timestep_seconds << "s\n"
            << "  Steps executed: " << step_count << " (max: " << max_steps << ")";

        // Verify current time is >= stop time
        EXPECT_GE(clock.GetCurrentSeconds(), stop_seconds)
            << "Current time should be >= stop time (test " << test << ")";

        // Verify we didn't run too many steps
        EXPECT_LE(step_count, expected_steps)
            << "Should not execute more than " << expected_steps << " steps (test " << test << ")";
    }
}

/**
 * @brief Test that stop time detection is consistent across multiple runs.
 *
 * This test verifies that running the same configuration multiple times
 * always detects stop time at the same point.
 */
TEST_F(StopTimeDetectionPropertyTest, StopTimeDetectionIsConsistent) {
    const int num_runs = 20;
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 172800;    // 2 days
    const int64_t timestep_seconds = 3600;  // 1 hour

    std::vector<int> step_counts;

    for (int run = 0; run < num_runs; ++run) {
        StopTimeTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int step_count = 0;
        while (!clock.IsAtStopTime()) {
            step_count++;
            clock.Advance();
        }

        step_counts.push_back(step_count);
    }

    // Verify all runs executed the same number of steps
    ASSERT_GT(step_counts.size(), 1) << "Need at least 2 runs to compare";

    int reference_steps = step_counts[0];
    for (size_t run = 1; run < step_counts.size(); ++run) {
        EXPECT_EQ(step_counts[run], reference_steps)
            << "Run " << run << " executed different number of steps\n"
            << "  Expected: " << reference_steps << "\n"
            << "  Actual: " << step_counts[run];
    }
}

/**
 * @brief Test stop time detection with various timestep sizes.
 *
 * This test verifies stop time detection works correctly across
 * a wide range of timestep sizes.
 */
TEST_F(StopTimeDetectionPropertyTest, StopTimeDetectionAcrossVariousTimestepSizes) {
    std::vector<int64_t> timestep_sizes = {
        1,      // 1 second
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

    for (int64_t timestep_seconds : timestep_sizes) {
        int64_t start_seconds = 0;
        int64_t stop_seconds = timestep_seconds * 10;  // 10 timesteps

        StopTimeTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int step_count = 0;
        while (!clock.IsAtStopTime()) {
            step_count++;
            clock.Advance();
        }

        // Should execute exactly 10 steps
        EXPECT_EQ(step_count, 10) << "Should execute exactly 10 steps for timestep size "
                                  << timestep_seconds << "s";

        // Should be at stop time
        EXPECT_TRUE(clock.IsAtStopTime())
            << "Should be at stop time for timestep size " << timestep_seconds << "s";

        // Current time should equal stop time
        EXPECT_EQ(clock.GetCurrentSeconds(), stop_seconds)
            << "Current time should equal stop time for timestep size " << timestep_seconds << "s";
    }
}

/**
 * @brief Test that stop time detection prevents infinite loops.
 *
 * This test verifies that the run loop always terminates and doesn't
 * run indefinitely due to incorrect stop time detection.
 */
TEST_F(StopTimeDetectionPropertyTest, StopTimeDetectionPreventsInfiniteLoops) {
    std::vector<std::tuple<int64_t, int64_t, int64_t>> test_cases = {
        {0, 86400, 3600},
        {0, 172800, 7200},
        {0, 259200, 10800},
        {1000, 100000, 5000},
        {1592179200, 1592611200, 21600},
    };

    for (const auto& [start_seconds, stop_seconds, timestep_seconds] : test_cases) {
        StopTimeTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int step_count = 0;
        const int max_steps = 10000;  // Safety limit

        while (!clock.IsAtStopTime() && step_count < max_steps) {
            step_count++;
            clock.Advance();
        }

        // Should have detected stop time before hitting safety limit
        EXPECT_TRUE(clock.IsAtStopTime())
            << "Stop time should be detected before safety limit\n"
            << "  Config: start=" << start_seconds << "s, stop=" << stop_seconds
            << "s, timestep=" << timestep_seconds << "s\n"
            << "  Steps executed: " << step_count << " (max: " << max_steps << ")";

        // Calculate expected steps
        int expected_steps = (stop_seconds - start_seconds) / timestep_seconds;

        // Should not have hit the safety limit
        EXPECT_LT(step_count, max_steps)
            << "Run loop should terminate naturally, not hit safety limit";

        // Should be close to expected steps
        EXPECT_LE(step_count, expected_steps + 1)
            << "Should execute at most " << (expected_steps + 1) << " steps";
    }
}

/**
 * @brief Test stop time detection with edge case: very long simulations.
 *
 * This test verifies stop time detection works correctly for
 * simulations with many timesteps.
 */
TEST_F(StopTimeDetectionPropertyTest, StopTimeDetectionForLongSimulations) {
    // Test a 1-year simulation with 1-hour timesteps (8760 steps)
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 31536000;  // 365 days
    const int64_t timestep_seconds = 3600;  // 1 hour

    StopTimeTestClock clock(start_seconds, stop_seconds, timestep_seconds);

    int step_count = 0;
    const int max_steps = 10000;  // Safety limit

    while (!clock.IsAtStopTime() && step_count < max_steps) {
        step_count++;
        clock.Advance();
    }

    // Calculate expected steps
    int expected_steps = (stop_seconds - start_seconds) / timestep_seconds;

    // Should execute exactly the expected number of steps
    EXPECT_EQ(step_count, expected_steps)
        << "Long simulation should execute exactly " << expected_steps << " steps";

    // Should be at stop time
    EXPECT_TRUE(clock.IsAtStopTime()) << "Long simulation should detect stop time correctly";

    // Current time should equal stop time
    EXPECT_EQ(clock.GetCurrentSeconds(), stop_seconds)
        << "Current time should equal stop time for long simulation";
}

}  // namespace aces
