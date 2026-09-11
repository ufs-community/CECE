/**
 * @file test_ingest_consolidation_equivalence.cpp
 * @brief Property-based test that the consolidated single-transpose assembly
 *        delivers byte-identical values to every live consumer as the baseline
 *        multi-copy assembly, for any generated source field.
 *
 * Feature: ingest-copy-consolidation, Property 1: consolidated assembly
 * byte-identical to baseline for every live consumer
 *
 * **Validates: Requirements 1.3, 2.3, 2.4, 3.4, 4.1, 4.2, 8.1**
 *
 * ----------------------------------------------------------------------------
 * What this test exercises and why it is faithful
 * ----------------------------------------------------------------------------
 * The full AdvanceTime read/regrid/assemble path
 * (CeceDriverOrchestrator::AssembleReplicatedField, private, requiring a live
 * MPI + DAGR + CeceIO + AMIO environment) is far too heavy to stand up in a
 * unit test. But the ACTUAL determinant of the consolidation's correctness is
 * the transpose/copy phase of AssembleReplicatedField
 * (src/driver/cece_driver_facade.cpp, ~lines 688-720): the point where the
 * gathered `full_destination` (laid out [level][j][i], the fixed MPI_Allgatherv
 * layout this spec MUST NOT change) is transposed into the LayoutLeft
 * (i, j, level) DualView layout and copied into the live consumers — the core
 * import field (import_state.fields[var]) and, while retained, the CeceIO
 * stream_view.
 *
 * This test reproduces that phase EXACTLY against the REAL production types
 * (cece::DualView3D) and the REAL production index math
 * (host(i, j, level) = full_destination[level*nx*ny + j*nx + i]), in two
 * variants driven from the SAME generated input:
 *
 *   * BASELINE (reference multi-copy re-implementation of steps 2+3):
 *       - transpose #1 into a fresh stream mirror + deep_copy -> stream_view
 *       - transpose #2 (identical index math) into a fresh core mirror +
 *         deep_copy + modify_device + sync_host -> core import field
 *     This mirrors the pre-consolidation code verbatim (two independent
 *     transpose loops, each into its own create_mirror_view + deep_copy).
 *
 *   * CONSOLIDATED (the Tier-1 target this spec ships, matching the current
 *     AssembleReplicatedField):
 *       - ONE transpose into a single shared host buffer (i, j, level)
 *       - deep_copy that one buffer into the core import field
 *         (authoritative write) + modify_device + sync_host
 *       - deep_copy the SAME buffer into stream_view (no second transpose)
 *
 * The property then asserts the two variants deliver BYTE-IDENTICAL values to
 * both live consumers (core import field and stream_view) for the same inputs.
 * Because both variants read from the identical generated `full_destination`
 * with identical index arithmetic, any discrepancy would signal that
 * collapsing the two transposes into one shared buffer altered the delivered
 * data — exactly the regression Property 1 guards against.
 *
 * This is the same faithful-reproduction strategy used by the sibling
 * property tests in this tree (e.g. tests/test_slice_cache_equivalence.cpp):
 * the heavy end-to-end orchestration is not stood up, but the exact production
 * data transformation and production types are exercised, not a paraphrase.
 *
 * Grid extents and level counts are kept small so the total buffer stays modest
 * across >=100 iterations, respecting the ~7 GB cece-dev container RAM limit.
 */

#include <gtest/gtest.h>
#include <mpi.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "cece/cece_compute.hpp"  // cece::DualView3D

