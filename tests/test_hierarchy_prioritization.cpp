/**
 * @file test_hierarchy_prioritization.cpp
 * @brief Property 8: Hierarchy-Based Layer Prioritization
 *
 * **Validates: Requirements 3.4, 3.16**
 *
 * Property: FOR ALL emission layer configurations with multiple hierarchy levels,
 * a layer with a higher hierarchy value and "replace" operation SHALL override
 * the contribution of lower-hierarchy layers in the cells where its mask is active.
 *
 * Test strategy:
 * - Generate layers with randomized hierarchy levels, field values, and masks
 * - Verify higher-hierarchy "replace" layers override lower-hierarchy "add" layers
 * - Test with various species names and grid configurations (2D and 3D)
 * - Run 100+ iterations with different random seeds
 *
 * All tests use real Kokkos (no ESMF mocking required for unit-level tests).
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_state.hpp"

namespace aces {

// ---------------------------------------------------------------------------
// Shared fixture
// ---------------------------------------------------------------------------

class HierarchyPrioritizationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }

    /** Creates a DualView of given dimensions filled with a constant value. */
    static DualView3D MakeField(const std::string& name, double val,
                                int nx, int ny, int nz = 1) {
        DualView3D dv(name, nx, ny, nz);
        Kokkos::deep_copy(dv.view_host(), val);
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
        return dv;
    }

    /** Creates a mask DualView: 1.0 where predicate(i,j) is true, else 0.0. */
    static DualView3D MakeMask(const std::string& name,
                               int nx, int ny,
                               std::function<bool(int, int)> predicate) {
        DualView3D dv(name, nx, ny, 1);
        auto h = dv.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h(i, j, 0) = predicate(i, j) ? 1.0 : 0.0;
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
        return dv;
    }

    /** Runs the StackingEngine and syncs the named export field to host. */
    static Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace>
    RunEngine(AcesConfig& cfg, AcesImportState& imp, AcesExportState& exp,
              const std::string& species, int nx, int ny, int nz = 1) {
        std::unordered_map<std::string, std::string> empty;
        AcesStateResolver resolver(imp, exp, empty);
        StackingEngine engine(cfg);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0, 0);
        exp.fields.at(species).sync<Kokkos::HostSpace>();
        return exp.fields.at(species).view_host();
    }

    /** Returns the max absolute error across all cells vs an expected scalar. */
    static double MaxAbsError(
        const Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace>& v,
        double expected, int nx, int ny, int nz = 1) {
        double err = 0.0;
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                for (int k = 0; k < nz; ++k)
                    err = std::max(err, std::abs(v(i, j, k) - expected));
        return err;
    }

    /** Creates a properly initialized EmissionLayer with all fields set. */
    static aces::EmissionLayer MakeLayer(
        const std::string& field_name,
        const std::string& operation,
        int hierarchy,
        double scale = 1.0,
        const std::vector<std::string>& masks = {},
        aces::VerticalDistributionMethod vdist = aces::VerticalDistributionMethod::SINGLE) {
        aces::EmissionLayer layer;
        layer.field_name = field_name;
        layer.operation = operation;
        layer.hierarchy = hierarchy;
        layer.scale = scale;
        layer.masks = masks;
        layer.vdist_method = vdist;
        layer.vdist_layer_start = 0;
        layer.vdist_layer_end = 0;
        layer.vdist_p_start = 0.0;
        layer.vdist_p_end = 0.0;
        layer.vdist_h_start = 0.0;
        layer.vdist_h_end = 0.0;
        layer.category = "";
        return layer;
    }
};

// ---------------------------------------------------------------------------
// 1. Basic replace overrides add (deterministic)
// ---------------------------------------------------------------------------

/**
 * @brief A single high-hierarchy "replace" layer completely overrides a
 *        low-hierarchy "add" layer when the mask covers the entire grid.
 *
 * Setup:
 *   Layer 1: hierarchy=1, operation="add",     value=5.0, full mask
 *   Layer 2: hierarchy=10, operation="replace", value=12.0, full mask
 *
 * Expected: every cell == 12.0
 */
