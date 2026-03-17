#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <vector>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"

namespace aces {

/**
 * @brief FieldResolver implementation for vertical distribution round-trip testing.
 */
class VerticalDistributionRoundTripFieldResolver : public FieldResolver {
    std::map<std::string, DualView3D> fields;

   public:
    void AddField(const std::string& name, int nx, int ny, int nz) {
        fields[name] = DualView3D("test_" + name, nx, ny, nz);
    }

    void SetValue(const std::string& name, double val) {
        auto host = fields[name].view_host();
        Kokkos::deep_copy(host, val);
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }

    void SetValue(const std::string& name, int i, int j, int k, double val) {
        auto host = fields[name].view_host();
        host(i, j, k) = val;
        fields[name].modify<Kokkos::HostSpace>();
        fields[name].sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }

    double GetValue(const std::string& name, int i, int j, int k) {
        fields[name].sync<Kokkos::HostSpace>();
        return fields[name].view_host()(i, j, k);
    }

    UnmanagedHostView3D ResolveImport(const std::string& name, int, int, int) override {
        return fields[name].view_host();
    }
    UnmanagedHostView3D ResolveExport(const std::string& name, int, int, int) override {
        return fields[name].view_host();
    }
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveImportDevice(const std::string& name, int, int, int) override {
        return fields[name].view_device();
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(
        const std::string& name, int, int, int) override {
        return fields[name].view_device();
    }
};

/**
 * @brief Property-based test suite for vertical distribution round-trip.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 *
 * Property 15: Vertical Distribution Round-Trip
 * Validates: Requirements 6.24
 *
 * FOR ALL vertical distribution methods and 2D emission fields, distributing
 * to 3D then summing vertically SHALL recover the original 2D field within
 * 1e-10 relative error.
 */
class VerticalDistributionRoundTripPropertyTest : public ::testing::Test {
   protected:
    std::mt19937 rng{42};  // Deterministic seed for reproducibility

    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }

    /**
     * @brief Generate random grid dimensions for testing.
     * @return Tuple of (nx, ny, nz)
     */
    std::tuple<int, int, int> GenerateRandomGridDimensions() {
        std::uniform_int_distribution<int> nx_dist(2, 20);
        std::uniform_int_distribution<int> ny_dist(2, 20);
        std::uniform_int_distribution<int> nz_dist(5, 50);

        return {nx_dist(rng), ny_dist(rng), nz_dist(rng)};
    }

    /**
     * @brief Generate random 2D emission field.
     * @param nx X dimension
     * @param ny Y dimension
     * @return Vector of emission values
     */
    std::vector<double> GenerateRandom2DEmissions(int nx, int ny) {
        std::uniform_real_distribution<double> emis_dist(0.1, 100.0);
        std::vector<double> emissions(nx * ny);

        for (int i = 0; i < nx * ny; ++i) {
            emissions[i] = emis_dist(rng);
        }

        return emissions;
    }

    /**
     * @brief Generate random layer index within bounds.
     * @param nz Number of vertical layers
     * @return Layer index in range [0, nz-1]
     */
    int GenerateRandomLayer(int nz) {
        std::uniform_int_distribution<int> layer_dist(0, nz - 1);
        return layer_dist(rng);
    }

    /**
     * @brief Generate random layer range.
     * @param nz Number of vertical layers
     * @return Pair of (start_layer, end_layer) with start <= end
     */
    std::pair<int, int> GenerateRandomLayerRange(int nz) {
        std::uniform_int_distribution<int> layer_dist(0, nz - 1);
        int start = layer_dist(rng);
        int end = layer_dist(rng);
        if (start > end) std::swap(start, end);
        return {start, end};
    }

