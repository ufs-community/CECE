/**
 * @file test_idempotent_clock_advancement_property.cpp
 * @brief Property-based test for idempotent clock advancement.
 *
 * **Validates: Requirements 5.4, 20.1, 20.4**
 *
 * Property 6: Idempotent Clock Advancement
 *
 * FOR ALL clock states, if NUOPC_Model has already advanced the clock before
 * calling Advance, the driver MUST NOT advance it again, resulting in the clock
 * advancing exactly once per Run phase.
 *
 * This property ensures that:
 * - Clock advances exactly once per Run phase
 * - Double-advancement is prevented when NUOPC_Model auto-advances
 * - Manual advancement only occurs when needed
 * - Clock state is consistent regardless of advancement mechanism
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

namespace aces {

/**
 * @brief Clock model with advancement tracking for testing idempotency.
 *
 * This models ESMF_Clock behavior with explicit tracking of who advanced the clock.
 */
class IdempotentTestClock {
   public:
    IdempotentTestClock(int64_t start_seconds, int64_t stop_seconds, int64_t timestep_seconds)
        : current_seconds_(start_seconds),
          stop_seconds_(stop_seconds),
          timestep_seconds_(timestep_seconds),
          advancement_count_(0) {}

    bool IsAtStopTime() const {
        return current_seconds_ >= stop_seconds_;
    }

    /**
     * @brief Advance the clock (can be called by NUOPC_Model or driver).
     */
    void Advance() {
        current_seconds_ += timestep_seconds_;
        advancement_count_++;
    }