TEST_F(HierarchyPrioritizationTest, ReplaceOverridesAddFullMask) {
    const int nx = 8, ny = 8;
    AcesImportState imp;
    AcesExportState exp;
    imp.fields["global_co"] = MakeField("global_co", 5.0, nx, ny);
    imp.fields["regional_co"] = MakeField("regional_co", 12.0, nx, ny);
    exp.fields["co"] = MakeField("co", 0.0, nx, ny);

    AcesConfig cfg;
    aces::EmissionLayer low = MakeLayer("global_co", "add", 1, 1.0);
    aces::EmissionLayer high = MakeLayer("regional_co", "replace", 10, 1.0);

    cfg.species_layers["co"] = {low, high};

    auto result = RunEngine(cfg, imp, exp, "co", nx, ny);
    EXPECT_LT(MaxAbsError(result, 12.0, nx, ny), 1e-12)
        << "Full-mask replace should yield regional value everywhere";
}

// ---------------------------------------------------------------------------
// 2. Replace only inside mask; add survives outside
// ---------------------------------------------------------------------------

/**
 * @brief A high-hierarchy "replace" layer only overrides cells where its mask
 *        is 1.0; cells outside the mask retain the low-hierarchy "add" value.
 *
 * Mask: top half of grid (j >= ny/2) = 1.0, bottom half = 0.0
 * Expected inside mask:  regional value (12.0)
 * Expected outside mask: global value   (5.0)
 */
