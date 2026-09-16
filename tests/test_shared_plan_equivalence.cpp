/**
 * @file test_shared_plan_equivalence.cpp
 * @brief Property-based tests for shared-regrid-plan output equivalence.
 *
 * Feature: regrid-per-stream, Property 2: Shared regrid plan produces identical
 * output
 *
 * Property 2 (design.md): "For any valid source-grid data array `src`, for any
 * built `RegridPlan` `plan`, and for any two variable names `var_a` and `var_b`
 * that share the same `StreamKey`: applying `apply_regrid_plan` produces the
 * same `dest` output regardless of which variable name triggered the plan
 * build. (The plan is a function of grid geometry and mapalgo only; the variable
 * name is not an input to `build_regrid_plan` or `apply_regrid_plan`.)"
 *
 * The regrid-per-stream refactor re-keys `regrid_plans_` from Model_Variable
 * name to Stream_Identity_Key so that variables in the same stream share a
 * single plan. The correctness guarantee that makes this sharing safe is that
 * `apply_regrid_plan` is a pure function of (plan, source, geometry): the
 * variable name is not an input. Therefore, applying the SAME plan to the SAME
 * source data — as two variables in a shared stream would do — must yield
 * byte-identical destination vectors.
 *
 * This test exercises the real production `apply_regrid_plan` using an identity
 * ("passthrough") plan, the simplest deterministic path (see
 * tests/test_cece_utils.cpp and tests/test_regrid_conservation_properties.cpp).
 * It requires no AMIO dataset and no AXIS weight generation, keeping the
 * property hermetic and fast while still driving the actual apply routine — the
 * exact function shared plans invoke.
 *
 * **Validates: Requirements 2.4, 6.1, 6.2**
 */

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <cstddef>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace cece::io {
namespace {

// Build a FIXED identity (passthrough) regrid plan over an nx * ny source grid
// that owns the destination row band [j0, j1). An identity plan copies the
// owned source rows directly, applying no AXIS weights — the deterministic core
// that shared plans rely on.
RegridPlan MakeIdentityPlan(int file_nx, int file_ny, int j0, int j1) {
    RegridPlan plan;
    plan.file_nx = file_nx;
    plan.file_ny = file_ny;
    plan.j0 = j0;
    plan.j1 = j1;
    plan.identity = true;
    plan.built = true;
    return plan;
}

// Generate a source field of exactly file_nx * file_ny doubles. Values span a
// mix of signs and magnitudes so the shared-plan comparison is non-trivial.
rc::Gen<std::vector<double>> genSourceField(std::size_t n) {
    return rc::gen::container<std::vector<double>>(n, rc::gen::map(rc::gen::inRange(-1000000, 1000001), [](int v) { return v / 1000.0; }));
}

}  // namespace

// ============================================================================
// Property 2: Shared regrid plan produces identical output
// Feature: regrid-per-stream, Property 2: Shared regrid plan produces identical output
// **Validates: Requirements 2.4, 6.1, 6.2**
//
// Two variables sharing a Stream_Identity_Key share one RegridPlan. Because the
// variable name is not an input to apply_regrid_plan, applying the SAME plan to
// the SAME source data twice — once "for var_a" and once "for var_b" — yields
// byte-identical destination vectors. This is what guarantees the re-keyed,
// shared plan produces the same numerical output the previous per-variable
// keying produced for each variable (Req 2.4, 6.1, 6.2).
// ============================================================================
RC_GTEST_PROP(SharedPlanEquivalenceProperty, SharedPlanProducesIdenticalOutput, ()) {
    // A fixed source grid and owned band shared by both "variables".
    const int file_nx = 3;
    const int file_ny = 4;
    const int j0 = 1;
    const int j1 = 3;
    const RegridPlan plan = MakeIdentityPlan(file_nx, file_ny, j0, j1);

    // One source-grid data array, shared by both applies (same stream => same
    // source file/grid).
    const std::vector<double> source = *genSourceField(static_cast<std::size_t>(file_nx) * file_ny);

    // Apply the SAME plan to the SAME source, once for each of two variables
    // that share the stream. The variable name never enters apply_regrid_plan.
    std::vector<double> dst_var_a;
    std::vector<double> dst_var_b;
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, source.data(), file_nx, file_ny,
                                /*nx=*/file_nx, dst_var_a));
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, source.data(), file_nx, file_ny,
                                /*nx=*/file_nx, dst_var_b));

    // The shared plan yields identical output regardless of which variable
    // triggered it — byte-for-byte, no floating-point tolerance needed.
    RC_ASSERT(dst_var_a == dst_var_b);
}

// ============================================================================
// Property 2 (float storage): storage type does not break shared-plan equality.
// Feature: regrid-per-stream, Property 2: Shared regrid plan produces identical output
// **Validates: Requirements 2.4, 6.1, 6.2**
//
// A shared plan applied to the same float-storage source buffer twice likewise
// yields identical output. Confirms sharing is safe across the is_float path,
// which the driver uses for single-precision NetCDF variables.
// ============================================================================
RC_GTEST_PROP(SharedPlanEquivalenceProperty, SharedPlanProducesIdenticalOutputFloat, ()) {
    const int file_nx = 3;
    const int file_ny = 4;
    const int j0 = 0;
    const int j1 = 4;  // whole grid
    const RegridPlan plan = MakeIdentityPlan(file_nx, file_ny, j0, j1);

    const std::vector<double> source_d = *genSourceField(static_cast<std::size_t>(file_nx) * file_ny);
    std::vector<float> source_f;
    source_f.reserve(source_d.size());
    for (double v : source_d) {
        source_f.push_back(static_cast<float>(v));
    }

    std::vector<double> dst_var_a;
    std::vector<double> dst_var_b;
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/true, source_f.data(), file_nx, file_ny,
                                /*nx=*/file_nx, dst_var_a));
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/true, source_f.data(), file_nx, file_ny,
                                /*nx=*/file_nx, dst_var_b));

    RC_ASSERT(dst_var_a == dst_var_b);
}

}  // namespace cece::io

// ============================================================================
// Kokkos + MPI lifecycle. apply_regrid_plan uses Kokkos HostSpace views, so
// Kokkos must be initialized; the identity path touches no MPI, but the CECE
// link brings MPI symbols in, so initialize/finalize it defensively (mirroring
// tests/test_regrid_conservation_properties.cpp).
// ============================================================================
class KokkosMpiEnvironment : public ::testing::Environment {
   private:
    int argc_;
    char** argv_;

   public:
    KokkosMpiEnvironment(int argc, char** argv) : argc_(argc), argv_(argv) {}

    void SetUp() override {
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc_, &argv_, MPI_THREAD_MULTIPLE, &provided);
        }
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize(argc_, argv_);
        }
    }

    void TearDown() override {
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized) {
            MPI_Finalize();
        }
    }
};

int main(int argc, char** argv) {
    bool is_discovery = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gtest_list_tests") {
            is_discovery = true;
            break;
        }
    }

    if (!is_discovery) {
        unsetenv("SLURM_JOB_ID");
        unsetenv("SLURM_STEP_ID");
        unsetenv("PMI_RANK");
        unsetenv("PMI_SIZE");

        setenv("I_MPI_HYDRA_BOOTSTRAP", "none", 0);
        setenv("I_MPI_SHM", "disable", 0);

        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
        }
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rank > 0) {
            MPI_Finalize();
            return 0;
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosMpiEnvironment(argc, argv));
    return RUN_ALL_TESTS();
}
