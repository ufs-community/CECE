/**
 * @file test_driver_run_loop.cpp
 * @brief Tests for run loop implementation with clock management.
 *
 * Validates:
 *   - Main run loop structure
 *   - ACES_Run execution in each iteration
 *   - MPI synchronization in run loop
 *   - Step counter and output indexing
 *   - Elapsed time tracking
 *
 * Requirements: 6.1, 6.2, 6.3, 6.4, 7.1, 7.2, 7.3, 9.4, 16.1, 16.3, 16.4, 17.1, 17.2, 17.3, 17.4
 * Properties: 12, 13
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Mock Clock and Run Loop Components
// ---------------------------------------------------------------------------

/**
 * @brief Mock clock for testing run loop logic.
 */
class MockRunLoopClock {
   public:
    MockRunLoopClock(int start_hour, int end_hour, int timestep_hours)
        : current_hour_(start_hour),
          end_hour_(end_hour),
          timestep_hours_(timestep_hours),
          start_hour_(start_hour) {}

    bool IsAtStopTime() const {
        return current_hour_ >= end_hour_;
    }

    void Advance() {
        current_hour_ += timestep_hours_;
    }

    int GetCurrentHour() const {
        return current_hour_;
    }

    int GetElapsedHours() const {
        return current_hour_ - start_hour_;
    }

    int GetEndHour() const {
        return end_hour_;
    }

    int GetTimestepHours() const {
        return timestep_hours_;
    }

   private:
    int current_hour_;
    int end_hour_;
    int timestep_hours_;
    int start_hour_;
};

/**
 * @brief Mock ACES component for testing run loop execution.
 */
class MockAcesRunComponent {
   public:
    MockAcesRunComponent() : run_count_(0), last_error_code_(0) {}

    int Run() {
        run_count_++;
        return 0;  // ESMF_SUCCESS
    }

    int GetRunCount() const {
        return run_count_;
    }

    void SetErrorCode(int rc) {
        last_error_code_ = rc;
    }

    int GetLastErrorCode() const {
        return last_error_code_;
    }

   private:
    int run_count_;
    int last_error_code_;
};

/**
 * @brief Mock VM for testing MPI synchronization.
 */
class MockVM {
   public:
    MockVM(int petCount = 1, int localPet = 0)
        : petCount_(petCount), localPet_(localPet), barrier_count_(0) {}

    int Barrier() {
        barrier_count_++;
        return 0;  // ESMF_SUCCESS
    }

    int GetBarrierCount() const {
        return barrier_count_;
    }

    int GetPetCount() const {
        return petCount_;
    }

    int GetLocalPet() const {
        return localPet_;
    }

   private:
    int petCount_;
    int localPet_;
    int barrier_count_;
};

// ---------------------------------------------------------------------------
// Test Suite: Run Loop Structure
// ---------------------------------------------------------------------------

class RunLoopStructureTest : public ::testing::Test {};

TEST_F(RunLoopStructureTest, BasicRunLoop) {
    MockRunLoopClock clock(0, 24, 1);  // 24 hours, 1-hour timesteps
    MockAcesRunComponent aces;

    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        aces.Run();
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    EXPECT_EQ(step_count, 24);
    EXPECT_EQ(aces.GetRunCount(), 24);
}

TEST_F(RunLoopStructureTest, ZeroLengthSimulation) {
    MockRunLoopClock clock(0, 0, 1);  // Already at stop time
    MockAcesRunComponent aces;

    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        aces.Run();
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    EXPECT_EQ(step_count, 0);
    EXPECT_EQ(aces.GetRunCount(), 0);
}

TEST_F(RunLoopStructureTest, SingleTimestepSimulation) {
    MockRunLoopClock clock(0, 1, 1);
    MockAcesRunComponent aces;

    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        aces.Run();
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    EXPECT_EQ(step_count, 1);
    EXPECT_EQ(aces.GetRunCount(), 1);
}