    /**
     * @brief Sum 3D field vertically to recover 2D field.
     * @param resolver Field resolver
     * @param field_name Name of 3D field
     * @param nx X dimension
     * @param ny Y dimension
     * @param nz Z dimension
     * @return Vector of vertically summed values
     */
    std::vector<double> SumVertically(VerticalDistributionRoundTripFieldResolver& resolver,
                                      const std::string& field_name, int nx, int ny, int nz) {
        std::vector<double> result(nx * ny, 0.0);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                for (int k = 0; k < nz; ++k) {
                    result[i * ny + j] += resolver.GetValue(field_name, i, j, k);
                }
            }
        }
        return result;
    }

    /**
     * @brief Compute relative error between two 2D fields.
     * @param original Original field values
     * @param recovered Recovered field values
     * @return Maximum relative error
     */
    double ComputeMaxRelativeError(const std::vector<double>& original,
                                   const std::vector<double>& recovered) {
        double max_rel_error = 0.0;
        for (size_t i = 0; i < original.size(); ++i) {
            if (original[i] != 0.0) {
                double rel_error = std::abs(recovered[i] - original[i]) / std::abs(original[i]);
                max_rel_error = std::max(max_rel_error, rel_error);
            }
        }
        return max_rel_error;
    }

    /**
     * @brief Set up MPAS-style height coordinate fields for vertical distribution tests.
     *
     * Populates z_coord (nx x ny x nz) with monotonically decreasing heights
     * from ~20000 m at k=0 to ~0 m at k=nz-1, and pbl_height (nx x ny x 1)
     * with random values in [500, 2000] m.  Also configures vertical_config on
     * the supplied AcesConfig to use VerticalCoordType::MPAS so the stacking
     * engine will bind and use these fields.
     *
     * @param config   AcesConfig to configure (vertical_config is mutated).
     * @param resolver Field resolver to add fields to.
     * @param nx       X dimension.
     * @param ny       Y dimension.
     * @param nz       Z dimension.
     */
    void SetupMPASCoords(AcesConfig& config, VerticalDistributionRoundTripFieldResolver& resolver,
                         int nx, int ny, int nz) {
        config.vertical_config.type = VerticalCoordType::MPAS;
        config.vertical_config.z_field = "height";
        config.vertical_config.pbl_field = "pbl_height";
        config.vertical_config.p_surf_field = "ps";

        // z_coord: layer mid-point heights, decreasing from top (k=0) to surface (k=nz-1)
        // Use nz+1 interface heights so the engine can access z_coord(i,j,k+1) safely.
        // The engine accesses z_coord(i,j,k) and z_coord(i,j,k+1) for HEIGHT/PBL.
        // We store nz+1 levels but the field is declared with extent nz; the engine
        // only accesses indices 0..nz-1 for k and k+1 up to nz-1+1=nz.
        // To be safe, allocate nz+1 in z dimension.
        resolver.AddField("height", nx, ny, nz + 1);
        double z_top = 20000.0;
        double dz = z_top / nz;
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                for (int k = 0; k <= nz; ++k) {
                    // Interface heights: z_top at k=0, 0 at k=nz
                    resolver.SetValue("height", i, j, k, z_top - k * dz);
                }
            }
        }

        // Surface pressure (not used for MPAS pressure calc but bound anyway)
        resolver.AddField("ps", nx, ny, 1);
        resolver.SetValue("ps", 101325.0);

        // PBL height
        resolver.AddField("pbl_height", nx, ny, 1);
        std::uniform_real_distribution<double> pbl_dist(500.0, 2000.0);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("pbl_height", i, j, 0, pbl_dist(rng));
            }
        }
    }
};

