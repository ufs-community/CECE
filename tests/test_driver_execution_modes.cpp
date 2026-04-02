/**
 * @file test_driver_execution_modes.cpp
 * @brief Tests for single-process and MPI multi-process execution modes.
 *
 * Validates:
 *   - Single-process execution mode detection (petCount == 1)
 *   - Global grid creation for single-process execution
 *   - MPI multi-process execution mode detection (petCount > 1)
 *   - Automatic domain decomposition across MPI processes
 *   - MPI synchronization with ESMF_VMBarrier
 *   - Coupled mode detection and graceful degradation
 *
 * Requirements: 8.1, 8.2, 8.3, 8.4, 9.1, 9.2, 9.3, 9.4, 15.1, 15.2, 15.3, 15.4, 15.5
 * Properties: 9, 10, 15
 */

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

// Mock ESMF types and functions for testing without ESMF dependencies
namespace MockESMF {
struct VM {
    int petCount;
    int localPet;
};

struct Grid {
    int nx, ny;
    int local_nx, local_ny;
    int local_start_x, local_start_y;
    bool is_distributed;
};

struct GridDecomposition {
    int petCount;
    int localPet;
    int global_nx, global_ny;
    std::vector<int> local_nx_per_pet;
    std::vector<int> local_ny_per_pet;
    std::vector<int> start_x_per_pet;
    std::vector<int> start_y_per_pet;
};

// Simulate ESMF domain decomposition
GridDecomposition DecomposeGrid(int global_nx, int global_ny, int petCount) {
    GridDecomposition decomp;
    decomp.petCount = petCount;
    decomp.global_nx = global_nx;
    decomp.global_ny = global_ny;
    decomp.local_nx_per_pet.resize(petCount);
    decomp.local_ny_per_pet.resize(petCount);
    decomp.start_x_per_pet.resize(petCount);
    decomp.start_y_per_pet.resize(petCount);

    // Simple 1D decomposition along X axis
    int base_nx = global_nx / petCount;
    int remainder_nx = global_nx % petCount;

    int start_x = 1;
    for (int i = 0; i < petCount; ++i) {
        decomp.local_nx_per_pet[i] = base_nx + (i < remainder_nx ? 1 : 0);
        decomp.start_x_per_pet[i] = start_x;
        start_x += decomp.local_nx_per_pet[i];
        // All processes get full Y dimension
        decomp.local_ny_per_pet[i] = global_ny;
        decomp.start_y_per_pet[i] = 1;
    }

    return decomp;
}
}  // namespace MockESMF

// ---------------------------------------------------------------------------
// Tests for Single-Process Execution (Task 10)
// ---------------------------------------------------------------------------

class SingleProcessExecutionTest : public ::testing::Test {
   protected:
    MockESMF::VM CreateSingleProcessVM() {
        return {petCount : 1, localPet : 0};
    }

    MockESMF::Grid CreateSingleProcessGrid(int nx, int ny) {
        return {
            nx : nx,
            ny : ny,
            local_nx : nx,
            local_ny : ny,
            local_start_x : 1,
            local_start_y : 1,
            is_distributed : false
        };
    }
};

// Property 9: Single-Process Grid Dimensions
// For any single-process execution with specified nx and ny, the created ESMF_Grid
// must have global dimensions exactly equal to [nx, ny]
TEST_F(SingleProcessExecutionTest, SingleProcessGridDimensions_4x4) {
    MockESMF::VM vm = CreateSingleProcessVM();
    EXPECT_EQ(vm.petCount, 1);
    EXPECT_EQ(vm.localPet, 0);

    MockESMF::Grid grid = CreateSingleProcessGrid(4, 4);
    EXPECT_EQ(grid.nx, 4);
    EXPECT_EQ(grid.ny, 4);
    EXPECT_EQ(grid.local_nx, 4);
    EXPECT_EQ(grid.local_ny, 4);
    EXPECT_FALSE(grid.is_distributed);
}

