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
 * @brief FieldResolver implementation for mass conservation property testing.
 */
class MassConservationFieldResolver : public FieldResolver {
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
 * @brief Property-based test suite for mass conservation invariant.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 *
 * Property 14: Mass Conservation Invariant
 * Validates: Requirements 6.23
 *
 * FOR ALL emission species and layer configurations, the total column mass
 * before and after StackingEngine execution SHALL be equal within 1e-10
 * relative error.
 */
class MassConservationPropertyTest : public ::testing::Test {
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
     * @brief Generate random emission values for a 2D field.
     * @param nx X dimension
     * @param ny Y dimension
     * @return Vector of emission values and their sum
     */
    std::pair<std::vector<double>, double> GenerateRandomEmissions(int nx, int ny) {
        std::uniform_real_distribution<double> emis_dist(0.1, 100.0);
        std::vector<double> emissions(nx * ny);
        double total = 0.0;

        for (int i = 0; i < nx * ny; ++i) {
            emissions[i] = emis_dist(rng);
            total += emissions[i];
        }

        return {emissions, total};
    }

    /**
     * @brief Generate random scale factor.
     * @return Scale factor in range [0.1, 10.0]
     */
    double GenerateRandomScale() {
        std::uniform_real_distribution<double> scale_dist(0.1, 10.0);
        return scale_dist(rng);
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
     * @brief Compute total column mass from 3D field.
     * @param resolver Field resolver
     * @param field_name Name of field to sum
     * @param nx X dimension
     * @param ny Y dimension
     * @param nz Z dimension
     * @return Total column mass
     */
    double ComputeTotalColumnMass(MassConservationFieldResolver& resolver,
                                  const std::string& field_name, int nx, int ny, int nz) {
        double total = 0.0;
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                for (int k = 0; k < nz; ++k) {
                    total += resolver.GetValue(field_name, i, j, k);
                }
            }
        }
        return total;
    }

    /**
     * @brief Compute total column mass from 2D field.
     * @param resolver Field resolver
     * @param field_name Name of field to sum
     * @param nx X dimension
     * @param ny Y dimension
     * @return Total mass
     */
    double ComputeTotalMass2D(MassConservationFieldResolver& resolver,
                              const std::string& field_name, int nx, int ny) {
        double total = 0.0;
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                total += resolver.GetValue(field_name, i, j, 0);
            }
        }
        return total;
    }
};

/**
 * @test Property 14: SINGLE vertical distribution mass conservation.
 *
 * FOR ALL random grid sizes and emission fields, distributing to a single
 * layer SHALL conserve total column mass within 1e-10 relative error.
 *
 * Iterations: 20 (different grid sizes and layer indices)
 */
TEST_F(MassConservationPropertyTest, SingleMethodMassConservation) {
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto [emissions, total_2d] = GenerateRandomEmissions(nx, ny);
        double scale = GenerateRandomScale();
        int layer = GenerateRandomLayer(nz);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = scale;
        layer_config.vdist_method = VerticalDistributionMethod::SINGLE;
        layer_config.vdist_layer_start = layer;
        layer_config.vdist_layer_end = layer;

        config.species_layers["TestSpecies"] = {layer_config};

        MassConservationFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions[i * ny + j]);
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify mass conservation
        double total_3d = ComputeTotalColumnMass(resolver, "TestSpecies", nx, ny, nz);
        double expected = scale * total_2d;
        double rel_error = std::abs(total_3d - expected) / expected;

        EXPECT_LT(rel_error, 1e-10) << "Iteration " << iteration << ": Grid (" << nx << "," << ny
                                     << "," << nz << "), Layer " << layer << ", Scale " << scale
                                     << ", Rel Error: " << rel_error;
    }
}

/**
 * @test Property 14: RANGE vertical distribution mass conservation.
 *
 * FOR ALL random grid sizes and layer ranges, distributing evenly over a
 * range SHALL conserve total column mass within 1e-10 relative error.
 *
 * Iterations: 20 (different grid sizes and layer ranges)
 */