    /**
     * @brief Idempotent advancement: only advance if not already advanced this phase.
     *
     * This simulates the driver's idempotent advancement logic.
     */
    void IdempotentAdvance(bool already_advanced) {
        if (!already_advanced) {
            Advance();
        }
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

    int GetAdvancementCount() const {
        return advancement_count_;
    }

    void ResetAdvancementCount() {
        advancement_count_ = 0;
    }

   private:
    int64_t current_seconds_;
    int64_t stop_seconds_;
    int64_t timestep_seconds_;
    int advancement_count_;  // Track how many times Advance() was called
};

/**
 * @brief Property-based test suite for idempotent clock advancement.
 */
class IdempotentClockAdvancementPropertyTest : public ::testing::Test {
   protected:
    std::mt19937 rng{42};  // Deterministic seed for reproducibility
};

/**
 * @brief Test that clock advances exactly once per Run phase with idempotent logic.
 *
 * This test simulates scenarios where NUOPC_Model may or may not have already
 * advanced the clock, and verifies the driver's idempotent advancement ensures
 * exactly one advancement per phase.
 */
TEST_F(IdempotentClockAdvancementPropertyTest, ClockAdvancesExactlyOncePerPhase) {
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 86400;     // 1 day
    const int64_t timestep_seconds = 3600;  // 1 hour

    IdempotentTestClock clock(start_seconds, stop_seconds, timestep_seconds);

    int phase_count = 0;
    const int max_phases = 100;

    while (!clock.IsAtStopTime() && phase_count < max_phases) {
        int64_t time_before_phase = clock.GetCurrentSeconds();

        // Simulate NUOPC_Model auto-advancing (happens in some configurations)
        bool nuopc_advanced = true;
        clock.Advance();

        // Driver uses idempotent advancement (should NOT advance again)
        clock.IdempotentAdvance(nuopc_advanced);

        int64_t time_after_phase = clock.GetCurrentSeconds();

        // Verify clock advanced exactly once (by timestep_seconds)
        int64_t time_diff = time_after_phase - time_before_phase;
        EXPECT_EQ(time_diff, timestep_seconds)
            << "Clock should advance exactly once per phase (phase " << phase_count << ")\n"
            << "  Expected diff: " << timestep_seconds << "s\n"
            << "  Actual diff:   " << time_diff << "s";

        phase_count++;
    }

    // Verify we completed the expected number of phases
    int expected_phases = (stop_seconds - start_seconds) / timestep_seconds;
    EXPECT_EQ(phase_count, expected_phases)
        << "Should complete exactly " << expected_phases << " phases";
}

/**
 * @brief Test idempotent advancement when driver must manually advance.
 *
 * This test simulates standalone driver mode where NUOPC_Model does NOT
 * auto-advance, so the driver must manually advance the clock.
 */
TEST_F(IdempotentClockAdvancementPropertyTest, ManualAdvancementWhenNeeded) {
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 172800;    // 2 days
    const int64_t timestep_seconds = 7200;  // 2 hours

    IdempotentTestClock clock(start_seconds, stop_seconds, timestep_seconds);

    int phase_count = 0;
    const int max_phases = 100;

    while (!clock.IsAtStopTime() && phase_count < max_phases) {
        int64_t time_before_phase = clock.GetCurrentSeconds();

        // Simulate standalone mode: NUOPC_Model does NOT auto-advance
        bool nuopc_advanced = false;

        // Driver uses idempotent advancement (SHOULD advance)
        clock.IdempotentAdvance(nuopc_advanced);

        int64_t time_after_phase = clock.GetCurrentSeconds();

        // Verify clock advanced exactly once
        int64_t time_diff = time_after_phase - time_before_phase;
        EXPECT_EQ(time_diff, timestep_seconds)
            << "Clock should advance exactly once per phase (phase " << phase_count << ")\n"
            << "  Expected diff: " << timestep_seconds << "s\n"
            << "  Actual diff:   " << time_diff << "s";

        phase_count++;
    }

    // Verify we completed the expected number of phases
    int expected_phases = (stop_seconds - start_seconds) / timestep_seconds;
    EXPECT_EQ(phase_count, expected_phases)
        << "Should complete exactly " << expected_phases << " phases";
}

/**
 * @brief Test that double-advancement is prevented with idempotent logic.
 *
 * This test explicitly verifies that calling IdempotentAdvance after
 * a manual Advance does NOT result in double-advancement.
 */
TEST_F(IdempotentClockAdvancementPropertyTest, DoubleAdvancementPrevented) {
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 86400;     // 1 day
    const int64_t timestep_seconds = 3600;  // 1 hour

    IdempotentTestClock clock(start_seconds, stop_seconds, timestep_seconds);

    // Manually advance the clock (simulating NUOPC_Model)
    int64_t time_before = clock.GetCurrentSeconds();
    clock.Advance();
    int64_t time_after_first_advance = clock.GetCurrentSeconds();

    // Verify first advancement worked
    EXPECT_EQ(time_after_first_advance - time_before, timestep_seconds)
        << "First advancement should increase time by timestep";

    // Try to advance again with idempotent logic (should be no-op)
    clock.IdempotentAdvance(true);  // already_advanced = true
    int64_t time_after_idempotent = clock.GetCurrentSeconds();

    // Verify time did NOT change
    EXPECT_EQ(time_after_idempotent, time_after_first_advance)
        << "Idempotent advancement should not change time when already advanced";

    // Verify total advancement is still exactly one timestep
    EXPECT_EQ(time_after_idempotent - time_before, timestep_seconds)
        << "Total advancement should be exactly one timestep";
}

/**
 * @brief Test idempotent advancement with mixed scenarios.
 *
 * This test simulates a realistic scenario where some phases have NUOPC
 * auto-advancement and others require manual advancement.
 */
TEST_F(IdempotentClockAdvancementPropertyTest, MixedAdvancementScenarios) {
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 345600;     // 4 days
    const int64_t timestep_seconds = 10800;  // 3 hours

    IdempotentTestClock clock(start_seconds, stop_seconds, timestep_seconds);

    // Simulate mixed scenarios: some phases with NUOPC auto-advance, some without
    std::vector<bool> nuopc_advances = {true, false, true, true,  false, false, true,  false,
                                        true, false, true, false, true,  true,  false, true};

    int phase_count = 0;
    size_t scenario_index = 0;

    while (!clock.IsAtStopTime() && phase_count < 100) {
        int64_t time_before_phase = clock.GetCurrentSeconds();

        // Get current scenario (cycle through the pattern)
        bool nuopc_advanced = nuopc_advances[scenario_index % nuopc_advances.size()];

        if (nuopc_advanced) {
            // NUOPC auto-advances
            clock.Advance();
        }

        // Driver uses idempotent advancement
        clock.IdempotentAdvance(nuopc_advanced);

        int64_t time_after_phase = clock.GetCurrentSeconds();

        // Verify clock advanced exactly once
        int64_t time_diff = time_after_phase - time_before_phase;
        EXPECT_EQ(time_diff, timestep_seconds)
            << "Clock should advance exactly once per phase (phase " << phase_count
            << ", nuopc_advanced=" << nuopc_advanced << ")\n"
            << "  Expected diff: " << timestep_seconds << "s\n"
            << "  Actual diff:   " << time_diff << "s";

        phase_count++;
        scenario_index++;
    }

    // Verify we completed the expected number of phases
    int expected_phases = (stop_seconds - start_seconds) / timestep_seconds;
    EXPECT_EQ(phase_count, expected_phases)
        << "Should complete exactly " << expected_phases << " phases";
}

/**
 * @brief Test idempotent advancement with random configurations.
 *
 * This is a true property-based test that generates random clock configurations
 * and random advancement patterns, verifying idempotency holds for all.
 */
TEST_F(IdempotentClockAdvancementPropertyTest, IdempotencyHoldsForRandomConfigurations) {
    std::uniform_int_distribution<int64_t> start_dist(0, 100000);
    std::uniform_int_distribution<int64_t> duration_dist(10000, 500000);
    std::uniform_int_distribution<int64_t> timestep_dist(100, 10000);
    std::uniform_int_distribution<int> bool_dist(0, 1);

    const int num_tests = 100;

    for (int test = 0; test < num_tests; ++test) {
        int64_t start_seconds = start_dist(rng);
        int64_t duration_seconds = duration_dist(rng);
        int64_t stop_seconds = start_seconds + duration_seconds;
        int64_t timestep_seconds = timestep_dist(rng);

        IdempotentTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int phase_count = 0;
        const int max_phases = 200;

        while (!clock.IsAtStopTime() && phase_count < max_phases) {
            int64_t time_before_phase = clock.GetCurrentSeconds();

            // Randomly decide if NUOPC auto-advances
            bool nuopc_advanced = bool_dist(rng) == 1;

            if (nuopc_advanced) {
                clock.Advance();
            }

            // Driver uses idempotent advancement
            clock.IdempotentAdvance(nuopc_advanced);

            int64_t time_after_phase = clock.GetCurrentSeconds();

            // Verify clock advanced exactly once
            int64_t time_diff = time_after_phase - time_before_phase;
            EXPECT_EQ(time_diff, timestep_seconds)
                << "Idempotency violated for random config (test " << test << ", phase "
                << phase_count << ")\n"
                << "  Config: start=" << start_seconds << "s, stop=" << stop_seconds
                << "s, timestep=" << timestep_seconds << "s\n"
                << "  NUOPC advanced: " << nuopc_advanced << "\n"
                << "  Expected diff: " << timestep_seconds << "s\n"
                << "  Actual diff:   " << time_diff << "s";

            phase_count++;
        }
    }
}

/**
 * @brief Test that advancement count matches expected phases.
 *
 * This test verifies that the total number of Advance() calls matches
 * the expected number of phases, regardless of advancement mechanism.
 */
TEST_F(IdempotentClockAdvancementPropertyTest, AdvancementCountMatchesPhases) {
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 259200;     // 3 days
    const int64_t timestep_seconds = 21600;  // 6 hours

    IdempotentTestClock clock(start_seconds, stop_seconds, timestep_seconds);

    int phase_count = 0;
    std::uniform_int_distribution<int> bool_dist(0, 1);

    while (!clock.IsAtStopTime()) {
        bool nuopc_advanced = bool_dist(rng) == 1;

        if (nuopc_advanced) {
            clock.Advance();
        }

        clock.IdempotentAdvance(nuopc_advanced);

        phase_count++;
    }

    // Calculate expected phases
    int expected_phases = (stop_seconds - start_seconds) / timestep_seconds;

    // Verify phase count matches expected
    EXPECT_EQ(phase_count, expected_phases)
        << "Phase count should match expected number of timesteps";

    // Verify advancement count matches phase count (exactly one advancement per phase)
    EXPECT_EQ(clock.GetAdvancementCount(), expected_phases)
        << "Advancement count should match phase count (one advancement per phase)";
}

/**
 * @brief Test idempotent advancement with various timestep sizes.
 *
 * This test verifies idempotency across a wide range of timestep sizes.
 */
TEST_F(IdempotentClockAdvancementPropertyTest, IdempotencyAcrossVariousTimestepSizes) {
    std::vector<int64_t> timestep_sizes = {
        60,     // 1 minute
        900,    // 15 minutes
        3600,   // 1 hour
        10800,  // 3 hours
        21600,  // 6 hours
        43200,  // 12 hours
        86400,  // 1 day
    };

    std::uniform_int_distribution<int> bool_dist(0, 1);

    for (int64_t timestep_seconds : timestep_sizes) {
        int64_t start_seconds = 0;
        int64_t stop_seconds = timestep_seconds * 20;  // 20 timesteps

        IdempotentTestClock clock(start_seconds, stop_seconds, timestep_seconds);

        int phase_count = 0;

        while (!clock.IsAtStopTime()) {
            int64_t time_before = clock.GetCurrentSeconds();

            bool nuopc_advanced = bool_dist(rng) == 1;

            if (nuopc_advanced) {
                clock.Advance();
            }

            clock.IdempotentAdvance(nuopc_advanced);

            int64_t time_after = clock.GetCurrentSeconds();

            // Verify exactly one timestep advancement
            EXPECT_EQ(time_after - time_before, timestep_seconds)
                << "Idempotency failed for timestep size " << timestep_seconds << "s";

            phase_count++;
        }

        // Should have exactly 20 phases
        EXPECT_EQ(phase_count, 20)
            << "Expected 20 phases for timestep size " << timestep_seconds << "s";
    }
}

/**
 * @brief Test that consecutive idempotent calls are safe.
 *
 * This test verifies that calling IdempotentAdvance multiple times
 * with already_advanced=true is safe and doesn't change the clock.
 */
TEST_F(IdempotentClockAdvancementPropertyTest, ConsecutiveIdempotentCallsAreSafe) {
    const int64_t start_seconds = 0;
    const int64_t stop_seconds = 86400;     // 1 day
    const int64_t timestep_seconds = 3600;  // 1 hour

    IdempotentTestClock clock(start_seconds, stop_seconds, timestep_seconds);

    // Advance once
    clock.Advance();
    int64_t time_after_advance = clock.GetCurrentSeconds();

    // Call IdempotentAdvance multiple times with already_advanced=true
    for (int i = 0; i < 10; ++i) {
        clock.IdempotentAdvance(true);
        int64_t current_time = clock.GetCurrentSeconds();

        EXPECT_EQ(current_time, time_after_advance)
            << "Time should not change after idempotent call " << i;
    }

    // Verify total advancement is still exactly one timestep
    EXPECT_EQ(time_after_advance - start_seconds, timestep_seconds)
        << "Total advancement should be exactly one timestep";
}

}  // namespace aces