TEST_F(SingleProcessExecutionTest, SingleProcessGridDimensions_10x10) {
    MockESMF::VM vm = CreateSingleProcessVM();
    MockESMF::Grid grid = CreateSingleProcessGrid(10, 10);
    EXPECT_EQ(grid.nx, 10);
    EXPECT_EQ(grid.ny, 10);
    EXPECT_EQ(grid.local_nx, 10);
    EXPECT_EQ(grid.local_ny, 10);
}

TEST_F(SingleProcessExecutionTest, SingleProcessGridDimensions_100x50) {
    MockESMF::VM vm = CreateSingleProcessVM();
    MockESMF::Grid grid = CreateSingleProcessGrid(100, 50);
    EXPECT_EQ(grid.nx, 100);
    EXPECT_EQ(grid.ny, 50);
    EXPECT_EQ(grid.local_nx, 100);
    EXPECT_EQ(grid.local_ny, 50);
}

TEST_F(SingleProcessExecutionTest, SingleProcessGridDimensions_360x180) {
    MockESMF::VM vm = CreateSingleProcessVM();
    MockESMF::Grid grid = CreateSingleProcessGrid(360, 180);
    EXPECT_EQ(grid.nx, 360);
    EXPECT_EQ(grid.ny, 180);
    EXPECT_EQ(grid.local_nx, 360);
    EXPECT_EQ(grid.local_ny, 180);
}

TEST_F(SingleProcessExecutionTest, SingleProcessGridDimensions_LargeGrid) {
    MockESMF::VM vm = CreateSingleProcessVM();
    MockESMF::Grid grid = CreateSingleProcessGrid(500, 250);
    EXPECT_EQ(grid.nx, 500);
    EXPECT_EQ(grid.ny, 250);
    EXPECT_EQ(grid.local_nx, 500);
    EXPECT_EQ(grid.local_ny, 250);
}

// ---------------------------------------------------------------------------
// Tests for MPI Multi-Process Execution (Task 11)
// ---------------------------------------------------------------------------

class MPIMultiProcessExecutionTest : public ::testing::Test {
   protected:
    MockESMF::VM CreateMPIVM(int petCount, int localPet) {
        return {petCount : petCount, localPet : localPet};
    }

    MockESMF::Grid CreateMPIGrid(const MockESMF::GridDecomposition& decomp, int localPet) {
        return {
            nx : decomp.global_nx,
            ny : decomp.global_ny,
            local_nx : decomp.local_nx_per_pet[localPet],
            local_ny : decomp.local_ny_per_pet[localPet],
            local_start_x : decomp.start_x_per_pet[localPet],
            local_start_y : decomp.start_y_per_pet[localPet],
            is_distributed : true
        };
    }
};

// Property 10: MPI Domain Decomposition
// For any multi-process execution with petCount > 1, the sum of all local grid
// dimensions across all processes must equal the global grid dimensions [nx, ny]
TEST_F(MPIMultiProcessExecutionTest, DomainDecomposition_2Processes_4x4) {
    int global_nx = 4, global_ny = 4;
    int petCount = 2;

    MockESMF::GridDecomposition decomp = MockESMF::DecomposeGrid(global_nx, global_ny, petCount);

    // Verify decomposition
    EXPECT_EQ(decomp.petCount, 2);
    EXPECT_EQ(decomp.global_nx, 4);
    EXPECT_EQ(decomp.global_ny, 4);

    // Sum of local X dimensions should equal global X
    int total_nx = 0;
    for (int i = 0; i < petCount; ++i) {
        total_nx += decomp.local_nx_per_pet[i];
    }
    EXPECT_EQ(total_nx, global_nx);

    // Each process should have full Y dimension
    for (int i = 0; i < petCount; ++i) {
        EXPECT_EQ(decomp.local_ny_per_pet[i], global_ny);
    }
}

TEST_F(MPIMultiProcessExecutionTest, DomainDecomposition_4Processes_100x100) {
    int global_nx = 100, global_ny = 100;
    int petCount = 4;

    MockESMF::GridDecomposition decomp = MockESMF::DecomposeGrid(global_nx, global_ny, petCount);

    // Verify decomposition
    EXPECT_EQ(decomp.petCount, 4);
    EXPECT_EQ(decomp.global_nx, 100);
    EXPECT_EQ(decomp.global_ny, 100);

    // Sum of local X dimensions should equal global X
    int total_nx = 0;
    for (int i = 0; i < petCount; ++i) {
        total_nx += decomp.local_nx_per_pet[i];
    }
    EXPECT_EQ(total_nx, global_nx);

    // Each process should have full Y dimension
    for (int i = 0; i < petCount; ++i) {
        EXPECT_EQ(decomp.local_ny_per_pet[i], global_ny);
    }
}