TEST_F(MassConservationPropertyTest, RangeMethodMassConservation) {
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto [emissions, total_2d] = GenerateRandomEmissions(nx, ny);
        double scale = GenerateRandomScale();
        auto [start_layer, end_layer] = GenerateRandomLayerRange(nz);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = scale;
        layer_config.vdist_method = VerticalDistributionMethod::RANGE;
        layer_config.vdist_layer_start = start_layer;
        layer_config.vdist_layer_end = end_layer;

        config.species_layers["TestSpecies"] = {layer_config};

        MassConservationFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions[i * ny + j]);
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify mass conservation
        double total_3d = ComputeTotalColumnMass(resolver, "TestSpecies", nx, ny, nz);
        double expected = scale * total_2d;
        double rel_error = std::abs(total_3d - expected) / expected;

        EXPECT_LT(rel_error, 1e-10) << "Iteration " << iteration << ": Grid (" << nx << "," << ny
                                     << "," << nz << "), Range [" << start_layer << ","
                                     << end_layer << "], Scale " << scale
                                     << ", Rel Error: " << rel_error;
    }
}

/**
 * @test Property 14: Multiple layers with add operation mass conservation.
 *
 * FOR ALL random configurations with multiple layers using add operation,
 * the total column mass SHALL equal the sum of all layer contributions
 * within 1e-10 relative error.
 *
 * Iterations: 15 (different grid sizes and layer configurations)
 */
