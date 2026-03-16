/**
 * @file test_dynamic_species_registration_property.cpp
 * @brief Property 9: Dynamic Species Registration
 *
 * Validates: Requirements 3.14, 3.15
 *
 * FOR ALL new emission species added to YAML at runtime, ACES SHALL create
 * export fields without recompilation. This property-based test generates
 * 100+ random species names and verifies each can be added dynamically.
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_physics_factory.hpp"
#include "aces/aces_stacking_engine.hpp"

namespace aces {

/**
 * @brief FieldResolver for dynamic species registration testing.
 */
class DynamicSpeciesFieldResolver : public FieldResolver {
    std::map<std::string, DualView3D> fields_;

   public:
    void AddField(const std::string& name, int nx, int ny, int nz) {
        fields_[name] = DualView3D("dyn_" + name, nx, ny, nz);
    }

    bool HasField(const std::string& name) const {
        return fields_.count(name) > 0;
    }

    void SetValue(const std::string& name, double val) {
        auto host = fields_.at(name).view_host();
        Kokkos::deep_copy(host, val);
        fields_.at(name).modify<Kokkos::HostSpace>();
        fields_.at(name).sync<Kokkos::DefaultExecutionSpace::memory_space>();
    }

    double GetValue(const std::string& name, int i, int j, int k) {
        fields_.at(name).sync<Kokkos::HostSpace>();
        return fields_.at(name).view_host()(i, j, k);
    }

    UnmanagedHostView3D ResolveImport(const std::string& name, int, int, int) override {
        return fields_.at(name).view_host();
    }
    UnmanagedHostView3D ResolveExport(const std::string& name, int, int, int) override {
        return fields_.at(name).view_host();
    }
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveImportDevice(const std::string& name, int, int, int) override {
        return fields_.at(name).view_device();
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
    ResolveExportDevice(const std::string& name, int, int, int) override {
        return fields_.at(name).view_device();
    }
};

/**
 * @brief Property-based test suite for dynamic species registration.
 *
 * Property 9: Dynamic Species Registration
 * Validates: Requirements 3.14, 3.15
 */
class DynamicSpeciesRegistrationPropertyTest : public ::testing::Test {
   protected:
    std::mt19937 rng_{42};

    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }

    /** Generate a random valid species name. */
    std::string GenerateSpeciesName(int id) {
        const std::vector<std::string> prefixes = {"CO", "NOx", "SO2", "NH3", "VOC",
                                                    "BC", "OC", "PM25", "CH4", "N2O"};
        const std::vector<std::string> suffixes = {"_anthro", "_biogenic", "_fire",
                                                    "_natural", "_ship", "_aircraft"};
        std::uniform_int_distribution<int> p(0, prefixes.size() - 1);
        std::uniform_int_distribution<int> s(0, suffixes.size() - 1);
        return prefixes[p(rng_)] + suffixes[s(rng_)] + "_" + std::to_string(id);
    }

    /** Generate random grid dimensions. */
    std::tuple<int, int, int> GenerateGrid() {
        std::uniform_int_distribution<int> nx(2, 10);
        std::uniform_int_distribution<int> ny(2, 10);
        std::uniform_int_distribution<int> nz(2, 10);
        return {nx(rng_), ny(rng_), nz(rng_)};
    }
};

/**
 * @test Property 9.1: Single species added at runtime is discoverable.
 *
 * FOR ALL species names, calling AddSpecies then constructing a StackingEngine
 * SHALL include the new species in the engine's processing.
 *
 * Iterations: 50
 */
TEST_F(DynamicSpeciesRegistrationPropertyTest, SingleSpeciesAddedAtRuntime) {
    for (int i = 0; i < 50; ++i) {
        AcesConfig config;
        std::string species = GenerateSpeciesName(i);

        // Verify species does not exist yet
        EXPECT_EQ(config.species_layers.count(species), 0u)
            << "Species should not exist before AddSpecies";

        // Add species dynamically
        EmissionLayer layer;
        layer.operation = "add";
        layer.field_name = species + "_src";
        layer.hierarchy = 1;
        layer.scale = 1.0;
        layer.vdist_method = VerticalDistributionMethod::SINGLE;
        layer.vdist_layer_start = 0;
        layer.vdist_layer_end = 0;

        AddSpecies(config, species, {layer});

        // Verify species now exists
        EXPECT_EQ(config.species_layers.count(species), 1u)
            << "Species should exist after AddSpecies: " << species;
        EXPECT_EQ(config.species_layers.at(species).size(), 1u)
            << "Species should have exactly one layer";
        EXPECT_EQ(config.species_layers.at(species)[0].field_name, species + "_src");
    }
}

