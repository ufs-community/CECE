/**
 * @file test_column_local_band_invariance.cpp
 * @brief Integration test: column-local compute is band-invariant.
 *
 * Feature: distributed-domain-decomposition, Property 7: Column-local compute
 * is band-invariant.
 *
 * The CECE physics schemes are column-local: each output cell (i, j, *) reads
 * only inputs at the same horizontal location (i, j), with no access to
 * horizontal neighbours. See src/core/physics/cece_ginoux.cpp — the kernel
 * self-sizes from the export view extents
 * (`MDRangePolicy({0,0},{nx,ny})`) and reads every input at (i, j, *). Because
 * of this, computing a scheme over the FULL global latitude range [0, ny) and
 * computing it over a contiguous band of rows [j0, j1) produce bit-for-bit
 * identical emissions at every global cell owned by that band: the arithmetic
 * for cell (i, j) is exactly the same expression evaluated on exactly the same
 * inputs, regardless of how many other rows sit in the view.
 *
 * This is the compute-side justification for the persistent latitude-band
 * decomposition (no halo exchange is required for physics).
 *
 * Approach:
 *   1. Pick the representative Ginoux (GOCART2G) dust scheme.
 *   2. Generate deterministic-per-iteration import fields over a small global
 *      grid (nx small, ny modest, nbins small).
 *   3. Run the scheme over the FULL global grid, capturing the global export.
 *   4. For a band [j0, j1) chosen with the exact block-decomposition formula
 *      the driver uses, build band-local import fields = rows [j0, j1) of the
 *      same global inputs, run the SAME scheme over the band, and assert the
 *      band output at (i, jrel) equals the global output at (i, j0 + jrel) for
 *      every i, jrel, bit-for-bit.
 *   5. Sweep several simulated rank counts (bands) per generated input.
 *
 * Inputs are generated with RapidCheck (>= 100 iterations). Grids are kept
 * small (fits well within the container's ~7 GB RAM).
 *
 * **Validates: Requirements 5.1, 5.3, 5.5**
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <string>
#include <vector>

#include "cece/cece_physics_factory.hpp"
#include "cece/cece_state.hpp"

namespace cece {
namespace {

// ---------------------------------------------------------------------------
// Block decomposition band bounds for rank `r` of `size`, using the EXACT
// formula the driver/regrid path and BandDecomposition::compute use:
//   band_base    = ny / size
//   band_rem     = ny % size
//   band_start(r)= r * band_base + min(r, band_rem)
// Kept local (pure, no MPI) so this compute-side test runs single-process and
// deterministically while still exercising the real band geometry.
// ---------------------------------------------------------------------------
int BandStart(int ny, int size, int r) {
    const int band_base = ny / size;
    const int band_rem = ny % size;
    return r * band_base + std::min(r, band_rem);
}

// Build a (nx, ny, nz) DualView3D filled uniformly with `val`.
DualView3D MakeFilled(const std::string& name, int nx, int ny, int nz, double val) {
    DualView3D dv(name, nx, ny, nz);
    Kokkos::deep_copy(dv.view_host(), val);
    dv.modify<Kokkos::HostSpace>();
    dv.sync<Kokkos::DefaultExecutionSpace>();
    return dv;
}

// A per-cell input record for the Ginoux scheme (all fields are 2D, level 0).
struct CellInputs {
    double u10m;
    double v10m;
    double gwettop;   // surface_soil_wetness
    double oro;       // land_mask (1.0 == land)
    double fraclake;  // lake_fraction
    double du_src;    // dust_source
};

// Deterministic per-cell input generator. Kept in physically plausible ranges
// that exercise both emitting and non-emitting branches of the Ginoux kernel.
CellInputs GenCell() {
    CellInputs c;
    c.u10m = *rc::gen::inRange(-1500, 1500) / 100.0;   // [-15, 15] m/s
    c.v10m = *rc::gen::inRange(-1500, 1500) / 100.0;   // [-15, 15] m/s
    c.gwettop = *rc::gen::inRange(0, 100) / 100.0;     // [0, 1]
    c.oro = (*rc::gen::inRange(0, 4) == 0) ? 0.0 : 1.0;  // ~75% land
    c.fraclake = *rc::gen::inRange(0, 40) / 100.0;     // [0, 0.4]
    c.du_src = *rc::gen::inRange(0, 300) / 100.0;      // [0, 3]
    return c;
}

// Instantiate + initialize a fresh Ginoux scheme (native C++).
std::unique_ptr<PhysicsScheme> MakeGinoux() {
    PhysicsSchemeConfig cfg;
    cfg.name = "ginoux";
    auto scheme = PhysicsFactory::CreateScheme(cfg);
    if (scheme) scheme->Initialize(cfg.options, nullptr);
    return scheme;
}

// Run Ginoux over a grid whose latitude extent is [0, ny_run), reading the
// global cell inputs at latitude rows [row_offset, row_offset + ny_run).
// `radii` are the per-bin particle radii shared by every run. Returns the
// export emissions host view (nx, ny_run, nbins) copied out into a flat vector
// in (i, jrel, n) order.
std::vector<double> RunGinouxOverRows(const std::vector<std::vector<CellInputs>>& global,  // [j][i]
                                      const std::vector<double>& radii, int nx, int nbins, int row_offset,
                                      int ny_run) {
    auto scheme = MakeGinoux();
    RC_ASSERT(scheme != nullptr);

    CeceImportState import_state;
    CeceExportState export_state;

    // 2D import fields sized (nx, ny_run, 1); particle_radius is (1, 1, nbins).
    import_state.fields["u10m"] = MakeFilled("u10m", nx, ny_run, 1, 0.0);
    import_state.fields["v10m"] = MakeFilled("v10m", nx, ny_run, 1, 0.0);
    import_state.fields["surface_soil_wetness"] = MakeFilled("gwettop", nx, ny_run, 1, 0.0);
    import_state.fields["land_mask"] = MakeFilled("oro", nx, ny_run, 1, 1.0);
    import_state.fields["lake_fraction"] = MakeFilled("fraclake", nx, ny_run, 1, 0.0);
    import_state.fields["dust_source"] = MakeFilled("du_src", nx, ny_run, 1, 0.0);
    import_state.fields["particle_radius"] = MakeFilled("radius", 1, 1, nbins, 0.0);

    auto hu = import_state.fields["u10m"].view_host();
    auto hv = import_state.fields["v10m"].view_host();
    auto hg = import_state.fields["surface_soil_wetness"].view_host();
    auto ho = import_state.fields["land_mask"].view_host();
    auto hl = import_state.fields["lake_fraction"].view_host();
    auto hd = import_state.fields["dust_source"].view_host();

    for (int jrel = 0; jrel < ny_run; ++jrel) {
        const int gj = row_offset + jrel;
        for (int i = 0; i < nx; ++i) {
            const CellInputs& c = global[gj][i];
            hu(i, jrel, 0) = c.u10m;
            hv(i, jrel, 0) = c.v10m;
            hg(i, jrel, 0) = c.gwettop;
            ho(i, jrel, 0) = c.oro;
            hl(i, jrel, 0) = c.fraclake;
            hd(i, jrel, 0) = c.du_src;
        }
    }
    for (auto& name : {"u10m", "v10m", "surface_soil_wetness", "land_mask", "lake_fraction", "dust_source"}) {
        import_state.fields[name].modify<Kokkos::HostSpace>();
        import_state.fields[name].sync<Kokkos::DefaultExecutionSpace>();
    }

    {
        auto hr = import_state.fields["particle_radius"].view_host();
        for (int n = 0; n < nbins; ++n) hr(0, 0, n) = radii[n];
        import_state.fields["particle_radius"].modify<Kokkos::HostSpace>();
        import_state.fields["particle_radius"].sync<Kokkos::DefaultExecutionSpace>();
    }

    export_state.fields["ginoux_dust_emissions"] = MakeFilled("emis", nx, ny_run, nbins, 0.0);

    auto* base = dynamic_cast<BasePhysicsScheme*>(scheme.get());
    if (base) base->ClearPhysicsCache();
    scheme->Run(import_state, export_state);

    auto& dv = export_state.fields["ginoux_dust_emissions"];
    dv.sync<Kokkos::HostSpace>();
    auto he = dv.view_host();

    std::vector<double> out(static_cast<size_t>(nx) * ny_run * nbins, 0.0);
    for (int jrel = 0; jrel < ny_run; ++jrel)
        for (int i = 0; i < nx; ++i)
            for (int n = 0; n < nbins; ++n) out[(static_cast<size_t>(jrel) * nx + i) * nbins + n] = he(i, jrel, n);
    return out;
}

}  // namespace

// ============================================================================
// Property 7: Column-local compute is band-invariant (Ginoux)
// Feature: distributed-domain-decomposition, Property 7: Column-local compute
// is band-invariant
// **Validates: Requirements 5.1, 5.3, 5.5**
//
// Emissions at a fixed global cell (i, gj) are bit-for-bit identical whether
// computed over the full global grid or over a band [j0, j1) that owns gj,
// because the Ginoux kernel reads only inputs at (i, gj) — no cross-row
// coupling. Sweeps several simulated rank counts (bands) per generated input.
// ============================================================================
RC_GTEST_PROP(ColumnLocalBandInvariance, Property7_GinouxBandInvariant, ()) {
    // Small global grid: keeps memory tiny while covering multiple bands.
    const int nx = *rc::gen::inRange(1, 6);
    const int ny = *rc::gen::inRange(1, 12);
    const int nbins = *rc::gen::inRange(1, 4);

    // Shared per-bin particle radii, physically valid dust sizes.
    std::vector<double> radii(nbins);
    for (int n = 0; n < nbins; ++n) radii[n] = *rc::gen::inRange(1, 250) / 10.0 * 1.0e-6;  // [0.1, 25] um

    // Generate the global inputs [j][i].
    std::vector<std::vector<CellInputs>> global(ny, std::vector<CellInputs>(nx));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) global[j][i] = GenCell();

    // Reference: run the scheme over the whole global grid [0, ny).
    const std::vector<double> global_out = RunGinouxOverRows(global, radii, nx, nbins, /*row_offset=*/0, /*ny_run=*/ny);

    // Sweep a few simulated communicator sizes (bands). size == 1 is the
    // whole-grid identity check; larger sizes split into multiple bands.
    const int max_size = std::min(ny + 1, 5);  // include a surplus-size case (size == ny+1)
    for (int size = 1; size <= max_size; ++size) {
        for (int r = 0; r < size; ++r) {
            const int j0 = BandStart(ny, size, r);
            const int j1 = BandStart(ny, size, r + 1);
            const int ny_local = j1 - j0;

            // Surplus ranks (ny_local == 0) own no rows: nothing to compare.
            if (ny_local == 0) continue;

            const std::vector<double> band_out = RunGinouxOverRows(global, radii, nx, nbins, /*row_offset=*/j0, /*ny_run=*/ny_local);

            // Band output at (i, jrel, n) must equal global output at
            // (i, j0 + jrel, n) exactly (same arithmetic, same inputs).
            for (int jrel = 0; jrel < ny_local; ++jrel) {
                const int gj = j0 + jrel;
                for (int i = 0; i < nx; ++i) {
                    for (int n = 0; n < nbins; ++n) {
                        const double bv = band_out[(static_cast<size_t>(jrel) * nx + i) * nbins + n];
                        const double gv = global_out[(static_cast<size_t>(gj) * nx + i) * nbins + n];
                        RC_ASSERT(bv == gv);
                    }
                }
            }
        }
    }
}

}  // namespace cece

// ---------------------------------------------------------------------------
// Own main() with a Kokkos scope guard (the schemes launch Kokkos kernels).
// No MPI is required: the band geometry is derived purely from the block
// decomposition formula, and the whole test runs single-process.
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    Kokkos::initialize(argc, argv);
    int rc = 0;
    {
        rc = RUN_ALL_TESTS();
    }
    Kokkos::finalize();
    return rc;
}
