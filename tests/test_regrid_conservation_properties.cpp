/**
 * @file test_regrid_conservation_properties.cpp
 * @brief Property-based tests for cece::io::apply_regrid_plan conservation.
 *
 * Feature: driver-io-regrid-perf, Property 9: Regrid apply conserves its
 * aggregate
 *
 * Property 9 (design.md): "For any source field and a fixed regrid plan,
 * applying the plan produces a destination field whose conservative aggregate
 * (mass/sum under the plan's weights) is unchanged relative to the
 * pre-optimization apply — the caching layer does not alter the regrid
 * arithmetic."
 *
 * The driver-io-regrid-perf work (task 1) only removed `[DEBUG REGRID]`
 * diagnostics and their MPI reductions from the regrid apply path; it did NOT
 * touch the regrid math. This suite validates the invariant that survives that
 * change, at the `apply_regrid_plan` boundary, using a FIXED plan and randomly
 * generated source fields:
 *
 *   - Determinism: applying the same plan to the same source twice yields a
 *     destination that is identical element-for-element. A deterministic,
 *     side-effect-free apply is exactly what the caching layer relies on when
 *     it reuses a previously computed slice.
 *
 *   - Conservative aggregate stability: for an identity ("passthrough") plan,
 *     the destination is exactly the selected source cells, so the destination
 *     aggregate (sum over the owned band) equals the aggregate computed
 *     directly from the source under that plan. This is the aggregate the
 *     pre-optimization apply produced, so the apply preserves it.
 *
 * The identity plan is the simplest deterministic path through
 * apply_regrid_plan (see tests/test_cece_utils.cpp,
 * IdentityRegridPlanCopiesTheOwnedRowsExactly, for how identity plans are
 * constructed and what they must copy). It requires no AMIO dataset and no
 * AXIS weight generation, keeping the property test hermetic and fast while
 * still exercising the real production apply routine.
 *
 * **Validates: Requirements 4.1, 4.3, 5.1**
 */

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <cstddef>
#include <numeric>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace cece::io {
namespace {

// Build a FIXED identity (passthrough) regrid plan over an nx * ny source grid
// that owns the destination row band [j0, j1). An identity plan copies the
// owned source rows directly, applying no AXIS weights, so its "conservative
// aggregate" is simply the sum of the owned source cells — the exact quantity
// the pre-optimization apply produced.
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
// mix of signs and magnitudes so the aggregate is a non-trivial cancellation,
// not just a monotone sum.
rc::Gen<std::vector<double>> genSourceField(std::size_t n) {
    return rc::gen::container<std::vector<double>>(n, rc::gen::map(rc::gen::inRange(-1000000, 1000001), [](int v) { return v / 1000.0; }));
}

// The aggregate the pre-optimization apply produced for an identity plan: the
// sum of the source cells in the owned band [j0, j1), row-major over file_nx.
double ExpectedBandAggregate(const std::vector<double>& source, int file_nx, int j0, int j1) {
    double sum = 0.0;
    for (int j = j0; j < j1; ++j) {
        for (int i = 0; i < file_nx; ++i) {
            sum += source[static_cast<std::size_t>(j) * file_nx + i];
        }
    }
    return sum;
}

}  // namespace

// ============================================================================
// Property 9a: Deterministic apply
// Feature: driver-io-regrid-perf, Property 9: Regrid apply conserves its aggregate
// **Validates: Requirements 4.1, 4.3, 5.1**
//
// apply_regrid_plan is deterministic and side-effect-free: applying the same
// fixed plan to the same source field twice yields a destination that is
// identical element-for-element. This determinism is what lets the caching
// layer reuse a slice without changing results.
// ============================================================================
RC_GTEST_PROP(RegridConservationProperty, Property9_DeterministicApply, ()) {
    // A fixed source grid and a fixed owned band for the whole property.
    const int file_nx = 3;
    const int file_ny = 4;
    const int j0 = 1;
    const int j1 = 3;
    const RegridPlan plan = MakeIdentityPlan(file_nx, file_ny, j0, j1);

    const std::vector<double> source = *genSourceField(static_cast<std::size_t>(file_nx) * file_ny);

    std::vector<double> dst_a;
    std::vector<double> dst_b;
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, source.data(), file_nx, file_ny,
                                /*nx=*/file_nx, dst_a));
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, source.data(), file_nx, file_ny,
                                /*nx=*/file_nx, dst_b));

    RC_ASSERT(dst_a == dst_b);
}