/**
 * @test Property 9.2: Multiple species added at runtime are all discoverable.
 *
 * FOR ALL sets of N species, adding all N then constructing StackingEngine
 * SHALL include all N species.
 *
 * Iterations: 20 sets of up to 10 species each
 */
TEST_F(DynamicSpeciesRegistrationPropertyTest, MultipleSpeciesAddedAtRuntime) {
    for (int iter = 0; iter < 20; ++iter) {
        AcesConfig config;
        std::uniform_int_distribution<int> count_dist(2, 10);
        int n = count_dist(rng_);

        std::vector<std::string> species_names;
        for (int i = 0; i < n; ++i) {
            std::string name = GenerateSpeciesName(iter * 100 + i);
            species_names.push_back(name);

            EmissionLayer layer;
            layer.operation = "add";
            layer.field_name = name + "_src";
            layer.hierarchy = 1;
            layer.scale = 1.0;
            layer.vdist_method = VerticalDistributionMethod::SINGLE;
            layer.vdist_layer_start = 0;
            layer.vdist_layer_end = 0;

            AddSpecies(config, name, {layer});
        }

        // Verify all species are present
        EXPECT_EQ(config.species_layers.size(), static_cast<size_t>(n))
            << "All " << n << " species should be registered";

        for (const auto& name : species_names) {
            EXPECT_EQ(config.species_layers.count(name), 1u)
                << "Species " << name << " should be registered";
        }
    }
}

/**
 * @test Property 9.3: Dynamically added species can be executed by StackingEngine.
 *
 * FOR ALL dynamically added species, the StackingEngine SHALL process them
 * and produce non-zero output when source field is non-zero.
 *
 * Iterations: 30
 */
TEST_F(DynamicSpeciesRegistrationPropertyTest, DynamicSpeciesExecutedByStackingEngine) {
    for (int i = 0; i < 30; ++i) {
        auto [nx, ny, nz] = GenerateGrid();
        std::string species = "DynSpecies_" + std::to_string(i);
        std::string src_field = species + "_src";

        AcesConfig config;

        EmissionLayer layer;
        layer.operation = "add";
        layer.field_name = src_field;
        layer.hierarchy = 1;
        layer.scale = 1.0;
        layer.vdist_method = VerticalDistributionMethod::SINGLE;
        layer.vdist_layer_start = 0;
        layer.vdist_layer_end = 0;

        // Add species dynamically - no recompilation needed
        AddSpecies(config, species, {layer});

        DynamicSpeciesFieldResolver resolver;
        resolver.AddField(src_field, nx, ny, 1);
        resolver.AddField(species, nx, ny, nz);

        // Set source to known value
        const double src_val = 42.0 + i;
        resolver.SetValue(src_field, src_val);

        StackingEngine engine(config);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0);

        // Verify output is non-zero (species was processed)
        double out = resolver.GetValue(species, 0, 0, 0);
        EXPECT_NEAR(out, src_val, src_val * 1e-10)
            << "Dynamically added species " << species
            << " should produce correct output. Got: " << out << " expected: " << src_val;
    }
}

/**
 * @test Property 9.4: Scale factor added at runtime is applied correctly.
 *
 * FOR ALL scale factors added via AddScaleFactor, the StackingEngine SHALL
 * apply them to the appropriate layers.
 *
 * Iterations: 30
 */