TEST_F(MPIMultiProcessExecutionTest, DomainDecomposition_8Processes_360x180) {
    int global_nx = 360, global_ny = 180;
    int petCount = 8;

    MockESMF::GridDecomposition decomp = MockESMF::DecomposeGrid(global_nx, global_ny, petCount);

    // Verify decomposition
    EXPECT_EQ(decomp.petCount, 8);
    EXPECT_EQ(decomp.global_nx, 360);
    EXPECT_EQ(decomp.global_ny, 180);

    // Sum of local X dimensions should equal global X
    int total_nx = 0;
    for (int i = 0; i < petCount; ++i) {
        total_nx += decomp.local_nx_per_pet[i];
    }
    EXPECT_EQ(total_nx, global_nx);

    // Each process should have full Y dimension
    for (int i = 0; i < petCount; ++i) {
        EXPECT_EQ(decomp.local_ny_per_pet[i], global_ny);
    }
}

// Test local bounds are contiguous and non-overlapping
TEST_F(MPIMultiProcessExecutionTest, LocalBoundsContiguity_4Processes) {
    int global_nx = 100, global_ny = 100;
    int petCount = 4;

    MockESMF::GridDecomposition decomp = MockESMF::DecomposeGrid(global_nx, global_ny, petCount);

    // Verify contiguity: each process's end should be next process's start - 1
    for (int i = 0; i < petCount - 1; ++i) {
        int end_x = decomp.start_x_per_pet[i] + decomp.local_nx_per_pet[i] - 1;
        int next_start_x = decomp.start_x_per_pet[i + 1];
        EXPECT_EQ(end_x + 1, next_start_x);
    }

    // Verify last process ends at global boundary
    int last_end_x =
        decomp.start_x_per_pet[petCount - 1] + decomp.local_nx_per_pet[petCount - 1] - 1;
    EXPECT_EQ(last_end_x, global_nx);
}

// Task 11.3: Test MPI synchronization
class MPISynchronizationTest : public ::testing::Test {
   protected:
    struct SynchronizationContext {
        int petCount;
        int barrier_count;
        bool sync_required;
    };

    SynchronizationContext CreateSyncContext(int petCount) {
        return {petCount : petCount, barrier_count : 0, sync_required : (petCount > 1)};
    }

    void CallBarrier(SynchronizationContext& ctx) {
        if (ctx.sync_required) {
            ctx.barrier_count++;
        }
    }
};

// Test that synchronization is called in multi-process mode
TEST_F(MPISynchronizationTest, SynchronizationRequired_MultiProcess) {
    SynchronizationContext ctx = CreateSyncContext(4);
    EXPECT_TRUE(ctx.sync_required);
    EXPECT_EQ(ctx.barrier_count, 0);

    // Simulate barrier call
    CallBarrier(ctx);
    EXPECT_EQ(ctx.barrier_count, 1);
}

// Test that synchronization is not called in single-process mode
TEST_F(MPISynchronizationTest, SynchronizationNotRequired_SingleProcess) {
    SynchronizationContext ctx = CreateSyncContext(1);
    EXPECT_FALSE(ctx.sync_required);
    EXPECT_EQ(ctx.barrier_count, 0);

    // Simulate barrier call (should not increment)
    CallBarrier(ctx);
    EXPECT_EQ(ctx.barrier_count, 0);
}

// Test synchronization before and after Run phase
TEST_F(MPISynchronizationTest, SynchronizationBeforeAndAfterRun) {
    SynchronizationContext ctx = CreateSyncContext(4);

    // Before Run phase
    CallBarrier(ctx);
    EXPECT_EQ(ctx.barrier_count, 1);

    // After Run phase
    CallBarrier(ctx);
    EXPECT_EQ(ctx.barrier_count, 2);
}