/**
 * @test Property 15: SINGLE vertical distribution round-trip.
 *
 * FOR ALL random 2D emission fields and layer indices, distributing to a
 * single layer then summing vertically SHALL recover the original 2D field
 * within 1e-10 relative error.
 *
 * Iterations: 25 (different grid sizes and layer indices)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, SingleMethodRoundTrip) {
    for (int iteration = 0; iteration < 25; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto emissions_2d = GenerateRandom2DEmissions(nx, ny);
        int layer = GenerateRandomLayer(nz);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = VerticalDistributionMethod::SINGLE;
        layer_config.vdist_layer_start = layer;
        layer_config.vdist_layer_end = layer;

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set 2D emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions_2d[i * ny + j]);
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(emissions_2d, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10)
            << "Iteration " << iteration << ": Grid (" << nx << "," << ny << "," << nz
            << "), Layer " << layer << ", Max Rel Error: " << max_rel_error;
    }
}

/**
 * @test Property 15: RANGE vertical distribution round-trip.
 *
 * FOR ALL random 2D emission fields and layer ranges, distributing evenly
 * over a range then summing vertically SHALL recover the original 2D field
 * within 1e-10 relative error.
 *
 * Iterations: 25 (different grid sizes and layer ranges)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, RangeMethodRoundTrip) {
    for (int iteration = 0; iteration < 25; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto emissions_2d = GenerateRandom2DEmissions(nx, ny);
        auto [start_layer, end_layer] = GenerateRandomLayerRange(nz);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = VerticalDistributionMethod::RANGE;
        layer_config.vdist_layer_start = start_layer;
        layer_config.vdist_layer_end = end_layer;

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set 2D emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions_2d[i * ny + j]);
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(emissions_2d, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10) << "Iteration " << iteration << ": Grid (" << nx << ","
                                        << ny << "," << nz << "), Range [" << start_layer << ","
                                        << end_layer << "], Max Rel Error: " << max_rel_error;
    }
}

/**
 * @test Property 15: PRESSURE vertical distribution round-trip.
 *
 * FOR ALL random 2D emission fields and pressure ranges, distributing based
 * on pressure overlap then summing vertically SHALL recover the original 2D
 * field within 1e-10 relative error.
 *
 * Iterations: 20 (different grid sizes and pressure ranges)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, PressureMethodRoundTrip) {
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto emissions_2d = GenerateRandom2DEmissions(nx, ny);

        // Generate random pressure range (Pa) within the hydrostatic range
        // z_coord goes from 20000 m (k=0) to 0 m (k=nz), so pressure goes from
        // ~9000 Pa at top to ~101325 Pa at surface.
        std::uniform_real_distribution<double> pressure_dist(9000.0, 101325.0);
        double p_start = pressure_dist(rng);
        double p_end = pressure_dist(rng);
        if (p_start > p_end) std::swap(p_start, p_end);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = VerticalDistributionMethod::PRESSURE;
        layer_config.vdist_p_start = p_start;
        layer_config.vdist_p_end = p_end;

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);
        SetupMPASCoords(config, resolver, nx, ny, nz);

        // Set 2D emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions_2d[i * ny + j]);
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(emissions_2d, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10)
            << "Iteration " << iteration << ": Grid (" << nx << "," << ny << "," << nz
            << "), Pressure [" << p_start << "," << p_end << "], Max Rel Error: " << max_rel_error;
    }
}

/**
 * @test Property 15: HEIGHT vertical distribution round-trip.
 *
 * FOR ALL random 2D emission fields and height ranges, distributing based
 * on height overlap then summing vertically SHALL recover the original 2D
 * field within 1e-10 relative error.
 *
 * Iterations: 20 (different grid sizes and height ranges)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, HeightMethodRoundTrip) {
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto emissions_2d = GenerateRandom2DEmissions(nx, ny);

        // Generate random height range (m) within the coordinate range [0, 20000]
        std::uniform_real_distribution<double> height_dist(0.0, 20000.0);
        double h_start = height_dist(rng);
        double h_end = height_dist(rng);
        if (h_start > h_end) std::swap(h_start, h_end);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = VerticalDistributionMethod::HEIGHT;
        layer_config.vdist_h_start = h_start;
        layer_config.vdist_h_end = h_end;

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);
        SetupMPASCoords(config, resolver, nx, ny, nz);

        // Set 2D emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions_2d[i * ny + j]);
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(emissions_2d, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10)
            << "Iteration " << iteration << ": Grid (" << nx << "," << ny << "," << nz
            << "), Height [" << h_start << "," << h_end << "], Max Rel Error: " << max_rel_error;
    }
}

/**
 * @test Property 15: PBL vertical distribution round-trip.
 *
 * FOR ALL random 2D emission fields and PBL heights, distributing within
 * the planetary boundary layer then summing vertically SHALL recover the
 * original 2D field within 1e-10 relative error.
 *
 * Iterations: 20 (different grid sizes and PBL configurations)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, PBLMethodRoundTrip) {
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto emissions_2d = GenerateRandom2DEmissions(nx, ny);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = VerticalDistributionMethod::PBL;

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);
        // SetupMPASCoords sets vertical_config, z_coord, pbl_height, and ps
        SetupMPASCoords(config, resolver, nx, ny, nz);

        // Set 2D emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions_2d[i * ny + j]);
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(emissions_2d, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10)
            << "Iteration " << iteration << ": Grid (" << nx << "," << ny << "," << nz
            << "), PBL, Max Rel Error: " << max_rel_error;
    }
}

/**
 * @test Property 15: All methods with scale factors.
 *
 * FOR ALL vertical distribution methods with random scale factors, the
 * round-trip property SHALL hold within 1e-10 relative error.
 *
 * Iterations: 20 (different methods and scale factors)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, AllMethodsWithScaleFactors) {
    std::vector<VerticalDistributionMethod> methods = {
        VerticalDistributionMethod::SINGLE, VerticalDistributionMethod::RANGE,
        VerticalDistributionMethod::PRESSURE, VerticalDistributionMethod::HEIGHT,
        VerticalDistributionMethod::PBL};

    for (int iteration = 0; iteration < 20; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto emissions_2d = GenerateRandom2DEmissions(nx, ny);

        // Select random method
        std::uniform_int_distribution<int> method_dist(0, methods.size() - 1);
        auto method = methods[method_dist(rng)];

        // Generate random scale factor
        std::uniform_real_distribution<double> scale_dist(0.1, 10.0);
        double scale = scale_dist(rng);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = scale;
        layer_config.vdist_method = method;

        // Configure method-specific parameters
        if (method == VerticalDistributionMethod::SINGLE) {
            layer_config.vdist_layer_start = GenerateRandomLayer(nz);
            layer_config.vdist_layer_end = layer_config.vdist_layer_start;
        } else if (method == VerticalDistributionMethod::RANGE) {
            auto [start, end] = GenerateRandomLayerRange(nz);
            layer_config.vdist_layer_start = start;
            layer_config.vdist_layer_end = end;
        } else if (method == VerticalDistributionMethod::PRESSURE) {
            std::uniform_real_distribution<double> pressure_dist(10000.0, 100000.0);
            double p_start = pressure_dist(rng);
            double p_end = pressure_dist(rng);
            if (p_start > p_end) std::swap(p_start, p_end);
            layer_config.vdist_p_start = p_start;
            layer_config.vdist_p_end = p_end;
        } else if (method == VerticalDistributionMethod::HEIGHT) {
            std::uniform_real_distribution<double> height_dist(0.0, 20000.0);
            double h_start = height_dist(rng);
            double h_end = height_dist(rng);
            if (h_start > h_end) std::swap(h_start, h_end);
            layer_config.vdist_h_start = h_start;
            layer_config.vdist_h_end = h_end;
        }

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set 2D emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions_2d[i * ny + j]);
            }
        }

        // Set up coordinate fields for methods that need them
        if (method == VerticalDistributionMethod::PRESSURE ||
            method == VerticalDistributionMethod::HEIGHT ||
            method == VerticalDistributionMethod::PBL) {
            SetupMPASCoords(config, resolver, nx, ny, nz);
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Scale original emissions by scale factor for comparison
        std::vector<double> scaled_emissions(emissions_2d.size());
        for (size_t i = 0; i < emissions_2d.size(); ++i) {
            scaled_emissions[i] = emissions_2d[i] * scale;
        }

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(scaled_emissions, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10)
            << "Iteration " << iteration << ": Grid (" << nx << "," << ny << "," << nz
            << "), Method " << static_cast<int>(method) << ", Scale " << scale
            << ", Max Rel Error: " << max_rel_error;
    }
}

/**
 * @test Property 15: Large grid round-trip.
 *
 * FOR ALL large grids (up to 360x180x72), the round-trip property SHALL
 * hold within 1e-10 relative error for all vertical distribution methods.
 *
 * Iterations: 10 (large grids with various methods)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, LargeGridRoundTrip) {
    std::vector<VerticalDistributionMethod> methods = {
        VerticalDistributionMethod::SINGLE, VerticalDistributionMethod::RANGE,
        VerticalDistributionMethod::PRESSURE, VerticalDistributionMethod::HEIGHT,
        VerticalDistributionMethod::PBL};

    for (int iteration = 0; iteration < 10; ++iteration) {
        std::uniform_int_distribution<int> nx_dist(100, 360);
        std::uniform_int_distribution<int> ny_dist(100, 180);
        std::uniform_int_distribution<int> nz_dist(30, 72);

        int nx = nx_dist(rng);
        int ny = ny_dist(rng);
        int nz = nz_dist(rng);

        auto emissions_2d = GenerateRandom2DEmissions(nx, ny);

        // Select random method
        std::uniform_int_distribution<int> method_dist(0, methods.size() - 1);
        auto method = methods[method_dist(rng)];

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = method;

        // Configure method-specific parameters
        if (method == VerticalDistributionMethod::SINGLE) {
            layer_config.vdist_layer_start = GenerateRandomLayer(nz);
            layer_config.vdist_layer_end = layer_config.vdist_layer_start;
        } else if (method == VerticalDistributionMethod::RANGE) {
            auto [start, end] = GenerateRandomLayerRange(nz);
            layer_config.vdist_layer_start = start;
            layer_config.vdist_layer_end = end;
        } else if (method == VerticalDistributionMethod::PRESSURE) {
            std::uniform_real_distribution<double> pressure_dist(10000.0, 100000.0);
            double p_start = pressure_dist(rng);
            double p_end = pressure_dist(rng);
            if (p_start > p_end) std::swap(p_start, p_end);
            layer_config.vdist_p_start = p_start;
            layer_config.vdist_p_end = p_end;
        } else if (method == VerticalDistributionMethod::HEIGHT) {
            std::uniform_real_distribution<double> height_dist(0.0, 20000.0);
            double h_start = height_dist(rng);
            double h_end = height_dist(rng);
            if (h_start > h_end) std::swap(h_start, h_end);
            layer_config.vdist_h_start = h_start;
            layer_config.vdist_h_end = h_end;
        }

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set 2D emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions_2d[i * ny + j]);
            }
        }

        // Set up coordinate fields for methods that need them
        if (method == VerticalDistributionMethod::PRESSURE ||
            method == VerticalDistributionMethod::HEIGHT ||
            method == VerticalDistributionMethod::PBL) {
            SetupMPASCoords(config, resolver, nx, ny, nz);
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(emissions_2d, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10)
            << "Iteration " << iteration << ": Large Grid (" << nx << "," << ny << "," << nz
            << "), Method " << static_cast<int>(method) << ", Max Rel Error: " << max_rel_error;
    }
}

/**
 * @test Property 15: Zero emissions round-trip.
 *
 * FOR ALL vertical distribution methods with zero emissions, the round-trip
 * property SHALL hold (recovered field should be zero).
 *
 * Iterations: 10 (different methods and grid sizes)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, ZeroEmissionsRoundTrip) {
    std::vector<VerticalDistributionMethod> methods = {
        VerticalDistributionMethod::SINGLE, VerticalDistributionMethod::RANGE,
        VerticalDistributionMethod::PRESSURE, VerticalDistributionMethod::HEIGHT,
        VerticalDistributionMethod::PBL};

    for (int iteration = 0; iteration < 10; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();

        // Select random method
        std::uniform_int_distribution<int> method_dist(0, methods.size() - 1);
        auto method = methods[method_dist(rng)];

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = method;

        // Configure method-specific parameters
        if (method == VerticalDistributionMethod::SINGLE) {
            layer_config.vdist_layer_start = GenerateRandomLayer(nz);
            layer_config.vdist_layer_end = layer_config.vdist_layer_start;
        } else if (method == VerticalDistributionMethod::RANGE) {
            auto [start, end] = GenerateRandomLayerRange(nz);
            layer_config.vdist_layer_start = start;
            layer_config.vdist_layer_end = end;
        } else if (method == VerticalDistributionMethod::PRESSURE) {
            std::uniform_real_distribution<double> pressure_dist(10000.0, 100000.0);
            double p_start = pressure_dist(rng);
            double p_end = pressure_dist(rng);
            if (p_start > p_end) std::swap(p_start, p_end);
            layer_config.vdist_p_start = p_start;
            layer_config.vdist_p_end = p_end;
        } else if (method == VerticalDistributionMethod::HEIGHT) {
            std::uniform_real_distribution<double> height_dist(0.0, 20000.0);
            double h_start = height_dist(rng);
            double h_end = height_dist(rng);
            if (h_start > h_end) std::swap(h_start, h_end);
            layer_config.vdist_h_start = h_start;
            layer_config.vdist_h_end = h_end;
        }

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.SetValue("emissions_2d", 0.0);  // All zeros
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set up coordinate fields for methods that need them
        if (method == VerticalDistributionMethod::PRESSURE ||
            method == VerticalDistributionMethod::HEIGHT ||
            method == VerticalDistributionMethod::PBL) {
            SetupMPASCoords(config, resolver, nx, ny, nz);
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify all recovered values are zero
        for (size_t i = 0; i < recovered_2d.size(); ++i) {
            EXPECT_NEAR(recovered_2d[i], 0.0, 1e-15)
                << "Iteration " << iteration << ": Grid (" << nx << "," << ny << "," << nz
                << "), Method " << static_cast<int>(method) << ", Index " << i;
        }
    }
}

/**
 * @test Property 15: Very small emissions round-trip.
 *
 * FOR ALL vertical distribution methods with very small emissions (1e-10 to
 * 1e-5), the round-trip property SHALL hold within 1e-10 relative error.
 *
 * Iterations: 10 (different methods with small values)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, SmallEmissionsRoundTrip) {
    std::vector<VerticalDistributionMethod> methods = {
        VerticalDistributionMethod::SINGLE, VerticalDistributionMethod::RANGE,
        VerticalDistributionMethod::PRESSURE, VerticalDistributionMethod::HEIGHT,
        VerticalDistributionMethod::PBL};

    for (int iteration = 0; iteration < 10; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();

        // Select random method
        std::uniform_int_distribution<int> method_dist(0, methods.size() - 1);
        auto method = methods[method_dist(rng)];

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = method;

        // Configure method-specific parameters
        if (method == VerticalDistributionMethod::SINGLE) {
            layer_config.vdist_layer_start = GenerateRandomLayer(nz);
            layer_config.vdist_layer_end = layer_config.vdist_layer_start;
        } else if (method == VerticalDistributionMethod::RANGE) {
            auto [start, end] = GenerateRandomLayerRange(nz);
            layer_config.vdist_layer_start = start;
            layer_config.vdist_layer_end = end;
        } else if (method == VerticalDistributionMethod::PRESSURE) {
            std::uniform_real_distribution<double> pressure_dist(10000.0, 100000.0);
            double p_start = pressure_dist(rng);
            double p_end = pressure_dist(rng);
            if (p_start > p_end) std::swap(p_start, p_end);
            layer_config.vdist_p_start = p_start;
            layer_config.vdist_p_end = p_end;
        } else if (method == VerticalDistributionMethod::HEIGHT) {
            std::uniform_real_distribution<double> height_dist(0.0, 20000.0);
            double h_start = height_dist(rng);
            double h_end = height_dist(rng);
            if (h_start > h_end) std::swap(h_start, h_end);
            layer_config.vdist_h_start = h_start;
            layer_config.vdist_h_end = h_end;
        }

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set very small emissions
        std::uniform_real_distribution<double> small_emis_dist(1e-10, 1e-5);
        std::vector<double> emissions_2d(nx * ny);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                double val = small_emis_dist(rng);
                emissions_2d[i * ny + j] = val;
                resolver.SetValue("emissions_2d", i, j, 0, val);
            }
        }

        // Set up coordinate fields for methods that need them
        if (method == VerticalDistributionMethod::PRESSURE ||
            method == VerticalDistributionMethod::HEIGHT ||
            method == VerticalDistributionMethod::PBL) {
            SetupMPASCoords(config, resolver, nx, ny, nz);
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(emissions_2d, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10)
            << "Iteration " << iteration << ": Grid (" << nx << "," << ny << "," << nz
            << "), Method " << static_cast<int>(method) << ", Small Emissions"
            << ", Max Rel Error: " << max_rel_error;
    }
}

/**
 * @test Property 15: Very large emissions round-trip.
 *
 * FOR ALL vertical distribution methods with very large emissions (1e6 to
 * 1e12), the round-trip property SHALL hold within 1e-10 relative error.
 *
 * Iterations: 10 (different methods with large values)
 */