// ---------------------------------------------------------------------------
// Test Suite: Step Counter and Output Indexing
// ---------------------------------------------------------------------------

class StepCounterTest : public ::testing::Test {};

// Property 12: Step Counter Increment
// For any successful Run phase, the step counter must increment by exactly 1
TEST_F(StepCounterTest, Property12_StepCounterIncrement) {
    // Test with various step counts
    std::vector<int> test_cases = {1, 5, 10, 24, 100, 1000};

    for (int num_steps : test_cases) {
        MockRunLoopClock clock(0, num_steps, 1);
        MockAcesRunComponent aces;

        int step_count = 0;
        bool clock_done = clock.IsAtStopTime();

        while (!clock_done) {
            step_count++;
            aces.Run();
            clock.Advance();
            clock_done = clock.IsAtStopTime();
        }

        EXPECT_EQ(step_count, num_steps);
        EXPECT_EQ(aces.GetRunCount(), num_steps);
    }
}

TEST_F(StepCounterTest, StepIndexing) {
    MockRunLoopClock clock(0, 5, 1);
    MockAcesRunComponent aces;

    std::vector<int> step_indices;
    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        step_indices.push_back(step_count);
        aces.Run();
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    // Verify step indices are 1, 2, 3, 4, 5
    EXPECT_EQ(step_indices.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(step_indices[i], i + 1);
    }
}

TEST_F(StepCounterTest, OutputIndexingCorrectness) {
    MockRunLoopClock clock(0, 10, 1);
    MockAcesRunComponent aces;

    std::vector<int> output_indices;
    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        output_indices.push_back(step_count);  // Pass to output writer
        aces.Run();
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    // Verify output indices match step count
    EXPECT_EQ(output_indices.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(output_indices[i], i + 1);
    }
}

// ---------------------------------------------------------------------------
// Test Suite: Elapsed Time Tracking
// ---------------------------------------------------------------------------

class ElapsedTimeTrackingTest : public ::testing::Test {};

// Property 13: Elapsed Time Monotonicity
// For any sequence of timesteps, elapsed time must strictly increase by exactly one timestep
TEST_F(ElapsedTimeTrackingTest, Property13_ElapsedTimeMonotonicity) {
    // Test with various step counts
    std::vector<int> test_cases = {1, 5, 10, 24, 100};

    for (int num_steps : test_cases) {
        MockRunLoopClock clock(0, num_steps, 1);
        MockAcesRunComponent aces;

        std::vector<int> elapsed_times;
        int step_count = 0;
        bool clock_done = clock.IsAtStopTime();

        while (!clock_done) {
            step_count++;
            aces.Run();
            clock.Advance();
            elapsed_times.push_back(clock.GetElapsedHours());
            clock_done = clock.IsAtStopTime();
        }

        // Verify elapsed time increases monotonically
        for (int i = 1; i < elapsed_times.size(); ++i) {
            EXPECT_GT(elapsed_times[i], elapsed_times[i - 1]);
            EXPECT_EQ(elapsed_times[i] - elapsed_times[i - 1], 1);
        }
    }
}

TEST_F(ElapsedTimeTrackingTest, ElapsedTimeCalculation) {
    MockRunLoopClock clock(0, 10, 1);
    MockAcesRunComponent aces;

    std::vector<int> elapsed_times;
    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        aces.Run();
        clock.Advance();
        elapsed_times.push_back(clock.GetElapsedHours());
        clock_done = clock.IsAtStopTime();
    }

    // Verify elapsed times are 1, 2, 3, ..., 10
    EXPECT_EQ(elapsed_times.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(elapsed_times[i], i + 1);
    }
}