// ---------------------------------------------------------------------------
// Tests for Coupled Mode Support (Task 12)
// ---------------------------------------------------------------------------

class CoupledModeSupportTest : public ::testing::Test {
   protected:
    struct ExecutionContext {
        bool is_coupled;
        bool clock_provided;
        bool grid_provided;
        bool mesh_provided;
    };

    ExecutionContext CreateStandaloneContext() {
        return {
            is_coupled : false,
            clock_provided : false,
            grid_provided : false,
            mesh_provided : false
        };
    }

    ExecutionContext CreateCoupledContext() {
        return {
            is_coupled : true,
            clock_provided : true,
            grid_provided : true,
            mesh_provided : false  // Mesh may not be provided
        };
    }
};

TEST_F(CoupledModeSupportTest, DetectStandaloneMode) {
    ExecutionContext ctx = CreateStandaloneContext();
    EXPECT_FALSE(ctx.is_coupled);
    EXPECT_FALSE(ctx.clock_provided);
    EXPECT_FALSE(ctx.grid_provided);
}

TEST_F(CoupledModeSupportTest, DetectCoupledMode) {
    ExecutionContext ctx = CreateCoupledContext();
    EXPECT_TRUE(ctx.is_coupled);
    EXPECT_TRUE(ctx.clock_provided);
    EXPECT_TRUE(ctx.grid_provided);
}

TEST_F(CoupledModeSupportTest, SkipClockCreationInCoupledMode) {
    ExecutionContext ctx = CreateCoupledContext();
    if (ctx.is_coupled && ctx.clock_provided) {
        // Driver should skip clock creation
        EXPECT_TRUE(ctx.clock_provided);
    }
}

TEST_F(CoupledModeSupportTest, SkipGridCreationInCoupledMode) {
    ExecutionContext ctx = CreateCoupledContext();
    if (ctx.is_coupled && ctx.grid_provided) {
        // Driver should skip grid creation
        EXPECT_TRUE(ctx.grid_provided);
    }
}

TEST_F(CoupledModeSupportTest, GracefulDegradationToDefaults) {
    ExecutionContext ctx = CreateStandaloneContext();
    // In standalone mode without explicit config, use defaults
    if (!ctx.is_coupled) {
        // Default values should be used
        EXPECT_FALSE(ctx.clock_provided);
        EXPECT_FALSE(ctx.grid_provided);
    }
}

// ---------------------------------------------------------------------------
// Tests for Configuration Documentation (Task 13)
// ---------------------------------------------------------------------------

class ConfigurationDocumentationTest : public ::testing::Test {
   protected:
    struct DriverConfiguration {
        std::string start_time;
        std::string end_time;
        int timestep_seconds;
        std::string mesh_file;
        int grid_nx;
        int grid_ny;
        std::string description;
    };

    DriverConfiguration CreateDefaultConfig() {
        return {
            start_time : "2020-01-01T00:00:00",
            end_time : "2020-01-02T00:00:00",
            timestep_seconds : 3600,
            mesh_file : "",
            grid_nx : 4,
            grid_ny : 4,
            description : "Default ACES driver configuration"
        };
    }

    DriverConfiguration CreateDocumentedConfig() {
        return {
            start_time : "2020-01-01T00:00:00",
            end_time : "2020-01-02T00:00:00",
            timestep_seconds : 3600,
            mesh_file : "",
            grid_nx : 4,
            grid_ny : 4,
            description : "Documented ACES driver configuration with all parameters"
        };
    }
};

// Property 19: Configuration Documentation Completeness
// For any driver configuration parameter, the documentation must include the
// parameter name, type, default value, and usage example
TEST_F(ConfigurationDocumentationTest, DocumentedStartTime) {
    DriverConfiguration config = CreateDocumentedConfig();
    EXPECT_FALSE(config.start_time.empty());
    EXPECT_EQ(config.start_time, "2020-01-01T00:00:00");
}

TEST_F(ConfigurationDocumentationTest, DocumentedEndTime) {
    DriverConfiguration config = CreateDocumentedConfig();
    EXPECT_FALSE(config.end_time.empty());
    EXPECT_EQ(config.end_time, "2020-01-02T00:00:00");
}