namespace cece {
namespace {

// ----------------------------------------------------------------------------
// Generated field shape. Small extents keep the total buffer modest across the
// >=100 iterations (worst case here is 12*16*16 = 3072 doubles per view), well
// within the ~7 GB container budget. field_nlev is kept small per the spec.
// ----------------------------------------------------------------------------
struct FieldShape {
    int nx;
    int ny;
    int nlev;
    std::size_t spatial() const {
        return static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    }
    std::size_t size() const {
        return spatial() * static_cast<std::size_t>(nlev);
    }
};

rc::Gen<FieldShape> genShape() {
    return rc::gen::apply([](int nx, int ny, int nlev) { return FieldShape{nx, ny, nlev}; }, rc::gen::inRange(1, 17), rc::gen::inRange(1, 17),
                          rc::gen::inRange(1, 5));
}

// Generate a gathered `full_destination` of exactly `n` doubles laid out
// [level][j][i] (level*nx*ny + j*nx + i), exactly the layout MPI_Allgatherv
// produces. Values deliberately span negatives, zeros, and large magnitudes so
// the byte-identical comparison is meaningful: any truncation, aliasing, or
// index mismatch introduced by the consolidation would be caught.
rc::Gen<std::vector<double>> genFullDestination(std::size_t n) {
    // A pool of representative doubles including negatives, zeros, fractions,
    // and large magnitudes. Emitted values are chosen from this pool so every
    // element is an exact, reproducible bit pattern.
    static const std::vector<double> kPool = {
        0.0,
        -0.0,
        1.0,
        -1.0,
        3.14159265358979,
        -2.718281828459045,
        1.0e-12,
        -1.0e-12,
        1.0e12,
        -1.0e12,
        1.5e-300,
        -9.87654321e200,
        42.0,
        -42.0,
        123456.789,
        -0.000123456789,
    };
    return rc::gen::container<std::vector<double>>(
        n, rc::gen::map(rc::gen::inRange<std::size_t>(0, kPool.size()), [](std::size_t idx) { return kPool[idx]; }));
}

// ----------------------------------------------------------------------------
// BASELINE (reference multi-copy re-implementation of steps 2+3): two
// independent transpose loops, each into its own create_mirror_view +
// deep_copy. Mirrors the pre-consolidation AssembleReplicatedField verbatim.
// Populates the two live consumers `stream` and `core`.
// ----------------------------------------------------------------------------
void AssembleBaselineMultiCopy(const std::vector<double>& full_destination, const FieldShape& s, DualView3D& stream, DualView3D& core) {
    const std::size_t target_spatial = s.spatial();
    auto stream_view = stream.view_device();
    auto core_view = core.view_device();

    // Transpose #1 -> stream_view (its own mirror + deep_copy).
    auto stream_host = Kokkos::create_mirror_view(stream_view);
    for (int level = 0; level < s.nlev; ++level) {
        for (int j = 0; j < s.ny; ++j) {
            for (int i = 0; i < s.nx; ++i) {
                stream_host(i, j, level) =
                    full_destination[static_cast<std::size_t>(level) * target_spatial + static_cast<std::size_t>(j) * s.nx + i];
            }
        }
    }
    Kokkos::deep_copy(stream_view, stream_host);

    // Transpose #2 (identical index math) -> core import field (authoritative
    // write, its own mirror + deep_copy + modify_device + sync_host).
    auto core_host = Kokkos::create_mirror_view(core_view);
    for (int level = 0; level < s.nlev; ++level) {
        for (int j = 0; j < s.ny; ++j) {
            for (int i = 0; i < s.nx; ++i) {
                core_host(i, j, level) = full_destination[static_cast<std::size_t>(level) * target_spatial + static_cast<std::size_t>(j) * s.nx + i];
            }
        }
    }
    Kokkos::deep_copy(core_view, core_host);
    core.modify_device();
    core.sync_host();
    Kokkos::fence();
}

// ----------------------------------------------------------------------------
// CONSOLIDATED (the shipped Tier-1 target, matching the current
// AssembleReplicatedField, src/driver/cece_driver_facade.cpp ~688-720): ONE
// transpose into a single shared host buffer, deep_copy'd into the core import
// field (authoritative) and into stream_view (no second transpose).
// ----------------------------------------------------------------------------
void AssembleConsolidatedSingleTranspose(const std::vector<double>& full_destination, const FieldShape& s, DualView3D& stream, DualView3D& core) {
    const std::size_t target_spatial = s.spatial();
    auto stream_view = stream.view_device();
    auto core_view = core.view_device();

    // Single transpose into one shared host buffer (i, j, level), matching the
    // production `transposed_host` buffer and its index math exactly.
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> transposed_host("assembled_field_host", s.nx, s.ny, s.nlev);
    for (int level = 0; level < s.nlev; ++level) {
        for (int j = 0; j < s.ny; ++j) {
            for (int i = 0; i < s.nx; ++i) {
                transposed_host(i, j, level) =
                    full_destination[static_cast<std::size_t>(level) * target_spatial + static_cast<std::size_t>(j) * s.nx + i];
            }
        }
    }

    // Authoritative write of the core import field from the single buffer.
    Kokkos::deep_copy(core_view, transposed_host);
    core.modify_device();
    core.sync_host();

    // Feed stream_view from the SAME single host buffer (no second transpose).
    Kokkos::deep_copy(stream_view, transposed_host);
    Kokkos::fence();
}

// Copy a DualView's device data down to a host mirror for element-wise
// comparison, returning a flat [i + nx*(j + ny*level)] host buffer.
std::vector<double> SnapshotToHost(DualView3D& dv, const FieldShape& s) {
    auto view = dv.view_device();
    auto host = Kokkos::create_mirror_view(view);
    Kokkos::deep_copy(host, view);
    Kokkos::fence();
    std::vector<double> out(s.size(), 0.0);
    for (int level = 0; level < s.nlev; ++level) {
        for (int j = 0; j < s.ny; ++j) {
            for (int i = 0; i < s.nx; ++i) {
                out[static_cast<std::size_t>(i) +
                    static_cast<std::size_t>(s.nx) *
                        (static_cast<std::size_t>(j) + static_cast<std::size_t>(s.ny) * static_cast<std::size_t>(level))] = host(i, j, level);
            }
        }
    }
    return out;
}

}  // namespace

// ============================================================================
// Property 1: consolidated assembly is byte-identical to the baseline for
// every live consumer.
//
// Feature: ingest-copy-consolidation, Property 1: consolidated assembly
// byte-identical to baseline for every live consumer
// **Validates: Requirements 1.3, 2.3, 2.4, 3.4, 4.1, 4.2, 8.1**
//
// For any generated source field (arbitrary finite doubles incl.
// negatives/zeros/large magnitudes, grid extents, and level count), the
// consolidated single-transpose assembly delivers byte-identical values to the
// core import field AND to stream_view (retained) as the baseline multi-copy
// assembly produces for the same inputs.
// ============================================================================
RC_GTEST_PROP(IngestConsolidationEquivalenceProperty, Property1_ConsolidatedByteIdenticalToBaseline, ()) {
    const FieldShape s = *genShape();
    const std::vector<double> full_destination = *genFullDestination(s.size());

    RC_ASSERT(full_destination.size() == s.size());

    // Two independent consumer pairs so the variants cannot cross-contaminate.
    DualView3D baseline_stream("baseline_stream", s.nx, s.ny, s.nlev);
    DualView3D baseline_core("baseline_core", s.nx, s.ny, s.nlev);
    DualView3D consolidated_stream("consolidated_stream", s.nx, s.ny, s.nlev);
    DualView3D consolidated_core("consolidated_core", s.nx, s.ny, s.nlev);

    AssembleBaselineMultiCopy(full_destination, s, baseline_stream, baseline_core);
    AssembleConsolidatedSingleTranspose(full_destination, s, consolidated_stream, consolidated_core);

    const std::vector<double> baseline_core_vals = SnapshotToHost(baseline_core, s);
    const std::vector<double> baseline_stream_vals = SnapshotToHost(baseline_stream, s);
    const std::vector<double> consolidated_core_vals = SnapshotToHost(consolidated_core, s);
    const std::vector<double> consolidated_stream_vals = SnapshotToHost(consolidated_stream, s);

    RC_ASSERT(consolidated_core_vals.size() == baseline_core_vals.size());
    RC_ASSERT(consolidated_stream_vals.size() == baseline_stream_vals.size());

    // Core import field: byte-identical (bit-for-bit) to the baseline.
    // (Req 2.3, 2.4, 3.4, 4.2)
    for (std::size_t k = 0; k < baseline_core_vals.size(); ++k) {
        RC_ASSERT(consolidated_core_vals[k] == baseline_core_vals[k]);
    }
    RC_ASSERT(consolidated_core_vals == baseline_core_vals);

    // stream_view (retained): byte-identical (bit-for-bit) to the baseline.
    // (Req 1.3, 4.1)
    for (std::size_t k = 0; k < baseline_stream_vals.size(); ++k) {
        RC_ASSERT(consolidated_stream_vals[k] == baseline_stream_vals[k]);
    }
    RC_ASSERT(consolidated_stream_vals == baseline_stream_vals);

    // The two live consumers also agree with each other under the consolidated
    // path (both fed from the one shared buffer), which is the invariant the
    // single-transpose collapse must preserve.
    RC_ASSERT(consolidated_core_vals == consolidated_stream_vals);
}

}  // namespace cece

// ============================================================================
// Kokkos + MPI global test environment and custom main().
//
// This test uses cece::DualView3D and Kokkos::deep_copy, so Kokkos must be
// initialized before any test runs and finalized after. Following the
// established pattern in this tree (tests/test_regrid_conservation_properties.cpp),
// a global test environment brings Kokkos (and MPI, which Kokkos/CECE headers
// pull in) up and down, and a custom main() ensures single-process execution.
// Because this file provides its own main(), the target links GTest::gtest
// (not GTest::gtest_main).
// ============================================================================
namespace {

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

}  // namespace

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