TEST_F(VerticalDistributionRoundTripPropertyTest, LargeEmissionsRoundTrip) {
    std::vector<VerticalDistributionMethod> methods = {
        VerticalDistributionMethod::SINGLE, VerticalDistributionMethod::RANGE,
        VerticalDistributionMethod::PRESSURE, VerticalDistributionMethod::HEIGHT,
        VerticalDistributionMethod::PBL};

    for (int iteration = 0; iteration < 10; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();

        // Select random method
        std::uniform_int_distribution<int> method_dist(0, methods.size() - 1);
        auto method = methods[method_dist(rng)];

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = method;

        // Configure method-specific parameters
        if (method == VerticalDistributionMethod::SINGLE) {
            layer_config.vdist_layer_start = GenerateRandomLayer(nz);
            layer_config.vdist_layer_end = layer_config.vdist_layer_start;
        } else if (method == VerticalDistributionMethod::RANGE) {
            auto [start, end] = GenerateRandomLayerRange(nz);
            layer_config.vdist_layer_start = start;
            layer_config.vdist_layer_end = end;
        } else if (method == VerticalDistributionMethod::PRESSURE) {
            std::uniform_real_distribution<double> pressure_dist(10000.0, 100000.0);
            double p_start = pressure_dist(rng);
            double p_end = pressure_dist(rng);
            if (p_start > p_end) std::swap(p_start, p_end);
            layer_config.vdist_p_start = p_start;
            layer_config.vdist_p_end = p_end;
        } else if (method == VerticalDistributionMethod::HEIGHT) {
            std::uniform_real_distribution<double> height_dist(0.0, 20000.0);
            double h_start = height_dist(rng);
            double h_end = height_dist(rng);
            if (h_start > h_end) std::swap(h_start, h_end);
            layer_config.vdist_h_start = h_start;
            layer_config.vdist_h_end = h_end;
        }

        config.species_layers["TestSpecies"] = {layer_config};

        VerticalDistributionRoundTripFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set very large emissions
        std::uniform_real_distribution<double> large_emis_dist(1e6, 1e12);
        std::vector<double> emissions_2d(nx * ny);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                double val = large_emis_dist(rng);
                emissions_2d[i * ny + j] = val;
                resolver.SetValue("emissions_2d", i, j, 0, val);
            }
        }

        // Set up coordinate fields for methods that need them
        if (method == VerticalDistributionMethod::PRESSURE ||
            method == VerticalDistributionMethod::HEIGHT ||
            method == VerticalDistributionMethod::PBL) {
            SetupMPASCoords(config, resolver, nx, ny, nz);
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Sum vertically to recover 2D field
        auto recovered_2d = SumVertically(resolver, "TestSpecies", nx, ny, nz);

        // Verify round-trip
        double max_rel_error = ComputeMaxRelativeError(emissions_2d, recovered_2d);

        EXPECT_LT(max_rel_error, 1e-10)
            << "Iteration " << iteration << ": Grid (" << nx << "," << ny << "," << nz
            << "), Method " << static_cast<int>(method) << ", Large Emissions"
            << ", Max Rel Error: " << max_rel_error;
    }
}

}  // namespace aces