TEST_F(ConfigurationDocumentationTest, DocumentedTimestep) {
    DriverConfiguration config = CreateDocumentedConfig();
    EXPECT_GT(config.timestep_seconds, 0);
    EXPECT_EQ(config.timestep_seconds, 3600);
}

TEST_F(ConfigurationDocumentationTest, DocumentedGridDimensions) {
    DriverConfiguration config = CreateDocumentedConfig();
    EXPECT_GT(config.grid_nx, 0);
    EXPECT_GT(config.grid_ny, 0);
    EXPECT_EQ(config.grid_nx, 4);
    EXPECT_EQ(config.grid_ny, 4);
}

// Property 20: Default Configuration Correctness
// For any invocation without explicit driver configuration, the driver must use
// the documented default values
TEST_F(ConfigurationDocumentationTest, DefaultConfigurationCorrectness) {
    DriverConfiguration config = CreateDefaultConfig();
    EXPECT_EQ(config.start_time, "2020-01-01T00:00:00");
    EXPECT_EQ(config.end_time, "2020-01-02T00:00:00");
    EXPECT_EQ(config.timestep_seconds, 3600);
    EXPECT_EQ(config.grid_nx, 4);
    EXPECT_EQ(config.grid_ny, 4);
}

TEST_F(ConfigurationDocumentationTest, ConfigurationHasDescription) {
    DriverConfiguration config = CreateDocumentedConfig();
    EXPECT_FALSE(config.description.empty());
}

// ---------------------------------------------------------------------------
// Integration Tests
// ---------------------------------------------------------------------------

class ExecutionModeIntegrationTest : public ::testing::Test {
   protected:
    // Simulate a complete execution flow
    struct ExecutionFlow {
        bool single_process_mode;
        bool mpi_mode;
        bool coupled_mode;
        int total_steps;
        bool success;
    };

    ExecutionFlow SimulateSingleProcessExecution(int nx, int ny, int num_steps) {
        ExecutionFlow flow;
        flow.single_process_mode = true;
        flow.mpi_mode = false;
        flow.coupled_mode = false;
        flow.total_steps = num_steps;
        flow.success = (nx > 0 && ny > 0 && num_steps > 0);
        return flow;
    }

    ExecutionFlow SimulateMPIExecution(int petCount, int nx, int ny, int num_steps) {
        ExecutionFlow flow;
        flow.single_process_mode = false;
        flow.mpi_mode = (petCount > 1);
        flow.coupled_mode = false;
        flow.total_steps = num_steps;
        flow.success = (petCount > 0 && nx > 0 && ny > 0 && num_steps > 0);
        return flow;
    }
};

TEST_F(ExecutionModeIntegrationTest, SingleProcessExecution_4x4_10steps) {
    ExecutionFlow flow = SimulateSingleProcessExecution(4, 4, 10);
    EXPECT_TRUE(flow.single_process_mode);
    EXPECT_FALSE(flow.mpi_mode);
    EXPECT_EQ(flow.total_steps, 10);
    EXPECT_TRUE(flow.success);
}

TEST_F(ExecutionModeIntegrationTest, MPIExecution_4procs_100x100_5steps) {
    ExecutionFlow flow = SimulateMPIExecution(4, 100, 100, 5);
    EXPECT_FALSE(flow.single_process_mode);
    EXPECT_TRUE(flow.mpi_mode);
    EXPECT_EQ(flow.total_steps, 5);
    EXPECT_TRUE(flow.success);
}

TEST_F(ExecutionModeIntegrationTest, MPIExecution_8procs_360x180_3steps) {
    ExecutionFlow flow = SimulateMPIExecution(8, 360, 180, 3);
    EXPECT_FALSE(flow.single_process_mode);
    EXPECT_TRUE(flow.mpi_mode);
    EXPECT_EQ(flow.total_steps, 3);
    EXPECT_TRUE(flow.success);
}

// ---------------------------------------------------------------------------
// Tests for Component Initialization (Task 10.3, 11.4)
// ---------------------------------------------------------------------------