// ============================================================================
// Property 9b: Conservative aggregate stability (identity plan)
// Feature: driver-io-regrid-perf, Property 9: Regrid apply conserves its aggregate
// **Validates: Requirements 4.1, 4.3, 5.1**
//
// For a fixed identity plan and any source field, the destination aggregate
// (sum over the owned destination band) equals the aggregate computed directly
// from the source under that plan (sum of the owned source rows). This is the
// aggregate the pre-optimization apply produced, so the apply — unchanged by
// the caching layer — preserves it exactly.
// ============================================================================
RC_GTEST_PROP(RegridConservationProperty, Property9_IdentityAggregateStable, ()) {
    const int file_nx = 3;
    const int file_ny = 4;
    const int j0 = 1;
    const int j1 = 3;
    const RegridPlan plan = MakeIdentityPlan(file_nx, file_ny, j0, j1);

    const std::vector<double> source = *genSourceField(static_cast<std::size_t>(file_nx) * file_ny);

    std::vector<double> dst;
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/0, /*is_float=*/false, source.data(), file_nx, file_ny,
                                /*nx=*/file_nx, dst));

    // Destination must be exactly the selected source cells (element-for-element).
    const int nband = j1 - j0;
    RC_ASSERT(dst.size() == static_cast<std::size_t>(file_nx) * nband);
    for (int local_j = 0; local_j < nband; ++local_j) {
        for (int i = 0; i < file_nx; ++i) {
            const std::size_t src_idx = static_cast<std::size_t>(j0 + local_j) * file_nx + i;
            const std::size_t dst_idx = static_cast<std::size_t>(local_j) * file_nx + i;
            RC_ASSERT(dst[dst_idx] == source[src_idx]);
        }
    }

    // The aggregate over the destination band equals the aggregate computed
    // directly from the source under this plan. Because the identity apply is a
    // pure copy, this holds bit-for-bit (no floating-point tolerance needed).
    const double dst_aggregate = std::accumulate(dst.begin(), dst.end(), 0.0);
    const double expected_aggregate = ExpectedBandAggregate(source, file_nx, j0, j1);
    RC_ASSERT(dst_aggregate == expected_aggregate);
}

// ============================================================================
// Property 9c: Aggregate invariant across time offsets / float vs double
// Feature: driver-io-regrid-perf, Property 9: Regrid apply conserves its aggregate
// **Validates: Requirements 4.1, 4.3, 5.1**
//
// The apply selects the record at `time_offset` and preserves the aggregate of
// that record's owned band regardless of the storage type (float or double)
// or which record in a multi-record buffer is selected. The pre-optimization
// apply had this same behavior; the caching layer does not alter it.
// ============================================================================
RC_GTEST_PROP(RegridConservationProperty, Property9_AggregateInvariantAcrossRecords, ()) {
    const int file_nx = 3;
    const int file_ny = 4;
    const int j0 = 0;
    const int j1 = 4;  // whole grid this time
    const RegridPlan plan = MakeIdentityPlan(file_nx, file_ny, j0, j1);

    const std::size_t record_len = static_cast<std::size_t>(file_nx) * file_ny;

    // Two stacked records; the apply must select exactly one via time_offset.
    const std::vector<double> record0 = *genSourceField(record_len);
    const std::vector<double> record1 = *genSourceField(record_len);

    // Build a double buffer [record0 | record1] and its float counterpart.
    std::vector<double> buffer_d;
    buffer_d.reserve(2 * record_len);
    buffer_d.insert(buffer_d.end(), record0.begin(), record0.end());
    buffer_d.insert(buffer_d.end(), record1.begin(), record1.end());

    std::vector<float> buffer_f;
    buffer_f.reserve(2 * record_len);
    for (double v : buffer_d) {
        buffer_f.push_back(static_cast<float>(v));
    }

    // Select the second record (time_offset == record_len).
    std::vector<double> dst_d;
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/record_len, /*is_float=*/false, buffer_d.data(), file_nx, file_ny, /*nx=*/file_nx, dst_d));

    // The selected-record aggregate equals the aggregate of record1 directly.
    const double expected_d = std::accumulate(record1.begin(), record1.end(), 0.0);
    RC_ASSERT(std::accumulate(dst_d.begin(), dst_d.end(), 0.0) == expected_d);

    // The float-buffer apply preserves the aggregate of the float-cast record1.
    std::vector<double> dst_f;
    RC_ASSERT(apply_regrid_plan(plan, /*time_offset=*/record_len, /*is_float=*/true, buffer_f.data(), file_nx, file_ny, /*nx=*/file_nx, dst_f));

    double expected_f = 0.0;
    for (std::size_t k = 0; k < record_len; ++k) {
        expected_f += static_cast<double>(buffer_f[record_len + k]);
    }
    RC_ASSERT(std::accumulate(dst_f.begin(), dst_f.end(), 0.0) == expected_f);
}

}  // namespace cece::io

// ============================================================================
// Kokkos + MPI lifecycle. apply_regrid_plan uses Kokkos HostSpace views, so
// Kokkos must be initialized; the identity path touches no MPI, but the CECE
// link brings MPI symbols in, so initialize/finalize it defensively (mirroring
// tests/test_cece_utils.cpp).
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