TEST_F(HierarchyPrioritizationTest, ReplaceOnlyInsideMask) {
    const int nx = 8, ny = 8;
    AcesImportState imp;
    AcesExportState exp;
    imp.fields["global_co"] = MakeField("global_co", 5.0, nx, ny);
    imp.fields["regional_co"] = MakeField("regional_co", 12.0, nx, ny);
    imp.fields["region_mask"] =
        MakeMask("region_mask", nx, ny, [&](int, int j) { return j >= ny / 2; });
    exp.fields["co"] = MakeField("co", 0.0, nx, ny);

    AcesConfig cfg;
    aces::EmissionLayer low = MakeLayer("global_co", "add", 1, 1.0);
    aces::EmissionLayer high = MakeLayer("regional_co", "replace", 10, 1.0, {"region_mask"});

    cfg.species_layers["co"] = {low, high};

    auto result = RunEngine(cfg, imp, exp, "co", nx, ny);

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            if (j >= ny / 2) {
                EXPECT_NEAR(result(i, j, 0), 12.0, 1e-12)
                    << "Inside mask at (" << i << "," << j << "): expected regional value";
            } else {
                EXPECT_NEAR(result(i, j, 0), 5.0, 1e-12)
                    << "Outside mask at (" << i << "," << j << "): expected global value";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 3. Multiple hierarchy levels – highest wins
// ---------------------------------------------------------------------------

/**
 * @brief With three hierarchy levels (1, 5, 10), the highest-hierarchy
 *        "replace" layer wins in its masked region; the mid-level "replace"
 *        wins in its exclusive region; the base "add" survives elsewhere.
 */
TEST_F(HierarchyPrioritizationTest, ThreeLevelHierarchy) {
    const int nx = 12, ny = 12;
    // Region A: j in [0, 3)   -> base only  (5.0)
    // Region B: j in [4, 7)   -> mid replace (8.0)
    // Region C: j in [8, 11]  -> top replace (15.0)

    AcesImportState imp;
    AcesExportState exp;
    imp.fields["base"] = MakeField("base", 5.0, nx, ny);
    imp.fields["mid"] = MakeField("mid", 8.0, nx, ny);
    imp.fields["top"] = MakeField("top", 15.0, nx, ny);
    imp.fields["mask_mid"] =
        MakeMask("mask_mid", nx, ny, [](int, int j) { return j >= 4 && j <= 7; });
    imp.fields["mask_top"] =
        MakeMask("mask_top", nx, ny, [](int, int j) { return j >= 8; });
    exp.fields["nox"] = MakeField("nox", 0.0, nx, ny);

    AcesConfig cfg;
    aces::EmissionLayer lay_base = MakeLayer("base", "add", 1, 1.0);
    aces::EmissionLayer lay_mid = MakeLayer("mid", "replace", 5, 1.0, {"mask_mid"});
    aces::EmissionLayer lay_top = MakeLayer("top", "replace", 10, 1.0, {"mask_top"});

    cfg.species_layers["nox"] = {lay_base, lay_mid, lay_top};

    auto result = RunEngine(cfg, imp, exp, "nox", nx, ny);

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = 5.0;
            if (j >= 8)
                expected = 15.0;
            else if (j >= 4)
                expected = 8.0;
            EXPECT_NEAR(result(i, j, 0), expected, 1e-12)
                << "Three-level hierarchy failed at (" << i << "," << j << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// 4. Property test – 100 random configurations
// ---------------------------------------------------------------------------

/**
 * @brief Property 8 (randomized): FOR ALL configurations with N layers at
 *        distinct hierarchy levels, the cell-wise result equals the value of
 *        the highest-hierarchy "replace" layer whose mask covers that cell,
 *        or the sum of all "add" layers if no "replace" mask covers it.
 *
 * Runs 100 iterations with different random seeds, grid sizes, and layer counts.
 */
TEST_F(HierarchyPrioritizationTest, RandomizedHierarchyProperty) {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dim_dist(4, 16);
    std::uniform_int_distribution<int> layer_count_dist(2, 6);
    std::uniform_real_distribution<double> val_dist(1.0e-12, 1.0e-6);
    std::uniform_int_distribution<int> hier_dist(1, 100);

    constexpr int kIterations = 100;

    for (int iter = 0; iter < kIterations; ++iter) {
        const int nx = dim_dist(rng);
        const int ny = dim_dist(rng);
        const int num_layers = layer_count_dist(rng);

        // Generate unique, sorted hierarchy levels
        std::vector<int> hierarchies;
        while (static_cast<int>(hierarchies.size()) < num_layers) {
            int h = hier_dist(rng);
            if (std::find(hierarchies.begin(), hierarchies.end(), h) == hierarchies.end())
                hierarchies.push_back(h);
        }
        std::sort(hierarchies.begin(), hierarchies.end());

        // Build per-layer field values and masks (checkerboard per layer)
        std::vector<double> layer_vals(num_layers);
        std::vector<std::string> ops(num_layers);
        // Layer 0 is always "add" (base); higher layers are "replace"
        ops[0] = "add";
        for (int l = 1; l < num_layers; ++l) ops[l] = "replace";

        AcesImportState imp;
        AcesExportState exp;
        AcesConfig cfg;
        std::vector<EmissionLayer> layers;

        for (int l = 0; l < num_layers; ++l) {
            layer_vals[l] = val_dist(rng);
            std::string fname = "field_" + std::to_string(iter) + "_" + std::to_string(l);
            std::string mname = "mask_" + std::to_string(iter) + "_" + std::to_string(l);

            imp.fields[fname] = MakeField(fname, layer_vals[l], nx, ny);

            EmissionLayer lay;
            lay.field_name = fname;
            lay.operation = ops[l];
            lay.hierarchy = hierarchies[l];
            lay.scale = 1.0;
            // Initialize vertical distribution fields
            lay.vdist_method = aces::VerticalDistributionMethod::SINGLE;
            lay.vdist_layer_start = 0;
            lay.vdist_layer_end = 0;
            lay.vdist_p_start = 0.0;
            lay.vdist_p_end = 0.0;
            lay.vdist_h_start = 0.0;
            lay.vdist_h_end = 0.0;

            // Replace layers get a checkerboard mask based on layer index
            if (ops[l] == "replace") {
                imp.fields[mname] = MakeMask(mname, nx, ny,
                    [l](int i, int j) { return ((i + j + l) % 2) == 0; });
                lay.masks = {mname};
            }

            layers.push_back(lay);
        }

        exp.fields["species"] = MakeField("species", 0.0, nx, ny);
        cfg.species_layers["species"] = layers;

        auto result = RunEngine(cfg, imp, exp, "species", nx, ny);

        // Compute expected value per cell using the same logic as StackingEngine:
        // Process layers in hierarchy order; "replace" zeroes out previous and
        // sets new value where mask==1; "add" accumulates.
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                double expected = 0.0;
                for (int l = 0; l < num_layers; ++l) {
                    if (ops[l] == "add") {
                        expected += layer_vals[l];
                    } else {
                        // mask: ((i+j+l) % 2) == 0
                        bool in_mask = ((i + j + l) % 2) == 0;
                        if (in_mask) {
                            expected = layer_vals[l];
                        }
                    }
                }
                EXPECT_NEAR(result(i, j, 0), expected, 1e-20)
                    << "Iter " << iter << " at (" << i << "," << j << ")"
                    << " hierarchy order violated";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 5. Multiple species – hierarchy is per-species, not global
// ---------------------------------------------------------------------------

/**
 * @brief Hierarchy prioritization is independent per species.
 *        Species "co" and "nox" each have their own layer stacks with
 *        different hierarchy configurations; neither should bleed into the other.
 */
TEST_F(HierarchyPrioritizationTest, HierarchyIsPerSpecies) {
    const int nx = 6, ny = 6;
    AcesImportState imp;
    AcesExportState exp;

    imp.fields["co_global"] = MakeField("co_global", 3.0, nx, ny);
    imp.fields["co_regional"] = MakeField("co_regional", 9.0, nx, ny);
    imp.fields["nox_global"] = MakeField("nox_global", 1.0, nx, ny);
    imp.fields["nox_regional"] = MakeField("nox_regional", 7.0, nx, ny);
    imp.fields["co_mask"] =
        MakeMask("co_mask", nx, ny, [&](int, int j) { return j >= ny / 2; });
    // nox has no mask on its replace layer -> full override
    exp.fields["co"] = MakeField("co", 0.0, nx, ny);
    exp.fields["nox"] = MakeField("nox", 0.0, nx, ny);

    AcesConfig cfg;

    aces::EmissionLayer co_low = MakeLayer("co_global", "add", 1, 1.0);
    aces::EmissionLayer co_high = MakeLayer("co_regional", "replace", 5, 1.0, {"co_mask"});
    aces::EmissionLayer nox_low = MakeLayer("nox_global", "add", 1, 1.0);
    aces::EmissionLayer nox_high = MakeLayer("nox_regional", "replace", 20, 1.0);

    cfg.species_layers["co"] = {co_low, co_high};
    cfg.species_layers["nox"] = {nox_low, nox_high};

    std::unordered_map<std::string, std::string> empty;
    AcesStateResolver resolver(imp, exp, empty);
    StackingEngine engine(cfg);
    engine.Execute(resolver, nx, ny, 1, {}, 0, 0, 0);
    exp.fields["co"].sync<Kokkos::HostSpace>();
    exp.fields["nox"].sync<Kokkos::HostSpace>();
    auto co_result = exp.fields["co"].view_host();
    auto nox_result = exp.fields["nox"].view_host();

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double co_expected = (j >= ny / 2) ? 9.0 : 3.0;
            EXPECT_NEAR(co_result(i, j, 0), co_expected, 1e-12)
                << "CO species at (" << i << "," << j << ")";
            EXPECT_NEAR(nox_result(i, j, 0), 7.0, 1e-12)
                << "NOx species at (" << i << "," << j << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// 6. Various grid configurations (2D and 3D)
// ---------------------------------------------------------------------------

/**
 * @brief Hierarchy prioritization works correctly for 3D fields (nz > 1).
 *        2D masks are correctly applied to 3D fields at all vertical levels.
 *        With SINGLE distribution (default), emissions only appear at k=0.
 */
TEST_F(HierarchyPrioritizationTest, HierarchyWith3DFields) {
    const int nx = 4, ny = 4, nz = 5;
    AcesImportState imp;
    AcesExportState exp;

    // Create 2D fields (nz=1) - these will be distributed vertically
    imp.fields["base_2d"] = MakeField("base_2d", 2.0, nx, ny, 1);
    imp.fields["override_2d"] = MakeField("override_2d", 8.0, nx, ny, 1);
    // 2D mask - applies to all k levels
    imp.fields["mask_2d"] =
        MakeMask("mask_2d", nx, ny, [&](int i, int) { return i >= nx / 2; });
    exp.fields["so2"] = MakeField("so2", 0.0, nx, ny, nz);

    AcesConfig cfg;
    // Use SINGLE distribution (default) - emissions only at k=0
    aces::EmissionLayer low = MakeLayer("base_2d", "add", 1, 1.0, {},
                                        aces::VerticalDistributionMethod::SINGLE);
    low.vdist_layer_start = 0;
    low.vdist_layer_end = 0;

    aces::EmissionLayer high = MakeLayer("override_2d", "replace", 8, 1.0, {"mask_2d"},
                                         aces::VerticalDistributionMethod::SINGLE);
    high.vdist_layer_start = 0;
    high.vdist_layer_end = 0;

    cfg.species_layers["so2"] = {low, high};

    auto result = RunEngine(cfg, imp, exp, "so2", nx, ny, nz);

    // With SINGLE distribution, emissions only appear at k=0
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            // At k=0: base layer (2.0) is replaced by override (8.0) where mask is 1
            double expected_k0 = (i >= nx / 2) ? 8.0 : 2.0;
            EXPECT_NEAR(result(i, j, 0), expected_k0, 1e-12)
                << "3D field at k=0 (" << i << "," << j << ",0)";

            // At k>0: no emissions (SINGLE distribution only applies to k=0)
            for (int k = 1; k < nz; ++k) {
                EXPECT_NEAR(result(i, j, k), 0.0, 1e-12)
                    << "3D field at k>0 (" << i << "," << j << "," << k << ")";
            }
        }
    }
}

/**
 * @brief Hierarchy prioritization works for non-square grids of various sizes.
 *        Exercises the property across a range of grid configurations.
 */
TEST_F(HierarchyPrioritizationTest, VariousGridSizes) {
    std::vector<std::pair<int, int>> grids = {{1, 1}, {2, 8}, {8, 2}, {16, 32}, {32, 16}};

    for (auto [nx, ny] : grids) {
        AcesImportState imp;
        AcesExportState exp;
        imp.fields["base"] = MakeField("base", 4.0, nx, ny);
        imp.fields["override"] = MakeField("override", 11.0, nx, ny);
        imp.fields["mask"] =
            MakeMask("mask", nx, ny, [&](int i, int j) { return (i + j) % 2 == 0; });
        exp.fields["isop"] = MakeField("isop", 0.0, nx, ny);

        AcesConfig cfg;
        aces::EmissionLayer low = MakeLayer("base", "add", 1, 1.0);
        aces::EmissionLayer high = MakeLayer("override", "replace", 9, 1.0, {"mask"});

        cfg.species_layers["isop"] = {low, high};

        auto result = RunEngine(cfg, imp, exp, "isop", nx, ny);

        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                double expected = ((i + j) % 2 == 0) ? 11.0 : 4.0;
                EXPECT_NEAR(result(i, j, 0), expected, 1e-12)
                    << "Grid " << nx << "x" << ny << " at (" << i << "," << j << ")";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 7. Scale factor applied to replace layer
// ---------------------------------------------------------------------------

/**
 * @brief A "replace" layer with a scale factor != 1.0 stores
 *        field_value * scale in the replaced cells.
 */
TEST_F(HierarchyPrioritizationTest, ReplaceWithScaleFactor) {
    const int nx = 6, ny = 6;
    AcesImportState imp;
    AcesExportState exp;
    imp.fields["base"] = MakeField("base", 5.0, nx, ny);
    imp.fields["regional"] = MakeField("regional", 10.0, nx, ny);
    imp.fields["mask"] =
        MakeMask("mask", nx, ny, [&](int, int j) { return j >= ny / 2; });
    exp.fields["co"] = MakeField("co", 0.0, nx, ny);

    AcesConfig cfg;
    aces::EmissionLayer low = MakeLayer("base", "add", 1, 1.0);
    aces::EmissionLayer high = MakeLayer("regional", "replace", 7, 0.5, {"mask"});

    cfg.species_layers["co"] = {low, high};

    auto result = RunEngine(cfg, imp, exp, "co", nx, ny);

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = (j >= ny / 2) ? 10.0 * 0.5 : 5.0;
            EXPECT_NEAR(result(i, j, 0), expected, 1e-12)
                << "Scaled replace at (" << i << "," << j << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// 8. Add layers accumulate before replace fires
// ---------------------------------------------------------------------------

/**
 * @brief Multiple "add" layers at low hierarchy accumulate correctly, and a
 *        single high-hierarchy "replace" layer then overrides the total in
 *        its masked region.
 */
TEST_F(HierarchyPrioritizationTest, MultipleAddLayersThenReplace) {
    const int nx = 8, ny = 8;
    AcesImportState imp;
    AcesExportState exp;
    imp.fields["add1"] = MakeField("add1", 3.0, nx, ny);
    imp.fields["add2"] = MakeField("add2", 4.0, nx, ny);
    imp.fields["add3"] = MakeField("add3", 2.0, nx, ny);
    imp.fields["rep"] = MakeField("rep", 20.0, nx, ny);
    imp.fields["mask"] =
        MakeMask("mask", nx, ny, [&](int i, int) { return i >= nx / 2; });
    exp.fields["co"] = MakeField("co", 0.0, nx, ny);

    AcesConfig cfg;
    aces::EmissionLayer l1 = MakeLayer("add1", "add", 1, 1.0);
    aces::EmissionLayer l2 = MakeLayer("add2", "add", 2, 1.0);
    aces::EmissionLayer l3 = MakeLayer("add3", "add", 3, 1.0);
    aces::EmissionLayer lrep = MakeLayer("rep", "replace", 50, 1.0, {"mask"});

    cfg.species_layers["co"] = {l1, l2, l3, lrep};

    auto result = RunEngine(cfg, imp, exp, "co", nx, ny);

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            double expected = (i >= nx / 2) ? 20.0 : (3.0 + 4.0 + 2.0);
            EXPECT_NEAR(result(i, j, 0), expected, 1e-12)
                << "Multi-add then replace at (" << i << "," << j << ")";
        }
    }
}

}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