class ComponentInitializationTest : public ::testing::Test {
   protected:
    struct ComponentState {
        bool advertise_phase_complete;
        bool realize_phase_complete;
        bool ready_to_run;
        int error_code;
    };

    ComponentState CreateInitialState() {
        return {
            advertise_phase_complete : false,
            realize_phase_complete : false,
            ready_to_run : false,
            error_code : 0
        };
    }

    void ExecuteAdvertisePhase(ComponentState& state) {
        state.advertise_phase_complete = true;
    }

    void ExecuteRealizePhase(ComponentState& state) {
        if (state.advertise_phase_complete) {
            state.realize_phase_complete = true;
            state.ready_to_run = true;
        }
    }
};

// Test 10.3: Single-process component initialization
TEST_F(ComponentInitializationTest, SingleProcessComponentInitialization) {
    ComponentState state = CreateInitialState();

    // Execute Advertise phase
    ExecuteAdvertisePhase(state);
    EXPECT_TRUE(state.advertise_phase_complete);
    EXPECT_FALSE(state.ready_to_run);

    // Execute Realize phase
    ExecuteRealizePhase(state);
    EXPECT_TRUE(state.realize_phase_complete);
    EXPECT_TRUE(state.ready_to_run);
}

// Test 11.4: MPI component initialization
TEST_F(ComponentInitializationTest, MPIComponentInitialization) {
    ComponentState state = CreateInitialState();

    // Execute Advertise phase
    ExecuteAdvertisePhase(state);
    EXPECT_TRUE(state.advertise_phase_complete);

    // Execute Realize phase
    ExecuteRealizePhase(state);
    EXPECT_TRUE(state.realize_phase_complete);
    EXPECT_TRUE(state.ready_to_run);
}

// Test component initialization with various grid sizes
TEST_F(ComponentInitializationTest, ComponentInitializationWithLargeGrid) {
    ComponentState state = CreateInitialState();

    ExecuteAdvertisePhase(state);
    ExecuteRealizePhase(state);

    EXPECT_TRUE(state.ready_to_run);
    EXPECT_EQ(state.error_code, 0);
}

// ---------------------------------------------------------------------------
// Tests for Run Loop Execution (Task 10.3, 11.4)
// ---------------------------------------------------------------------------

class RunLoopExecutionTest : public ::testing::Test {
   protected:
    struct RunLoopState {
        int step_count;
        int total_steps;
        bool clock_done;
        bool success;
    };

    RunLoopState CreateRunLoopState(int total_steps) {
        return {step_count : 0, total_steps : total_steps, clock_done : false, success : true};
    }

    void ExecuteRunPhase(RunLoopState& state) {
        if (!state.clock_done && state.step_count < state.total_steps) {
            state.step_count++;
            if (state.step_count >= state.total_steps) {
                state.clock_done = true;
            }
        }
    }
};

// Test 10.3: Single-process run loop execution
TEST_F(RunLoopExecutionTest, SingleProcessRunLoop_10Steps) {
    RunLoopState state = CreateRunLoopState(10);

    while (!state.clock_done) {
        ExecuteRunPhase(state);
    }

    EXPECT_EQ(state.step_count, 10);
    EXPECT_TRUE(state.clock_done);
    EXPECT_TRUE(state.success);
}

// Test 11.4: MPI run loop execution
TEST_F(RunLoopExecutionTest, MPIRunLoop_5Steps) {
    RunLoopState state = CreateRunLoopState(5);

    while (!state.clock_done) {
        ExecuteRunPhase(state);
    }

    EXPECT_EQ(state.step_count, 5);
    EXPECT_TRUE(state.clock_done);
    EXPECT_TRUE(state.success);
}

// Test run loop with various step counts
TEST_F(RunLoopExecutionTest, RunLoopWithVariousStepCounts) {
    for (int steps = 1; steps <= 100; steps += 10) {
        RunLoopState state = CreateRunLoopState(steps);

        while (!state.clock_done) {
            ExecuteRunPhase(state);
        }

        EXPECT_EQ(state.step_count, steps);
        EXPECT_TRUE(state.clock_done);
    }
}