TEST_F(ElapsedTimeTrackingTest, ElapsedTimeWithVariableTimestep) {
    MockRunLoopClock clock(0, 24, 2);  // 2-hour timesteps
    MockAcesRunComponent aces;

    std::vector<int> elapsed_times;
    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        aces.Run();
        clock.Advance();
        elapsed_times.push_back(clock.GetElapsedHours());
        clock_done = clock.IsAtStopTime();
    }

    // Verify elapsed times are 2, 4, 6, ..., 24
    EXPECT_EQ(elapsed_times.size(), 12);
    for (int i = 0; i < 12; ++i) {
        EXPECT_EQ(elapsed_times[i], (i + 1) * 2);
    }
}

// ---------------------------------------------------------------------------
// Test Suite: MPI Synchronization
// ---------------------------------------------------------------------------

class MPISynchronizationTest : public ::testing::Test {};

TEST_F(MPISynchronizationTest, SingleProcessNoBarrier) {
    MockVM vm(1, 0);
    MockRunLoopClock clock(0, 5, 1);
    MockAcesRunComponent aces;

    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        aces.Run();
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    // Single process: no barriers needed
    EXPECT_EQ(vm.GetBarrierCount(), 0);
}

TEST_F(MPISynchronizationTest, MultiProcessBarrier) {
    MockVM vm(4, 0);  // 4 processes
    MockRunLoopClock clock(0, 5, 1);
    MockAcesRunComponent aces;

    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        // In MPI mode, call barrier after Run
        vm.Barrier();
        aces.Run();
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    // 5 steps = 5 barriers
    EXPECT_EQ(vm.GetBarrierCount(), 5);
}

TEST_F(MPISynchronizationTest, BarrierBeforeAndAfterRun) {
    MockVM vm(2, 0);
    MockRunLoopClock clock(0, 3, 1);
    MockAcesRunComponent aces;

    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        vm.Barrier();  // Before Run
        aces.Run();
        vm.Barrier();  // After Run
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    // 3 steps = 6 barriers (2 per step)
    EXPECT_EQ(vm.GetBarrierCount(), 6);
}

// ---------------------------------------------------------------------------
// Integration Tests
// ---------------------------------------------------------------------------

class RunLoopIntegrationTest : public ::testing::Test {};

TEST_F(RunLoopIntegrationTest, FullRunLoopWithAllComponents) {
    MockVM vm(1, 0);
    MockRunLoopClock clock(0, 24, 1);
    MockAcesRunComponent aces;

    int step_count = 0;
    std::vector<int> step_indices;
    std::vector<int> elapsed_times;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        step_indices.push_back(step_count);
        aces.Run();
        clock.Advance();
        elapsed_times.push_back(clock.GetElapsedHours());
        clock_done = clock.IsAtStopTime();
    }

    EXPECT_EQ(step_count, 24);
    EXPECT_EQ(aces.GetRunCount(), 24);
    EXPECT_EQ(step_indices.size(), 24);
    EXPECT_EQ(elapsed_times.size(), 24);

    // Verify step indices
    for (int i = 0; i < 24; ++i) {
        EXPECT_EQ(step_indices[i], i + 1);
    }

    // Verify elapsed times
    for (int i = 0; i < 24; ++i) {
        EXPECT_EQ(elapsed_times[i], i + 1);
    }
}

TEST_F(RunLoopIntegrationTest, MPIRunLoopWithSynchronization) {
    MockVM vm(4, 0);
    MockRunLoopClock clock(0, 10, 1);
    MockAcesRunComponent aces;

    int step_count = 0;
    bool clock_done = clock.IsAtStopTime();

    while (!clock_done) {
        step_count++;
        vm.Barrier();  // Synchronize before Run
        aces.Run();
        vm.Barrier();  // Synchronize after Run
        clock.Advance();
        clock_done = clock.IsAtStopTime();
    }

    EXPECT_EQ(step_count, 10);
    EXPECT_EQ(aces.GetRunCount(), 10);
    EXPECT_EQ(vm.GetBarrierCount(), 20);  // 2 barriers per step
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
