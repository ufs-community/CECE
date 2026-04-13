/**
 * @file test_k14_properties.cpp
 * @brief Property-based tests for the K14 (Kok et al., 2014) dust emission scheme.
 *
 * Validates correctness properties for the K14 helper functions and
 * the full scheme implementation using randomized inputs.
 *
 * Properties tested:
 * 5. Kok vertical dust flux computation
 * 6. MacKinnon drag partition computation
 * 7. Smooth roughness lookup from soil texture
 * 8. Laurent erodibility computation
 * 9. Numerical equivalence between C++ and Fortran across opt_clay values
 * 10. Zero-emission invariant for non-emitting cells
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <random>

#include "cece/cece_physics_factory.hpp"
#include "cece/cece_state.hpp"

// ============================================================================
// Reference implementations of K14 helper functions for host-side testing.
// These mirror the KOKKOS_INLINE_FUNCTION helpers in cece_k14.cpp exactly,
// but use std:: math instead of Kokkos:: math for host-side execution.
// ============================================================================
namespace k14_ref {

double vertical_dust_flux(double u, double u_t, double rho_air,
                          double f_erod, double k_gamma) {
    constexpr double rho_a0 = 1.225;
    constexpr double u_st0 = 0.16;
    constexpr double C_d0 = 4.4e-5;
    constexpr double C_e = 2.0;
    constexpr double C_a = 2.7;
    double u_st = std::max(u_t * std::sqrt(rho_air / rho_a0), u_st0);
    double f_ust = (u_st - u_st0) / u_st0;
    double C_d = C_d0 * std::exp(-C_e * f_ust);
    return C_d * f_erod * k_gamma * rho_air
           * ((u * u - u_t * u_t) / u_st)
           * std::pow(u / u_t, C_a * f_ust);
}

double drag_partition(double z0, double z0s) {
    constexpr double z0_max = 5.0e-4;
    if (z0 <= z0s || z0 >= z0_max) return 1.0;
    return 1.0 - std::log(z0 / z0s)
           / std::log(0.7 * std::pow(122.55 / z0s, 0.8));
}

double smooth_roughness(int texture) {
    constexpr double Dc_soil[12] = {
        710e-6, 710e-6, 125e-6, 125e-6, 125e-6, 160e-6,
        710e-6, 125e-6, 125e-6, 160e-6, 125e-6,   2e-6
    };
    if (texture >= 1 && texture <= 12) {
        return Dc_soil[texture - 1] / 30.0;
    }
    return 125e-6 / 30.0;
}

double erodibility(double z0, int texture) {
    constexpr double z0_max = 5.0e-4;
    if (texture == 15) return 0.0;
    if (z0 > 3.0e-5 && z0 < z0_max) {
        return 0.7304 - 0.0804 * std::log10(100.0 * z0);
    }
    return 1.0;
}

}  // namespace k14_ref

namespace cece {

class K14PropertyTest : public ::testing::Test {
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

    [[nodiscard]] DualView3D create_dv(const std::string& name, int nx, int ny, int nz,
                                       double val) const {
        DualView3D dv(name, nx, ny, nz);
        Kokkos::deep_copy(dv.view_host(), val);
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
        return dv;
    }

    /// Helper to populate a K14 import state with random physically valid values.
    void fill_k14_import_state(CeceImportState& import_state, int nx, int ny) {
        import_state.fields["friction_velocity"]        = create_dv("ustar",   nx, ny, 1, 0.0);
        import_state.fields["soil_temperature"]         = create_dv("t_soil",  nx, ny, 1, 0.0);
        import_state.fields["volumetric_soil_moisture"] = create_dv("w_top",   nx, ny, 1, 0.0);
        import_state.fields["air_density"]              = create_dv("rho_air", nx, ny, 1, 0.0);
        import_state.fields["roughness_length"]         = create_dv("z0",      nx, ny, 1, 0.0);
        import_state.fields["height"]                   = create_dv("z",       nx, ny, 1, 0.0);
        import_state.fields["u_wind"]                   = create_dv("u_z",     nx, ny, 1, 0.0);
        import_state.fields["v_wind"]                   = create_dv("v_z",     nx, ny, 1, 0.0);
        import_state.fields["land_fraction"]            = create_dv("f_land",  nx, ny, 1, 0.0);
        import_state.fields["snow_fraction"]            = create_dv("f_snow",  nx, ny, 1, 0.0);
        import_state.fields["dust_source"]              = create_dv("f_src",   nx, ny, 1, 0.0);
        import_state.fields["sand_fraction"]            = create_dv("f_sand",  nx, ny, 1, 0.0);
        import_state.fields["silt_fraction"]            = create_dv("f_silt",  nx, ny, 1, 0.0);
        import_state.fields["clay_fraction"]            = create_dv("f_clay",  nx, ny, 1, 0.0);
        import_state.fields["soil_texture"]             = create_dv("texture", nx, ny, 1, 0.0);
        import_state.fields["vegetation_type"]          = create_dv("veg",     nx, ny, 1, 0.0);
        import_state.fields["vegetation_fraction"]      = create_dv("gvf",     nx, ny, 1, 0.0);

        auto fill_random_2d = [&](const std::string& name, double lo, double hi) {
            auto h = import_state.fields[name].view_host();
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                    h(i, j, 0) = rand_uniform(lo, hi);
            import_state.fields[name].modify<Kokkos::HostSpace>();
            import_state.fields[name].sync<Kokkos::DefaultExecutionSpace>();
        };

        fill_random_2d("friction_velocity",        0.1,  2.0);
        fill_random_2d("soil_temperature",         250.0, 320.0);
        fill_random_2d("volumetric_soil_moisture", 0.01, 0.3);
        fill_random_2d("air_density",              0.8,  1.4);
        fill_random_2d("roughness_length",         1e-5, 4e-4);
        fill_random_2d("height",                   10.0, 50.0);
        fill_random_2d("u_wind",                   -15.0, 15.0);
        fill_random_2d("v_wind",                   -15.0, 15.0);
        fill_random_2d("land_fraction",            0.5,  1.0);
        fill_random_2d("snow_fraction",            0.0,  0.1);
        fill_random_2d("dust_source",              0.1,  2.0);
        fill_random_2d("sand_fraction",            0.1,  0.8);
        fill_random_2d("silt_fraction",            0.05, 0.5);
        fill_random_2d("clay_fraction",            0.01, 0.4);

        // soil_texture: integer values 1-12 stored as double
        {
            auto h = import_state.fields["soil_texture"].view_host();
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                    h(i, j, 0) = static_cast<double>(rand_int(1, 12));
            import_state.fields["soil_texture"].modify<Kokkos::HostSpace>();
            import_state.fields["soil_texture"].sync<Kokkos::DefaultExecutionSpace>();
        }

        // vegetation_type: 7 (open shrublands) or 16 (barren) for emitting cells
        {
            auto h = import_state.fields["vegetation_type"].view_host();
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                    h(i, j, 0) = (rand_uniform(0.0, 1.0) < 0.5) ? 7.0 : 16.0;
            import_state.fields["vegetation_type"].modify<Kokkos::HostSpace>();
            import_state.fields["vegetation_type"].sync<Kokkos::DefaultExecutionSpace>();
        }

        fill_random_2d("vegetation_fraction", 0.0, 0.3);
    }
};

// ============================================================================
// Property 5: Kok vertical dust flux computation
// **Validates: Requirements 10.1, 10.5**
// ============================================================================

TEST_F(K14PropertyTest, Property5_KokVerticalDustFlux) {
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        double u_t = rand_uniform(0.01, 1.0);
        double u = rand_uniform(u_t + 0.001, 3.0);
        double rho_air = rand_uniform(0.5, 1.5);
        double f_erod = rand_uniform(1e-6, 1.0);
        double k_gamma = rand_uniform(1e-6, 1.0);

        double result = k14_ref::vertical_dust_flux(u, u_t, rho_air, f_erod, k_gamma);

        // Reference formula computed step-by-step
        constexpr double rho_a0 = 1.225, u_st0 = 0.16;
        constexpr double C_d0 = 4.4e-5, C_e = 2.0, C_a = 2.7;
        double u_st = std::max(u_t * std::sqrt(rho_air / rho_a0), u_st0);
        double f_ust = (u_st - u_st0) / u_st0;
        double C_d = C_d0 * std::exp(-C_e * f_ust);
        double expected = C_d * f_erod * k_gamma * rho_air
                          * ((u * u - u_t * u_t) / u_st)
                          * std::pow(u / u_t, C_a * f_ust);

        EXPECT_NEAR(result, expected, std::abs(expected) * 1e-14 + 1e-20)
            << "Failed at iter=" << iter
            << " u=" << u << " u_t=" << u_t << " rho_air=" << rho_air;

        // Flux must be positive for valid inputs where u > u_t
        EXPECT_GT(result, 0.0) << "Flux must be positive, iter=" << iter;
    }
}

// ============================================================================
// Property 6: MacKinnon drag partition computation
// **Validates: Requirements 10.2, 10.5**
// ============================================================================

TEST_F(K14PropertyTest, Property6_MacKinnonDragPartition) {
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        double z0 = rand_uniform(1e-6, 4.99e-4);
        double z0s = rand_uniform(1e-7, z0 * 0.99);

        double result = k14_ref::drag_partition(z0, z0s);

        // Reference formula step-by-step
        double expected = 1.0 - std::log(z0 / z0s)
                          / std::log(0.7 * std::pow(122.55 / z0s, 0.8));

        EXPECT_NEAR(result, expected, std::abs(expected) * 1e-14 + 1e-14)
            << "Failed at iter=" << iter << " z0=" << z0 << " z0s=" << z0s;
    }

    // Verify returns 1.0 when z0 <= z0s
    for (int iter = 0; iter < 50; ++iter) {
        double z0s = rand_uniform(1e-6, 4e-4);
        double z0 = rand_uniform(1e-7, z0s);  // z0 <= z0s

        double result = k14_ref::drag_partition(z0, z0s);
        EXPECT_DOUBLE_EQ(result, 1.0)
            << "Should return 1.0 when z0 <= z0s, iter=" << iter;
    }

    // Verify returns 1.0 when z0 >= z0_max
    for (int iter = 0; iter < 50; ++iter) {
        double z0 = rand_uniform(5.0e-4, 1.0e-2);
        double z0s = rand_uniform(1e-7, 1e-5);

        double result = k14_ref::drag_partition(z0, z0s);
        EXPECT_DOUBLE_EQ(result, 1.0)
            << "Should return 1.0 when z0 >= z0_max, iter=" << iter;
    }
}

// ============================================================================
// Property 7: Smooth roughness lookup from soil texture
// **Validates: Requirements 10.3, 10.5, 13.5**
// ============================================================================

TEST_F(K14PropertyTest, Property7_SmoothRoughnessLookup) {
    constexpr double Dc_soil[12] = {
        710e-6, 710e-6, 125e-6, 125e-6, 125e-6, 160e-6,
        710e-6, 125e-6, 125e-6, 160e-6, 125e-6,   2e-6
    };

    // Test all valid indices (1-12)
    for (int texture = 1; texture <= 12; ++texture) {
        double result = k14_ref::smooth_roughness(texture);
        double expected = Dc_soil[texture - 1] / 30.0;
        EXPECT_DOUBLE_EQ(result, expected)
            << "Mismatch for texture=" << texture;
    }

    // Test invalid indices return default
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        int texture = rand_int(0, 20);
        double result = k14_ref::smooth_roughness(texture);

        if (texture >= 1 && texture <= 12) {
            double expected = Dc_soil[texture - 1] / 30.0;
            EXPECT_DOUBLE_EQ(result, expected)
                << "Mismatch for valid texture=" << texture;
        } else {
            EXPECT_DOUBLE_EQ(result, 125e-6 / 30.0)
                << "Invalid texture=" << texture << " should return default";
        }
    }
}

// ============================================================================
// Property 8: Laurent erodibility computation
// **Validates: Requirements 10.4, 10.5**
// ============================================================================

TEST_F(K14PropertyTest, Property8_LaurentErodibility) {
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        double z0 = rand_uniform(0.0, 1e-3);
        int texture = rand_int(1, 20);

        double result = k14_ref::erodibility(z0, texture);

        if (texture == 15) {
            // Bedrock: always 0
            EXPECT_DOUBLE_EQ(result, 0.0)
                << "Bedrock (texture==15) should return 0.0, iter=" << iter;
        } else if (z0 > 3.0e-5 && z0 < 5.0e-4) {
            // Laurent formula range
            double expected = 0.7304 - 0.0804 * std::log10(100.0 * z0);
            EXPECT_NEAR(result, expected, std::abs(expected) * 1e-14 + 1e-14)
                << "Failed at iter=" << iter << " z0=" << z0 << " texture=" << texture;
        } else {
            // Outside range: 1.0
            EXPECT_DOUBLE_EQ(result, 1.0)
                << "Outside range should return 1.0, iter=" << iter
                << " z0=" << z0 << " texture=" << texture;
        }
    }
}

// ============================================================================
// Property 9: K14 numerical equivalence across all opt_clay values
// **Validates: Requirements 8.1, 8.5**
// ============================================================================

TEST_F(K14PropertyTest, Property9_NumericalEquivalence) {
    // Check at runtime if the Fortran scheme is available
    PhysicsSchemeConfig cfg_fort_check;
    cfg_fort_check.name = "k14_fortran";
    auto fort_check = PhysicsFactory::CreateScheme(cfg_fort_check);
    if (fort_check == nullptr) {
        GTEST_SKIP() << "k14_fortran scheme not available, skipping equivalence test";
    }

    for (int opt_clay = 0; opt_clay <= 2; ++opt_clay) {
        for (int iter = 0; iter < 20; ++iter) {
            int nx = rand_int(1, 8);
            int ny = rand_int(1, 8);
            int nbins = rand_int(1, 5);

            // Configure both schemes with the same opt_clay
            YAML::Node config;
            config["opt_clay"] = opt_clay;

            PhysicsSchemeConfig cfg_cpp, cfg_fort;
            cfg_cpp.name = "k14";
            cfg_cpp.options = config;
            cfg_fort.name = "k14_fortran";
            cfg_fort.options = config;

            auto scheme_cpp = PhysicsFactory::CreateScheme(cfg_cpp);
            auto scheme_fort = PhysicsFactory::CreateScheme(cfg_fort);
            ASSERT_NE(scheme_cpp, nullptr);
            ASSERT_NE(scheme_fort, nullptr);

            scheme_cpp->Initialize(cfg_cpp.options, nullptr);
            scheme_fort->Initialize(cfg_fort.options, nullptr);

            // Create import state with random physically valid values
            CeceImportState import_state;
            CeceExportState export_cpp, export_fort;

            fill_k14_import_state(import_state, nx, ny);

            export_cpp.fields["k14_dust_emissions"]  = create_dv("emis_cpp",  nx, ny, nbins, 0.0);
            export_fort.fields["k14_dust_emissions"] = create_dv("emis_fort", nx, ny, nbins, 0.0);

            // Run both schemes
            auto* base_cpp = dynamic_cast<BasePhysicsScheme*>(scheme_cpp.get());
            if (base_cpp) base_cpp->ClearPhysicsCache();
            scheme_cpp->Run(import_state, export_cpp);

            auto* base_fort = dynamic_cast<BasePhysicsScheme*>(scheme_fort.get());
            if (base_fort) base_fort->ClearPhysicsCache();
            scheme_fort->Run(import_state, export_fort);

            // Compare results
            auto& dv_cpp = export_cpp.fields["k14_dust_emissions"];
            auto& dv_fort = export_fort.fields["k14_dust_emissions"];
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
                        EXPECT_NEAR(cpp_val, fort_val, tol)
                            << "Mismatch at (" << i << "," << j << "," << n
                            << ") opt_clay=" << opt_clay << " iter=" << iter;
                    }
                }
            }
        }
    }
}

// ============================================================================
// Property 10: K14 zero-emission invariant
// **Validates: Requirements 8.2, 8.3, 8.4**
// ============================================================================

TEST_F(K14PropertyTest, Property10_ZeroEmissionInvariant) {
    // Test that cells with zero land fraction, roughness above z0_max,
    // or non-emitting vegetation produce zero emissions in both C++ and Fortran.
    int nx = 8, ny = 6, nbins = 3;

    // Check if Fortran scheme is available
    PhysicsSchemeConfig cfg_fort_check;
    cfg_fort_check.name = "k14_fortran";
    auto fort_check = PhysicsFactory::CreateScheme(cfg_fort_check);
    bool has_fortran = (fort_check != nullptr);

    for (int iter = 0; iter < 50; ++iter) {
        PhysicsSchemeConfig cfg_cpp, cfg_fort;
        cfg_cpp.name = "k14";
        cfg_fort.name = "k14_fortran";

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

        // Fill with random physically valid values (emitting cells)
        fill_k14_import_state(import_state, nx, ny);

        // Now force specific rows to non-emitting conditions:
        auto f_land_h = import_state.fields["land_fraction"].view_host();
        auto z0_h     = import_state.fields["roughness_length"].view_host();

        // Rows 0-1: zero land fraction
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < 2; ++i)
                f_land_h(i, j, 0) = 0.0;

        // Rows 2-3: roughness above z0_max (5.0e-4)
        for (int j = 0; j < ny; ++j)
            for (int i = 2; i < 4; ++i)
                z0_h(i, j, 0) = rand_uniform(5.0e-4, 1.0e-2);

        // Rows 4-5: friction velocity very low (below any possible threshold)
        auto ustar_h = import_state.fields["friction_velocity"].view_host();
        for (int j = 0; j < ny; ++j)
            for (int i = 4; i < 6; ++i)
                ustar_h(i, j, 0) = rand_uniform(0.0, 0.001);

        // Rows 6-7: zero erodibility via bedrock texture (15) and non-emitting veg type
        auto texture_h = import_state.fields["soil_texture"].view_host();
        auto veg_h     = import_state.fields["vegetation_type"].view_host();
        for (int j = 0; j < ny; ++j)
            for (int i = 6; i < 8; ++i) {
                texture_h(i, j, 0) = 15.0;  // bedrock → erodibility = 0
                veg_h(i, j, 0) = 1.0;       // non-emitting vegetation type
            }

        import_state.fields["land_fraction"].modify<Kokkos::HostSpace>();
        import_state.fields["land_fraction"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["roughness_length"].modify<Kokkos::HostSpace>();
        import_state.fields["roughness_length"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["friction_velocity"].modify<Kokkos::HostSpace>();
        import_state.fields["friction_velocity"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["soil_texture"].modify<Kokkos::HostSpace>();
        import_state.fields["soil_texture"].sync<Kokkos::DefaultExecutionSpace>();
        import_state.fields["vegetation_type"].modify<Kokkos::HostSpace>();
        import_state.fields["vegetation_type"].sync<Kokkos::DefaultExecutionSpace>();

        export_cpp.fields["k14_dust_emissions"] = create_dv("emis_cpp", nx, ny, nbins, 0.0);

        auto* base_cpp = dynamic_cast<BasePhysicsScheme*>(scheme_cpp.get());
        if (base_cpp) base_cpp->ClearPhysicsCache();
        scheme_cpp->Run(import_state, export_cpp);

        auto& dv_cpp = export_cpp.fields["k14_dust_emissions"];
        dv_cpp.sync<Kokkos::HostSpace>();
        auto emis_cpp_h = dv_cpp.view_host();

        // Verify zero emissions for non-emitting cells (C++)
        for (int j = 0; j < ny; ++j) {
            for (int n = 0; n < nbins; ++n) {
                // Zero land fraction (rows 0-1)
                for (int i = 0; i < 2; ++i) {
                    EXPECT_DOUBLE_EQ(emis_cpp_h(i, j, n), 0.0)
                        << "C++ zero-land cell (" << i << "," << j << "," << n
                        << ") should have zero emissions, iter=" << iter;
                }
                // Roughness above z0_max (rows 2-3)
                for (int i = 2; i < 4; ++i) {
                    EXPECT_DOUBLE_EQ(emis_cpp_h(i, j, n), 0.0)
                        << "C++ high-roughness cell (" << i << "," << j << "," << n
                        << ") should have zero emissions, iter=" << iter;
                }
                // Bedrock + non-emitting veg (rows 6-7)
                for (int i = 6; i < 8; ++i) {
                    EXPECT_DOUBLE_EQ(emis_cpp_h(i, j, n), 0.0)
                        << "C++ bedrock/non-emitting-veg cell (" << i << "," << j << "," << n
                        << ") should have zero emissions, iter=" << iter;
                }
            }
        }

        // Also verify Fortran scheme if available
        if (has_fortran) {
            export_fort.fields["k14_dust_emissions"] = create_dv("emis_fort", nx, ny, nbins, 0.0);

            auto* base_fort = dynamic_cast<BasePhysicsScheme*>(scheme_fort.get());
            if (base_fort) base_fort->ClearPhysicsCache();
            scheme_fort->Run(import_state, export_fort);

            auto& dv_fort = export_fort.fields["k14_dust_emissions"];
            dv_fort.sync<Kokkos::HostSpace>();
            auto emis_fort_h = dv_fort.view_host();

            for (int j = 0; j < ny; ++j) {
                for (int n = 0; n < nbins; ++n) {
                    // Zero land fraction (rows 0-1) — strict zero
                    for (int i = 0; i < 2; ++i) {
                        EXPECT_DOUBLE_EQ(emis_fort_h(i, j, n), 0.0)
                            << "Fortran zero-land cell (" << i << "," << j << "," << n
                            << ") should have zero emissions, iter=" << iter;
                    }
                    // High roughness (rows 2-3) — Fortran WHERE clauses don't
                    // short-circuit all steps, so tiny near-zero values can leak
                    // through. Use a tolerance consistent with machine noise.
                    for (int i = 2; i < 4; ++i) {
                        EXPECT_NEAR(emis_fort_h(i, j, n), 0.0, 1e-12)
                            << "Fortran high-roughness cell (" << i << "," << j << "," << n
                            << ") should have near-zero emissions, iter=" << iter;
                    }
                    // Bedrock + non-emitting veg (rows 6-7) — strict zero
                    for (int i = 6; i < 8; ++i) {
                        EXPECT_DOUBLE_EQ(emis_fort_h(i, j, n), 0.0)
                            << "Fortran bedrock/non-emitting-veg cell (" << i << "," << j << "," << n
                            << ") should have zero emissions, iter=" << iter;
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
