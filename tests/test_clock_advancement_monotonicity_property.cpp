/**
 * @file test_clock_advancement_monotonicity_property.cpp
 * @brief Property-based test for clock advancement monotonicity.
 *
 * **Validates: Requirements 5.1, 5.2**
 *
 * Property 5: Clock Advancement Monotonicity
 *
 * FOR ALL sequences of clock advancements, the clock time MUST strictly increase
 * by exactly one timestep with each advancement.
 *
 * This property ensures that:
 * - Clock time never decreases
 * - Clock time never stays the same after advancement
 * - Clock time increases by exactly the configured timestep
 * - The advancement is deterministic and consistent
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

namespace aces {

/**
 * @brief Simple clock model for testing advancement logic.
 *
 * This models the essential behavior of ESMF_Clock for testing purposes.
 */
class TestClock {
   public:
    TestClock(int64_t start_seconds, int64_t stop_seconds, int64_t timestep_seconds)
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
 * @brief Property-based test suite for clock advancement monotonicity.
 */
class ClockAdvancementMonotonicityPropertyTest : public ::testing::Test {
   protected:
    std::mt19937 rng{42};  // Deterministic seed for reproducibility
};

/**
 * @brief Test that clock time strictly increases with each advancement.
 *
 * This test creates clocks with various configurations and verifies that
 * advancing the clock always increases the time by exactly one timestep.
 */
TEST_F(ClockAdvancementMonotonicityPropertyTest, ClockTimeStrictlyIncreases) {
    // Test various clock configurations (in seconds)
    std::vector<std::tuple<int64_t, int64_t, int64_t>> test_cases = {
        {0, 86400, 3600},     // 1 day, 1-hour timesteps
        {0, 172800, 7200},    // 2 days, 2-hour timesteps
        {0, 345600, 10800},   // 4 days, 3-hour timesteps
        {0, 2592000, 86400},  // 30 days, 1-day timesteps
        {0, 0, 900},  // Same start/stop, 15-minute timesteps (should be at stop immediately)
        {1592179200, 1592611200, 21600},  // Mid-year, 6-hour timesteps
        {1609372800, 1609804800, 43200},  // Year boundary, 12-hour timesteps
    };

    for (const auto& [start_seconds, stop_seconds, timestep_seconds] : test_cases) {
        TestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int64_t prev_time = clock.GetCurrentSeconds();
        int step_count = 0;
        const int max_steps = 1000;  // Limit iterations to prevent infinite loops

        while (!clock.IsAtStopTime() && step_count < max_steps) {
            // Advance clock
            clock.Advance();

            // Get new time
            int64_t curr_time = clock.GetCurrentSeconds();

            // Verify monotonicity: current time must be strictly greater than previous time
            EXPECT_GT(curr_time, prev_time)
                << "Clock time did not increase at step " << step_count << "\n"
                << "  Configuration: start=" << start_seconds << "s, stop=" << stop_seconds
                << "s, timestep=" << timestep_seconds << "s\n"
                << "  Previous time: " << prev_time << "s\n"
                << "  Current time:  " << curr_time << "s";

            // Verify the increase is exactly equal to the timestep
            int64_t time_diff = curr_time - prev_time;
            EXPECT_EQ(time_diff, timestep_seconds)
                << "Clock time increase does not match timestep at step " << step_count << "\n"
                << "  Configuration: start=" << start_seconds << "s, stop=" << stop_seconds
                << "s, timestep=" << timestep_seconds << "s\n"
                << "  Expected diff: " << timestep_seconds << "s\n"
                << "  Actual diff:   " << time_diff << "s";

            prev_time = curr_time;
            step_count++;
        }

        // Verify we didn't hit the max_steps limit (unless it's a very long simulation)
        if (step_count >= max_steps && !clock.IsAtStopTime()) {
            ADD_FAILURE() << "Clock did not reach stop time within " << max_steps << " steps";
        }
    }
}

/**
 * @brief Test that clock advancement is consistent across multiple runs.
 *
 * This test verifies that advancing a clock with the same configuration
 * produces the same sequence of times every time.
 */
TEST_F(ClockAdvancementMonotonicityPropertyTest, ClockAdvancementIsConsistent) {
    const int num_runs = 10;
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 172800;    // 2 days
    const int64_t timestep_seconds = 3600;  // 1 hour

    std::vector<std::vector<int64_t>> time_sequences;

    for (int run = 0; run < num_runs; ++run) {
        TestClock clock(start_seconds, stop_seconds, timestep_seconds);

        std::vector<int64_t> times;
        times.push_back(clock.GetCurrentSeconds());

        while (!clock.IsAtStopTime()) {
            clock.Advance();
            times.push_back(clock.GetCurrentSeconds());
        }

        time_sequences.push_back(times);
    }

    // Verify all runs produced the same sequence
    ASSERT_GT(time_sequences.size(), 1) << "Need at least 2 runs to compare";

    const auto& reference_sequence = time_sequences[0];
    for (size_t run = 1; run < time_sequences.size(); ++run) {
        EXPECT_EQ(time_sequences[run].size(), reference_sequence.size())
            << "Run " << run << " produced different number of timesteps";

        for (size_t i = 0; i < std::min(time_sequences[run].size(), reference_sequence.size());
             ++i) {
            EXPECT_EQ(time_sequences[run][i], reference_sequence[i])
                << "Run " << run << " produced different time at step " << i;
        }
    }
}

/**
 * @brief Test that clock never decreases or stays the same after advancement.
 *
 * This test specifically verifies that clock time never goes backwards
 * and never stays the same after an advancement call.
 */
TEST_F(ClockAdvancementMonotonicityPropertyTest, ClockNeverDecreasesOrStaysTheSame) {
    // Test with random timestep values
    std::uniform_int_distribution<int64_t> timestep_dist(900, 86400);  // 15 minutes to 1 day

    const int num_tests = 100;

    for (int test = 0; test < num_tests; ++test) {
        int64_t timestep_seconds = timestep_dist(rng);
        int64_t start_seconds = 0;
        int64_t stop_seconds = 864000;  // 10 days

        TestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int64_t prev_time = clock.GetCurrentSeconds();
        int step_count = 0;
        const int max_steps = 500;

        while (!clock.IsAtStopTime() && step_count < max_steps) {
            clock.Advance();
            int64_t curr_time = clock.GetCurrentSeconds();

            // Verify time never decreases
            EXPECT_GE(curr_time, prev_time) << "Clock time decreased at step " << step_count
                                            << " (timestep: " << timestep_seconds << "s)";

            // Verify time never stays the same (strict monotonicity)
            EXPECT_NE(curr_time, prev_time) << "Clock time did not change at step " << step_count
                                            << " (timestep: " << timestep_seconds << "s)";

            // Verify exact increment
            EXPECT_EQ(curr_time - prev_time, timestep_seconds)
                << "Clock time increment incorrect at step " << step_count
                << " (timestep: " << timestep_seconds << "s)";

            prev_time = curr_time;
            step_count++;
        }
    }
}

/**
 * @brief Test clock advancement with various timestep sizes.
 *
 * This test verifies monotonicity across a wide range of timestep sizes,
 * from very small (seconds) to very large (days).
 */
TEST_F(ClockAdvancementMonotonicityPropertyTest, MonotonicityAcrossVariousTimestepSizes) {
    // Test various timestep sizes (in seconds)
    std::vector<int64_t> timestep_sizes = {
        1,       // 1 second
        60,      // 1 minute
        300,     // 5 minutes
        900,     // 15 minutes
        1800,    // 30 minutes
        3600,    // 1 hour
        7200,    // 2 hours
        10800,   // 3 hours
        21600,   // 6 hours
        43200,   // 12 hours
        86400,   // 1 day
        172800,  // 2 days
        604800,  // 1 week
    };

    for (int64_t timestep_seconds : timestep_sizes) {
        int64_t start_seconds = 0;
        int64_t stop_seconds = timestep_seconds * 10;  // 10 timesteps

        TestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int64_t prev_time = clock.GetCurrentSeconds();
        int step_count = 0;

        while (!clock.IsAtStopTime()) {
            clock.Advance();
            int64_t curr_time = clock.GetCurrentSeconds();

            EXPECT_GT(curr_time, prev_time)
                << "Clock time did not increase for timestep size " << timestep_seconds << "s";

            EXPECT_EQ(curr_time - prev_time, timestep_seconds)
                << "Clock time increment incorrect for timestep size " << timestep_seconds << "s";

            prev_time = curr_time;
            step_count++;
        }

        // Should have exactly 10 steps
        EXPECT_EQ(step_count, 10) << "Expected 10 steps for timestep size " << timestep_seconds
                                  << "s";
    }
}

/**
 * @brief Test clock advancement with random configurations.
 *
 * This is a true property-based test that generates random clock configurations
 * and verifies monotonicity holds for all of them.
 */
TEST_F(ClockAdvancementMonotonicityPropertyTest, MonotonicityHoldsForRandomConfigurations) {
    std::uniform_int_distribution<int64_t> start_dist(0, 1000000);
    std::uniform_int_distribution<int64_t> duration_dist(1000, 1000000);
    std::uniform_int_distribution<int64_t> timestep_dist(1, 10000);

    const int num_tests = 200;

    for (int test = 0; test < num_tests; ++test) {
        int64_t start_seconds = start_dist(rng);
        int64_t duration_seconds = duration_dist(rng);
        int64_t stop_seconds = start_seconds + duration_seconds;
        int64_t timestep_seconds = timestep_dist(rng);

        TestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int64_t prev_time = clock.GetCurrentSeconds();
        int step_count = 0;
        const int max_steps = 1000;

        while (!clock.IsAtStopTime() && step_count < max_steps) {
            clock.Advance();
            int64_t curr_time = clock.GetCurrentSeconds();

            // Verify strict monotonicity
            EXPECT_GT(curr_time, prev_time)
                << "Monotonicity violated for random config: start=" << start_seconds
                << "s, stop=" << stop_seconds << "s, timestep=" << timestep_seconds << "s";

            // Verify exact increment
            EXPECT_EQ(curr_time - prev_time, timestep_seconds)
                << "Increment incorrect for random config: start=" << start_seconds
                << "s, stop=" << stop_seconds << "s, timestep=" << timestep_seconds << "s";

            prev_time = curr_time;
            step_count++;
        }
    }
}

/**
 * @brief Test that zero-length simulations handle correctly.
 *
 * This test verifies that clocks where start == stop are immediately at stop time
 * and don't advance.
 */
TEST_F(ClockAdvancementMonotonicityPropertyTest, ZeroLengthSimulationsHandleCorrectly) {
    std::vector<int64_t> start_times = {0, 1000, 86400, 1000000};

    for (int64_t start_seconds : start_times) {
        TestClock clock(start_seconds, start_seconds, 3600);  // start == stop

        // Should be at stop time immediately
        EXPECT_TRUE(clock.IsAtStopTime())
            << "Clock with start==stop should be at stop time immediately (start=" << start_seconds
            << "s)";

        // Current time should equal start time
        EXPECT_EQ(clock.GetCurrentSeconds(), start_seconds)
            << "Clock current time should equal start time for zero-length simulation";
    }
}

}  // namespace aces