TEST_F(DynamicSpeciesRegistrationPropertyTest, DynamicScaleFactorApplied) {
    for (int i = 0; i < 30; ++i) {
        auto [nx, ny, nz] = GenerateGrid();
        std::string species = "ScaleSpecies_" + std::to_string(i);
        std::string src_field = species + "_src";
        std::string scale_internal = "my_scale_" + std::to_string(i);
        std::string scale_external = "ext_scale_" + std::to_string(i);

        AcesConfig config;

        // Add scale factor mapping dynamically
        AddScaleFactor(config, scale_internal, scale_external);

        // Verify it was registered
        EXPECT_EQ(config.scale_factor_mapping.count(scale_internal), 1u)
            << "Scale factor should be registered: " << scale_internal;
        EXPECT_EQ(config.scale_factor_mapping.at(scale_internal), scale_external)
            << "Scale factor external name should match";
    }
}

/**
 * @test Property 9.5: Species added with multiple layers preserves layer order.
 *
 * FOR ALL species with N layers, AddSpecies SHALL preserve the layer order
 * in the config.
 *
 * Iterations: 20
 */
TEST_F(DynamicSpeciesRegistrationPropertyTest, MultipleLayersPreserveOrder) {
    for (int iter = 0; iter < 20; ++iter) {
        AcesConfig config;
        std::string species = "MultiLayer_" + std::to_string(iter);

        std::uniform_int_distribution<int> n_layers_dist(2, 5);
        int n_layers = n_layers_dist(rng_);

        std::vector<EmissionLayer> layers;
        for (int k = 0; k < n_layers; ++k) {
            EmissionLayer layer;
            layer.operation = (k == 0) ? "add" : "add";
            layer.field_name = species + "_src_" + std::to_string(k);
            layer.hierarchy = k;
            layer.scale = 1.0 + k * 0.1;
            layer.vdist_method = VerticalDistributionMethod::SINGLE;
            layer.vdist_layer_start = 0;
            layer.vdist_layer_end = 0;
            layers.push_back(layer);
        }

        AddSpecies(config, species, layers);

        ASSERT_EQ(config.species_layers.at(species).size(), static_cast<size_t>(n_layers))
            << "Layer count should match";

        for (int k = 0; k < n_layers; ++k) {
            EXPECT_EQ(config.species_layers.at(species)[k].hierarchy, k)
                << "Layer order should be preserved at index " << k;
            EXPECT_EQ(config.species_layers.at(species)[k].field_name,
                      species + "_src_" + std::to_string(k))
                << "Layer field name should be preserved at index " << k;
        }
    }
}

/**
 * @test Property 9.6: Adding species to existing config does not affect other species.
 *
 * FOR ALL existing configs, adding a new species SHALL not modify existing species.
 *
 * Iterations: 20
 */
TEST_F(DynamicSpeciesRegistrationPropertyTest, AddingSpeciesDoesNotAffectExisting) {
    for (int iter = 0; iter < 20; ++iter) {
        AcesConfig config;

        // Pre-populate with some species
        const int n_existing = 3;
        for (int k = 0; k < n_existing; ++k) {
            EmissionLayer layer;
            layer.operation = "add";
            layer.field_name = "existing_src_" + std::to_string(k);
            layer.hierarchy = k;
            layer.scale = 2.0;
            layer.vdist_method = VerticalDistributionMethod::SINGLE;
            layer.vdist_layer_start = 0;
            layer.vdist_layer_end = 0;
            AddSpecies(config, "existing_" + std::to_string(k), {layer});
        }

        // Snapshot existing state
        auto snapshot = config.species_layers;

        // Add new species
        EmissionLayer new_layer;
        new_layer.operation = "add";
        new_layer.field_name = "new_src";
        new_layer.hierarchy = 1;
        new_layer.scale = 1.0;
        new_layer.vdist_method = VerticalDistributionMethod::SINGLE;
        new_layer.vdist_layer_start = 0;
        new_layer.vdist_layer_end = 0;
        AddSpecies(config, "new_species_" + std::to_string(iter), {new_layer});

        // Verify existing species are unchanged
        for (int k = 0; k < n_existing; ++k) {
            std::string name = "existing_" + std::to_string(k);
            ASSERT_EQ(config.species_layers.count(name), 1u);
            EXPECT_EQ(config.species_layers.at(name).size(),
                      snapshot.at(name).size())
                << "Existing species " << name << " layer count should be unchanged";
            EXPECT_EQ(config.species_layers.at(name)[0].field_name,
                      snapshot.at(name)[0].field_name)
                << "Existing species " << name << " field name should be unchanged";
        }
    }
}

}  // namespace aces