TEST_F(MassConservationPropertyTest, MultipleLayersAddMassConservation) {
    for (int iteration = 0; iteration < 15; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();

        AcesConfig config;
        double expected_total = 0.0;

        // Create 2-3 layers with add operation
        std::uniform_int_distribution<int> num_layers_dist(2, 3);
        int num_layers = num_layers_dist(rng);

        MassConservationFieldResolver resolver;
        resolver.AddField("TestSpecies", nx, ny, nz);

        for (int layer_idx = 0; layer_idx < num_layers; ++layer_idx) {
            auto [emissions, total_2d] = GenerateRandomEmissions(nx, ny);
            double scale = GenerateRandomScale();

            std::string field_name = "emissions_" + std::to_string(layer_idx);
            resolver.AddField(field_name, nx, ny, 1);
            for (int i = 0; i < nx; ++i) {
                for (int j = 0; j < ny; ++j) {
                    resolver.SetValue(field_name, i, j, 0, emissions[i * ny + j]);
                }
            }

            EmissionLayer layer_config;
            layer_config.operation = "add";
            layer_config.field_name = field_name;
            layer_config.hierarchy = layer_idx;
            layer_config.scale = scale;
            layer_config.vdist_method = VerticalDistributionMethod::SINGLE;
            layer_config.vdist_layer_start = GenerateRandomLayer(nz);
            layer_config.vdist_layer_end = layer_config.vdist_layer_start;

            config.species_layers["TestSpecies"].push_back(layer_config);
            expected_total += scale * total_2d;
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify mass conservation
        double total_3d = ComputeTotalColumnMass(resolver, "TestSpecies", nx, ny, nz);
        double rel_error = std::abs(total_3d - expected_total) / expected_total;

        EXPECT_LT(rel_error, 1e-10) << "Iteration " << iteration << ": Grid (" << nx << "," << ny
                                     << "," << nz << "), Layers " << num_layers
                                     << ", Rel Error: " << rel_error;
    }
}

/**
 * @test Property 14: Multiple layers with replace operation mass conservation.
 *
 * FOR ALL random configurations with multiple layers using replace operation,
 * the total column mass SHALL equal the highest-hierarchy layer's contribution
 * within 1e-10 relative error.
 *
 * Iterations: 15 (different grid sizes and layer configurations)
 */
TEST_F(MassConservationPropertyTest, MultipleLayersReplaceMassConservation) {
    for (int iteration = 0; iteration < 15; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();

        AcesConfig config;
        double expected_total = 0.0;

        // Create 2-3 layers with replace operation
        std::uniform_int_distribution<int> num_layers_dist(2, 3);
        int num_layers = num_layers_dist(rng);

        MassConservationFieldResolver resolver;
        resolver.AddField("TestSpecies", nx, ny, nz);

        for (int layer_idx = 0; layer_idx < num_layers; ++layer_idx) {
            auto [emissions, total_2d] = GenerateRandomEmissions(nx, ny);
            double scale = GenerateRandomScale();

            std::string field_name = "emissions_" + std::to_string(layer_idx);
            resolver.AddField(field_name, nx, ny, 1);
            for (int i = 0; i < nx; ++i) {
                for (int j = 0; j < ny; ++j) {
                    resolver.SetValue(field_name, i, j, 0, emissions[i * ny + j]);
                }
            }

            EmissionLayer layer_config;
            layer_config.operation = (layer_idx == 0) ? "add" : "replace";
            layer_config.field_name = field_name;
            layer_config.hierarchy = layer_idx;
            layer_config.scale = scale;
            layer_config.vdist_method = VerticalDistributionMethod::SINGLE;
            layer_config.vdist_layer_start = 0; // Fixed layer for all to ensure replacement
            layer_config.vdist_layer_end = 0;

            config.species_layers["TestSpecies"].push_back(layer_config);

            // Only the last (highest hierarchy) layer contributes
            if (layer_idx == num_layers - 1) {
                expected_total = scale * total_2d;
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify mass conservation
        double total_3d = ComputeTotalColumnMass(resolver, "TestSpecies", nx, ny, nz);
        double rel_error = std::abs(total_3d - expected_total) / expected_total;

        EXPECT_LT(rel_error, 1e-10) << "Iteration " << iteration << ": Grid (" << nx << "," << ny
                                     << "," << nz << "), Layers " << num_layers
                                     << ", Rel Error: " << rel_error;
    }
}

/**
 * @test Property 14: Large grid mass conservation.
 *
 * FOR ALL large grids (up to 360x180x72), mass conservation SHALL hold
 * within 1e-10 relative error.
 *
 * Iterations: 10 (large grids with various configurations)
 */
TEST_F(MassConservationPropertyTest, LargeGridMassConservation) {
    for (int iteration = 0; iteration < 10; ++iteration) {
        std::uniform_int_distribution<int> nx_dist(100, 360);
        std::uniform_int_distribution<int> ny_dist(100, 180);
        std::uniform_int_distribution<int> nz_dist(30, 72);

        int nx = nx_dist(rng);
        int ny = ny_dist(rng);
        int nz = nz_dist(rng);

        auto [emissions, total_2d] = GenerateRandomEmissions(nx, ny);
        double scale = GenerateRandomScale();
        int layer = GenerateRandomLayer(nz);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = scale;
        layer_config.vdist_method = VerticalDistributionMethod::SINGLE;
        layer_config.vdist_layer_start = layer;
        layer_config.vdist_layer_end = layer;

        config.species_layers["TestSpecies"] = {layer_config};

        MassConservationFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set emissions
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                resolver.SetValue("emissions_2d", i, j, 0, emissions[i * ny + j]);
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify mass conservation
        double total_3d = ComputeTotalColumnMass(resolver, "TestSpecies", nx, ny, nz);
        double expected = scale * total_2d;
        double rel_error = std::abs(total_3d - expected) / expected;

        EXPECT_LT(rel_error, 1e-10) << "Iteration " << iteration << ": Large Grid (" << nx << ","
                                     << ny << "," << nz << "), Layer " << layer << ", Scale "
                                     << scale << ", Rel Error: " << rel_error;
    }
}

/**
 * @test Property 14: Zero emissions mass conservation.
 *
 * FOR ALL configurations with zero emissions, the total column mass
 * SHALL remain zero within machine precision.
 *
 * Iterations: 10 (different grid sizes)
 */
TEST_F(MassConservationPropertyTest, ZeroEmissionsMassConservation) {
    for (int iteration = 0; iteration < 10; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        double scale = GenerateRandomScale();
        int layer = GenerateRandomLayer(nz);

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = scale;
        layer_config.vdist_method = VerticalDistributionMethod::SINGLE;
        layer_config.vdist_layer_start = layer;
        layer_config.vdist_layer_end = layer;

        config.species_layers["TestSpecies"] = {layer_config};

        MassConservationFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.SetValue("emissions_2d", 0.0);  // All zeros
        resolver.AddField("TestSpecies", nx, ny, nz);

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify mass is zero
        double total_3d = ComputeTotalColumnMass(resolver, "TestSpecies", nx, ny, nz);

        EXPECT_NEAR(total_3d, 0.0, 1e-15) << "Iteration " << iteration << ": Grid (" << nx << ","
                                           << ny << "," << nz << "), Total: " << total_3d;
    }
}

/**
 * @test Property 14: Very small emissions mass conservation.
 *
 * FOR ALL configurations with very small emissions (1e-10 to 1e-5),
 * mass conservation SHALL hold within 1e-10 relative error.
 *
 * Iterations: 10 (different grid sizes with small values)
 */
TEST_F(MassConservationPropertyTest, SmallEmissionsMassConservation) {
    for (int iteration = 0; iteration < 10; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = VerticalDistributionMethod::SINGLE;
        layer_config.vdist_layer_start = GenerateRandomLayer(nz);
        layer_config.vdist_layer_end = layer_config.vdist_layer_start;

        config.species_layers["TestSpecies"] = {layer_config};

        MassConservationFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set very small emissions
        std::uniform_real_distribution<double> small_emis_dist(1e-10, 1e-5);
        double total_2d = 0.0;
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                double val = small_emis_dist(rng);
                resolver.SetValue("emissions_2d", i, j, 0, val);
                total_2d += val;
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify mass conservation
        double total_3d = ComputeTotalColumnMass(resolver, "TestSpecies", nx, ny, nz);
        double rel_error = std::abs(total_3d - total_2d) / total_2d;

        EXPECT_LT(rel_error, 1e-10) << "Iteration " << iteration << ": Grid (" << nx << "," << ny
                                     << "," << nz << "), Small Emissions, Rel Error: " << rel_error;
    }
}

/**
 * @test Property 14: Very large emissions mass conservation.
 *
 * FOR ALL configurations with very large emissions (1e6 to 1e12),
 * mass conservation SHALL hold within 1e-10 relative error.
 *
 * Iterations: 10 (different grid sizes with large values)
 */
TEST_F(MassConservationPropertyTest, LargeEmissionsMassConservation) {
    for (int iteration = 0; iteration < 10; ++iteration) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();

        AcesConfig config;
        EmissionLayer layer_config;
        layer_config.operation = "add";
        layer_config.field_name = "emissions_2d";
        layer_config.hierarchy = 0;
        layer_config.scale = 1.0;
        layer_config.vdist_method = VerticalDistributionMethod::SINGLE;
        layer_config.vdist_layer_start = GenerateRandomLayer(nz);
        layer_config.vdist_layer_end = layer_config.vdist_layer_start;

        config.species_layers["TestSpecies"] = {layer_config};

        MassConservationFieldResolver resolver;
        resolver.AddField("emissions_2d", nx, ny, 1);
        resolver.AddField("TestSpecies", nx, ny, nz);

        // Set very large emissions
        std::uniform_real_distribution<double> large_emis_dist(1e6, 1e12);
        double total_2d = 0.0;
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                double val = large_emis_dist(rng);
                resolver.SetValue("emissions_2d", i, j, 0, val);
                total_2d += val;
            }
        }

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify mass conservation
        double total_3d = ComputeTotalColumnMass(resolver, "TestSpecies", nx, ny, nz);
        double rel_error = std::abs(total_3d - total_2d) / total_2d;

        EXPECT_LT(rel_error, 1e-10) << "Iteration " << iteration << ": Grid (" << nx << "," << ny
                                     << "," << nz << "), Large Emissions, Rel Error: " << rel_error;
    }
}

}  // namespace aces
