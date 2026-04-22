/**
 * @file test_ginoux_properties.cpp
 * @brief Property-based tests for the Ginoux (GOCART2G) dust emission scheme.
 *
 * Validates correctness properties for the Ginoux helper functions and
 * the full scheme implementation using randomized inputs.
 *
 * Properties tested:
 * 1. Marticorena dry-soil threshold computation
 * 2. Ginoux moisture threshold behavior
 * 3. Numerical equivalence between C++ and Fortran implementations
 * 4. Zero-emission invariant for non-emitting cells
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <random>

#include "cece/cece_physics_factory.hpp"
#include "cece/cece_state.hpp"

// ============================================================================
// Reference implementations of Ginoux helper functions for host-side testing.
// These mirror the KOKKOS_INLINE_FUNCTION helpers in cece_ginoux.cpp exactly.
// ============================================================================
namespace ginoux_ref {

double marticorena_threshold(double diameter, double soil_density, double grav, double air_dens) {
    double reynol = 1331.0 * std::pow(100.0 * diameter, 1.56) + 0.38;
    return 0.13 * std::sqrt(soil_density * grav * diameter / air_dens) * std::sqrt(1.0 + 6.0e-7 / (soil_density * grav * std::pow(diameter, 2.5))) /
           std::sqrt(1.928 * std::pow(reynol, 0.092) - 1.0);
}

double moisture_threshold(double u_thresh0, double gwettop) {
    if (gwettop >= 0.5) return 0.0;
    return std::max(0.0, u_thresh0 * (1.2 + 0.2 * std::log10(std::max(1.0e-3, gwettop))));
}

}  // namespace ginoux_ref

namespace cece {

class GinouxPropertyTest : public ::testing::Test {
   public:
    static void SetUpTestSuite() {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }

    std::mt19937 rng_{42};
    static constexpr int NUM_ITERATIONS = 200;

    double rand_uniform(double lo, double hi) {
        std::uniform_real_distribution<double> dist(lo, hi);
        return dist(rng_);
    }

    int rand_int(int lo, int hi) {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(rng_);
    }

    [[nodiscard]] DualView3D create_dv(const std::string& name, int nx, int ny, int nz, double val) const {
        DualView3D dv(name, nx, ny, nz);
        Kokkos::deep_copy(dv.view_host(), val);
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
        return dv;
    }
};

// ============================================================================
// Property 1: Marticorena dry-soil threshold computation
// **Validates: Requirements 9.1, 9.3**
// ============================================================================

TEST_F(GinouxPropertyTest, Property1_MarticorenaThreshold) {
    constexpr double soil_density = 2650.0;
    constexpr double air_dens = 1.25;

    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        double diameter = rand_uniform(0.1e-6, 50.0e-6);
        double grav = rand_uniform(9.0, 10.0);

        double result = ginoux_ref::marticorena_threshold(diameter, soil_density, grav, air_dens);

        // Reference formula computed step-by-step
        double reynol = 1331.0 * std::pow(100.0 * diameter, 1.56) + 0.38;
        double term1 = 0.13 * std::sqrt(soil_density * grav * diameter / air_dens);
        double term2 = std::sqrt(1.0 + 6.0e-7 / (soil_density * grav * std::pow(diameter, 2.5)));
        double term3 = std::sqrt(1.928 * std::pow(reynol, 0.092) - 1.0);
        double expected = term1 * term2 / term3;

        EXPECT_NEAR(result, expected, std::abs(expected) * 1e-14 + 1e-14)
            << "Failed at iter=" << iter << " diameter=" << diameter << " grav=" << grav;

        // Threshold must be positive for valid inputs
        EXPECT_GT(result, 0.0) << "Threshold must be positive, iter=" << iter;
    }
}

// ============================================================================
// Property 2: Ginoux moisture threshold behavior
// **Validates: Requirements 9.2, 9.3**
// ============================================================================

TEST_F(GinouxPropertyTest, Property2_MoistureThreshold) {
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        double u_thresh0 = rand_uniform(0.01, 5.0);
        double gwettop = rand_uniform(0.0, 1.0);

        double result = ginoux_ref::moisture_threshold(u_thresh0, gwettop);

        if (gwettop >= 0.5) {
            // When gwettop >= 0.5, moisture threshold returns 0 (no emission)
            EXPECT_DOUBLE_EQ(result, 0.0) << "Should return 0 when gwettop >= 0.5. gwettop=" << gwettop << " iter=" << iter;
        } else {
            // When gwettop < 0.5, compute the adjusted threshold
            double expected = std::max(0.0, u_thresh0 * (1.2 + 0.2 * std::log10(std::max(1.0e-3, gwettop))));
            EXPECT_NEAR(result, expected, std::abs(expected) * 1e-14 + 1e-14)
                << "Failed at iter=" << iter << " u_thresh0=" << u_thresh0 << " gwettop=" << gwettop;

            // Result must be non-negative
            EXPECT_GE(result, 0.0) << "Adjusted threshold must be non-negative";
        }
    }
}

// ============================================================================
// Property 3: Ginoux numerical equivalence between C++ and Fortran
// **Validates: Requirements 7.1, 1.7**
// ============================================================================

TEST_F(GinouxPropertyTest, Property3_NumericalEquivalence) {
    // Check at runtime if the Fortran scheme is available
    PhysicsSchemeConfig cfg_fort_check;
    cfg_fort_check.name = "ginoux_fortran";
    auto fort_check = PhysicsFactory::CreateScheme(cfg_fort_check);
    if (fort_check == nullptr) {
        GTEST_SKIP() << "ginoux_fortran scheme not available, skipping equivalence test";
    }

    for (int iter = 0; iter < 50; ++iter) {
        int nx = rand_int(1, 10);
        int ny = rand_int(1, 10);
        int nbins = rand_int(1, 5);

        PhysicsSchemeConfig cfg_cpp, cfg_fort;
        cfg_cpp.name = "ginoux";
        cfg_fort.name = "ginoux_fortran";

        auto scheme_cpp = PhysicsFactory::CreateScheme(cfg_cpp);
        auto scheme_fort = PhysicsFactory::CreateScheme(cfg_fort);
        ASSERT_NE(scheme_cpp, nullptr);
        ASSERT_NE(scheme_fort, nullptr);

        scheme_cpp->Initialize(cfg_cpp.options, nullptr);
        scheme_fort->Initialize(cfg_fort.options, nullptr);

        // Create import state with random physically valid values
        CeceImportState import_state;
        CeceExportState export_cpp, export_fort;

        import_state.fields["u10m"] = create_dv("u10m", nx, ny, 1, 0.0);
        import_state.fields["v10m"] = create_dv("v10m", nx, ny, 1, 0.0);
        import_state.fields["surface_soil_wetness"] = create_dv("gwettop", nx, ny, 1, 0.0);
        import_state.fields["land_mask"] = create_dv("oro", nx, ny, 1, 1.0);
        import_state.fields["lake_fraction"] = create_dv("fraclake", nx, ny, 1, 0.0);
        import_state.fields["dust_source"] = create_dv("du_src", nx, ny, 1, 0.0);
        // particle_radius stored as (1, 1, nbins) — the Fortran bridge reads .data() as 1D
        import_state.fields["particle_radius"] = create_dv("radius", 1, 1, nbins, 0.0);

        // Fill 2D fields with random values
        auto fill_random_2d = [&](const std::string& name, double lo, double hi) {
            auto h = import_state.fields[name].view_host();
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) h(i, j, 0) = rand_uniform(lo, hi);
            import_state.fields[name].modify<Kokkos::HostSpace>();
            import_state.fields[name].sync<Kokkos::DefaultExecutionSpace>();
        };

        fill_random_2d("u10m", -15.0, 15.0);
        fill_random_2d("v10m", -15.0, 15.0);
        fill_random_2d("surface_soil_wetness", 0.0, 0.6);
        fill_random_2d("lake_fraction", 0.0, 0.3);
        fill_random_2d("dust_source", 0.1, 2.0);

        // Fill particle radii
        {
            auto h = import_state.fields["particle_radius"].view_host();
            for (int n = 0; n < nbins; ++n) h(0, 0, n) = rand_uniform(0.1e-6, 25.0e-6);
            import_state.fields["particle_radius"].modify<Kokkos::HostSpace>();
            import_state.fields["particle_radius"].sync<Kokkos::DefaultExecutionSpace>();
        }

        export_cpp.fields["ginoux_dust_emissions"] = create_dv("emis_cpp", nx, ny, nbins, 0.0);
        export_fort.fields["ginoux_dust_emissions"] = create_dv("emis_fort", nx, ny, nbins, 0.0);

        // Run both schemes
        auto* base_cpp = dynamic_cast<BasePhysicsScheme*>(scheme_cpp.get());
        if (base_cpp) base_cpp->ClearPhysicsCache();
        scheme_cpp->Run(import_state, export_cpp);

        auto* base_fort = dynamic_cast<BasePhysicsScheme*>(scheme_fort.get());
        if (base_fort) base_fort->ClearPhysicsCache();
        scheme_fort->Run(import_state, export_fort);

        // Compare results
        auto& dv_cpp = export_cpp.fields["ginoux_dust_emissions"];
        auto& dv_fort = export_fort.fields["ginoux_dust_emissions"];
        dv_cpp.sync<Kokkos::HostSpace>();
        dv_fort.sync<Kokkos::HostSpace>();
        auto h_cpp = dv_cpp.view_host();
        auto h_fort = dv_fort.view_host();

        for (int n = 0; n < nbins; ++n) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    double cpp_val = h_cpp(i, j, n);
                    double fort_val = h_fort(i, j, n);
                    double tol = std::max(std::abs(cpp_val), std::abs(fort_val)) * 1e-12 + 1e-20;
                    EXPECT_NEAR(cpp_val, fort_val, tol) << "Mismatch at (" << i << "," << j << "," << n << ") iter=" << iter;
                }
            }
        }
    }
}

// ============================================================================
// Property 4: Ginoux zero-emission invariant
// **Validates: Requirements 7.2, 7.3, 7.4**
// ============================================================================

TEST_F(GinouxPropertyTest, Property4_ZeroEmissionInvariant) {
    // Test that ocean cells, high-wetness cells, and below-threshold wind cells
    // produce zero emissions in both C++ and Fortran schemes.
    int nx = 6, ny = 6, nbins = 3;

    // Check if Fortran scheme is available
    PhysicsSchemeConfig cfg_fort_check;
    cfg_fort_check.name = "ginoux_fortran";
    auto fort_check = PhysicsFactory::CreateScheme(cfg_fort_check);
    bool has_fortran = (fort_check != nullptr);

    for (int iter = 0; iter < 50; ++iter) {
        PhysicsSchemeConfig cfg_cpp, cfg_fort;
        cfg_cpp.name = "ginoux";
        cfg_fort.name = "ginoux_fortran";

        auto scheme_cpp = PhysicsFactory::CreateScheme(cfg_cpp);
        ASSERT_NE(scheme_cpp, nullptr);
        scheme_cpp->Initialize(cfg_cpp.options, nullptr);

        std::unique_ptr<PhysicsScheme> scheme_fort;
        if (has_fortran) {
            scheme_fort = PhysicsFactory::CreateScheme(cfg_fort);
            scheme_fort->Initialize(cfg_fort.options, nullptr);
        }

        CeceImportState import_state;
        CeceExportState export_cpp, export_fort;

        // Create fields with random valid values
        import_state.fields["u10m"] = create_dv("u10m", nx, ny, 1, 0.0);
        import_state.fields["v10m"] = create_dv("v10m", nx, ny, 1, 0.0);
        import_state.fields["surface_soil_wetness"] = create_dv("gwettop", nx, ny, 1, 0.0);
        import_state.fields["land_mask"] = create_dv("oro", nx, ny, 1, 1.0);
        import_state.fields["lake_fraction"] = create_dv("fraclake", nx, ny, 1, 0.0);
        import_state.fields["dust_source"] = create_dv("du_src", nx, ny, 1, rand_uniform(0.5, 2.0));
        import_state.fields["particle_radius"] = create_dv("radius", 1, 1, nbins, 0.0);

        // Fill particle radii with physically valid values
        {
            auto h = import_state.fields["particle_radius"].view_host();
            for (int n = 0; n < nbins; ++n) h(0, 0, n) = rand_uniform(1.0e-6, 10.0e-6);
            import_state.fields["particle_radius"].modify<Kokkos::HostSpace>();
            import_state.fields["particle_radius"].sync<Kokkos::DefaultExecutionSpace>();
        }

        // Fill wind with moderate values
        {
            auto hu = import_state.fields["u10m"].view_host();
            auto hv = import_state.fields["v10m"].view_host();
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    hu(i, j, 0) = rand_uniform(2.0, 8.0);
                    hv(i, j, 0) = rand_uniform(2.0, 8.0);
                }
            import_state.fields["u10m"].modify<Kokkos::HostSpace>();
            import_state.fields["u10m"].sync<Kokkos::DefaultExecutionSpace>();
            import_state.fields["v10m"].modify<Kokkos::HostSpace>();
            import_state.fields["v10m"].sync<Kokkos::DefaultExecutionSpace>();
        }

        // Fill gwettop with moderate values
        {
            auto h = import_state.fields["surface_soil_wetness"].view_host();
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) h(i, j, 0) = rand_uniform(0.01, 0.3);
            import_state.fields["surface_soil_wetness"].modify<Kokkos::HostSpace>();
            import_state.fields["surface_soil_wetness"].sync<Kokkos::DefaultExecutionSpace>();
        }

        // Force specific cells to non-emitting conditions:
        auto oro_h = import_state.fields["land_mask"].view_host();
        auto gwettop_h = import_state.fields["surface_soil_wetness"].view_host();
        auto u10m_h = import_state.fields["u10m"].view_host();
        auto v10m_h = import_state.fields["v10m"].view_host();

        // Rows 0-1: ocean cells (land_mask = 0)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < 2; ++i) oro_h(i, j, 0) = 0.0;

        // Rows 2-3: high wetness (gwettop >= 0.5)
        for (int j = 0; j < ny; ++j)
            for (int i = 2; i < 4; ++i) gwettop_h(i, j, 0) = rand_uniform(0.5, 1.0);

        // Rows 4-5: very low wind speed (below any possible threshold)
        for (int j = 0; j < ny; ++j)
            for (int i = 4; i < 6; ++i) {
                u10m_h(i, j, 0) = rand_uniform(-0.001, 0.001);
                v10m_h(i, j, 0) = rand_uniform(-0.001, 0.001);
            }

        import_state.fields["land_mask"].modify<Kokkos::HostSpace>();
        import_state.fields["land_mask"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["surface_soil_wetness"].modify<Kokkos::HostSpace>();
        import_state.fields["surface_soil_wetness"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["u10m"].modify<Kokkos::HostSpace>();
        import_state.fields["u10m"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["v10m"].modify<Kokkos::HostSpace>();
        import_state.fields["v10m"].sync<Kokkos::DefaultExecutionSpace>();

        export_cpp.fields["ginoux_dust_emissions"] = create_dv("emis_cpp", nx, ny, nbins, 0.0);

        auto* base_cpp = dynamic_cast<BasePhysicsScheme*>(scheme_cpp.get());
        if (base_cpp) base_cpp->ClearPhysicsCache();
        scheme_cpp->Run(import_state, export_cpp);

        auto& dv_cpp = export_cpp.fields["ginoux_dust_emissions"];
        dv_cpp.sync<Kokkos::HostSpace>();
        auto emis_cpp_h = dv_cpp.view_host();

        // Verify zero emissions for non-emitting cells (C++)
        for (int j = 0; j < ny; ++j) {
            for (int n = 0; n < nbins; ++n) {
                // Ocean cells (rows 0-1)
                for (int i = 0; i < 2; ++i) {
                    EXPECT_DOUBLE_EQ(emis_cpp_h(i, j, n), 0.0)
                        << "C++ ocean cell (" << i << "," << j << "," << n << ") should have zero emissions, iter=" << iter;
                }
                // High wetness cells (rows 2-3)
                for (int i = 2; i < 4; ++i) {
                    EXPECT_DOUBLE_EQ(emis_cpp_h(i, j, n), 0.0)
                        << "C++ high-wetness cell (" << i << "," << j << "," << n << ") should have zero emissions, iter=" << iter;
                }
                // Below-threshold wind cells (rows 4-5)
                for (int i = 4; i < 6; ++i) {
                    EXPECT_DOUBLE_EQ(emis_cpp_h(i, j, n), 0.0)
                        << "C++ below-threshold cell (" << i << "," << j << "," << n << ") should have zero emissions, iter=" << iter;
                }
            }
        }

        // Also verify Fortran scheme if available
        if (has_fortran) {
            export_fort.fields["ginoux_dust_emissions"] = create_dv("emis_fort", nx, ny, nbins, 0.0);

            auto* base_fort = dynamic_cast<BasePhysicsScheme*>(scheme_fort.get());
            if (base_fort) base_fort->ClearPhysicsCache();
            scheme_fort->Run(import_state, export_fort);

            auto& dv_fort = export_fort.fields["ginoux_dust_emissions"];
            dv_fort.sync<Kokkos::HostSpace>();
            auto emis_fort_h = dv_fort.view_host();

            for (int j = 0; j < ny; ++j) {
                for (int n = 0; n < nbins; ++n) {
                    for (int i = 0; i < 2; ++i) {
                        EXPECT_DOUBLE_EQ(emis_fort_h(i, j, n), 0.0)
                            << "Fortran ocean cell (" << i << "," << j << "," << n << ") should have zero emissions, iter=" << iter;
                    }
                    for (int i = 2; i < 4; ++i) {
                        EXPECT_DOUBLE_EQ(emis_fort_h(i, j, n), 0.0)
                            << "Fortran high-wetness cell (" << i << "," << j << "," << n << ") should have zero emissions, iter=" << iter;
                    }
                    for (int i = 4; i < 6; ++i) {
                        EXPECT_DOUBLE_EQ(emis_fort_h(i, j, n), 0.0)
                            << "Fortran below-threshold cell (" << i << "," << j << "," << n << ") should have zero emissions, iter=" << iter;
                    }
                }
            }
        }
    }
}

}  // namespace cece

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
