/**
 * @file test_scale_factor_commutativity.cpp
 * @brief Property 20: Scale Factor Commutativity
 *
 * **Validates: Requirements 3.2**
 *
 * Property: FOR ALL emission layers with multiple scale factors, applying those
 * scale factors in different orders SHALL produce identical final emissions.
 *
 * The StackingEngine multiplies all scale_fields together as a combined_scale
 * before applying to the base field value. Because multiplication is commutative
 * and associative over real numbers, the order in which scale fields appear in
 * the scale_fields list must not affect the result.
 *
 * Test strategy:
 * - Deterministic tests: 2 and 3 scale fields, verify all permutations match
 * - Property test: 100+ iterations with random grid sizes, field values, and
 *   scale factor counts (2–5), checking every permutation of scale_fields
 * - Multi-species test: commutativity holds independently per species
 * - 3D field test: commutativity holds across all vertical levels
 *
 * All tests use real Kokkos (no ESMF mocking required for unit-level tests).
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
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

class ScaleFactorCommutativityTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }

    /** Creates a DualView filled with a constant value. */
    static DualView3D MakeField(const std::string& name, double val, int nx, int ny, int nz = 1) {
        DualView3D dv(name, nx, ny, nz);
        Kokkos::deep_copy(dv.view_host(), val);
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
        return dv;
    }

    /**
     * @brief Runs the StackingEngine with a given scale_fields order and
     *        returns the host-side result view.
     */
    static Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> RunWithScaleOrder(
        const std::string& base_field_name, double base_val,
        const std::vector<std::string>& scale_names, const std::vector<double>& scale_vals, int nx,
        int ny, int nz = 1) {
        AcesImportState imp;
        AcesExportState exp;

        imp.fields[base_field_name] = MakeField(base_field_name, base_val, nx, ny, nz);
        for (size_t i = 0; i < scale_names.size(); ++i) {
            imp.fields[scale_names[i]] = MakeField(scale_names[i], scale_vals[i], nx, ny, nz);
        }
        exp.fields["species"] = MakeField("species", 0.0, nx, ny, nz);

        AcesConfig cfg;
        EmissionLayer layer;
        layer.field_name = base_field_name;
        layer.operation = "add";
        layer.hierarchy = 1;
        layer.scale = 1.0;
        layer.scale_fields = scale_names;  // order under test
        cfg.species_layers["species"] = {layer};

        std::unordered_map<std::string, std::string> empty;
        AcesStateResolver resolver(imp, exp, empty);
        StackingEngine engine(cfg);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0, 0);
        exp.fields["species"].sync<Kokkos::HostSpace>();
        return exp.fields["species"].view_host();
    }

    /** Returns the max absolute difference between two result views. */
    static double MaxAbsDiff(
        const Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace>& a,
        const Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace>& b, int nx, int ny,
        int nz = 1) {
        double diff = 0.0;
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                for (int k = 0; k < nz; ++k)
                    diff = std::max(diff, std::abs(a(i, j, k) - b(i, j, k)));
        return diff;
    }
};

// ---------------------------------------------------------------------------
// 1. Two scale factors – both permutations match
// ---------------------------------------------------------------------------

/**
 * @brief With two scale fields [sf_a, sf_b] and [sf_b, sf_a], the result
 *        must be identical because multiplication is commutative.
 *
 * base = 2.0, sf_a = 3.0, sf_b = 5.0
 * Expected: 2.0 * 3.0 * 5.0 = 30.0 in every cell
 */
TEST_F(ScaleFactorCommutativityTest, TwoScaleFactorsBothPermutations) {
    const int nx = 8, ny = 8;
    const double base = 2.0, sf_a = 3.0, sf_b = 5.0;

    auto result_ab = RunWithScaleOrder("base", base, {"sf_a", "sf_b"}, {sf_a, sf_b}, nx, ny);
    auto result_ba = RunWithScaleOrder("base", base, {"sf_b", "sf_a"}, {sf_b, sf_a}, nx, ny);

    EXPECT_LT(MaxAbsDiff(result_ab, result_ba, nx, ny), 1e-15)
        << "Two scale factors: [sf_a, sf_b] vs [sf_b, sf_a] must be identical";

    // Also verify the absolute value is correct
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            EXPECT_NEAR(result_ab(i, j, 0), 30.0, 1e-12)
                << "Expected base * sf_a * sf_b = 30.0 at (" << i << "," << j << ")";
}

// ---------------------------------------------------------------------------
// 2. Three scale factors – all 6 permutations match
// ---------------------------------------------------------------------------

/**
 * @brief With three scale fields, all 6 permutations must produce the same
 *        result.
 *
 * base = 1.5, sf_a = 2.0, sf_b = 4.0, sf_c = 0.5
 * Expected: 1.5 * 2.0 * 4.0 * 0.5 = 6.0
 */
TEST_F(ScaleFactorCommutativityTest, ThreeScaleFactorsAllPermutations) {
    const int nx = 6, ny = 6;
    const double base = 1.5;
    const std::vector<std::string> names = {"sf_a", "sf_b", "sf_c"};
    const std::vector<double> vals = {2.0, 4.0, 0.5};

    // Generate all 6 permutations
    std::vector<int> idx = {0, 1, 2};
    std::vector<Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace>> results;

    do {
        std::vector<std::string> perm_names = {names[idx[0]], names[idx[1]], names[idx[2]]};
        std::vector<double> perm_vals = {vals[idx[0]], vals[idx[1]], vals[idx[2]]};
        results.push_back(RunWithScaleOrder("base", base, perm_names, perm_vals, nx, ny));
    } while (std::next_permutation(idx.begin(), idx.end()));

    ASSERT_EQ(results.size(), 6u);

    for (size_t p = 1; p < results.size(); ++p) {
        EXPECT_LT(MaxAbsDiff(results[0], results[p], nx, ny), 1e-15)
            << "Permutation " << p << " differs from permutation 0";
    }

    // Verify absolute value
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            EXPECT_NEAR(results[0](i, j, 0), 6.0, 1e-12)
                << "Expected 1.5 * 2.0 * 4.0 * 0.5 = 6.0 at (" << i << "," << j << ")";
}

// ---------------------------------------------------------------------------
// 3. Scale factor of 1.0 is identity
// ---------------------------------------------------------------------------

/**
 * @brief A scale factor of exactly 1.0 must not change the result regardless
 *        of its position in the scale_fields list.
 */
TEST_F(ScaleFactorCommutativityTest, IdentityScaleFactorIsNeutral) {
    const int nx = 4, ny = 4;
    const double base = 7.0, sf_real = 3.0;

    auto result_with_identity_first =
        RunWithScaleOrder("base", base, {"sf_one", "sf_real"}, {1.0, sf_real}, nx, ny);
    auto result_with_identity_last =
        RunWithScaleOrder("base", base, {"sf_real", "sf_one"}, {sf_real, 1.0}, nx, ny);
    auto result_no_scale = RunWithScaleOrder("base", base, {"sf_real"}, {sf_real}, nx, ny);

    EXPECT_LT(MaxAbsDiff(result_with_identity_first, result_with_identity_last, nx, ny), 1e-15)
        << "Identity scale factor position must not matter";
    EXPECT_LT(MaxAbsDiff(result_with_identity_first, result_no_scale, nx, ny), 1e-15)
        << "Adding a 1.0 scale factor must not change the result";
}

// ---------------------------------------------------------------------------
// 4. Scale factor of 0.0 zeroes the result regardless of position
// ---------------------------------------------------------------------------

/**
 * @brief A zero scale factor must zero the result regardless of its position
 *        in the scale_fields list.
 */
TEST_F(ScaleFactorCommutativityTest, ZeroScaleFactorZeroesResult) {
    const int nx = 4, ny = 4;
    const double base = 5.0;

    auto result_zero_first =
        RunWithScaleOrder("base", base, {"sf_zero", "sf_nonzero"}, {0.0, 3.0}, nx, ny);
    auto result_zero_last =
        RunWithScaleOrder("base", base, {"sf_nonzero", "sf_zero"}, {3.0, 0.0}, nx, ny);

    EXPECT_LT(MaxAbsDiff(result_zero_first, result_zero_last, nx, ny), 1e-15)
        << "Zero scale factor must zero result regardless of position";

    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            EXPECT_NEAR(result_zero_first(i, j, 0), 0.0, 1e-15)
                << "Zero scale factor must produce zero at (" << i << "," << j << ")";
}

// ---------------------------------------------------------------------------
// 5. Spatially-varying scale fields – commutativity holds cell-wise
// ---------------------------------------------------------------------------

/**
 * @brief When scale fields have spatially varying values, commutativity must
 *        hold at every grid cell independently.
 *
 * sf_a(i,j) = i + 1, sf_b(i,j) = j + 1
 * Expected: base * (i+1) * (j+1) == base * (j+1) * (i+1)
 */
TEST_F(ScaleFactorCommutativityTest, SpatiallyVaryingScaleFieldsCommute) {
    const int nx = 8, ny = 8;
    const double base = 2.0;

    // Build spatially varying scale fields manually
    auto make_spatial = [&](const std::string& name, bool use_i) {
        DualView3D dv(name, nx, ny, 1);
        auto h = dv.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                h(i, j, 0) = use_i ? static_cast<double>(i + 1) : static_cast<double>(j + 1);
        dv.modify<Kokkos::HostSpace>();
        dv.sync<Kokkos::DefaultExecutionSpace>();
        return dv;
    };

    // Run with [sf_i, sf_j]
    {
        AcesImportState imp;
        AcesExportState exp;
        imp.fields["base"] = MakeField("base", base, nx, ny);
        imp.fields["sf_i"] = make_spatial("sf_i", true);
        imp.fields["sf_j"] = make_spatial("sf_j", false);
        exp.fields["species"] = MakeField("species", 0.0, nx, ny);

        AcesConfig cfg;
        EmissionLayer layer;
        layer.field_name = "base";
        layer.operation = "add";
        layer.hierarchy = 1;
        layer.scale = 1.0;
        layer.scale_fields = {"sf_i", "sf_j"};
        cfg.species_layers["species"] = {layer};

        std::unordered_map<std::string, std::string> empty;
        AcesStateResolver resolver(imp, exp, empty);
        StackingEngine engine_ij(cfg);
        engine_ij.Execute(resolver, nx, ny, 1, {}, 0, 0, 0);
        exp.fields["species"].sync<Kokkos::HostSpace>();
        auto result_ij = exp.fields["species"].view_host();

        // Run with [sf_j, sf_i]
        AcesImportState imp2;
        AcesExportState exp2;
        imp2.fields["base"] = MakeField("base", base, nx, ny);
        imp2.fields["sf_i"] = make_spatial("sf_i", true);
        imp2.fields["sf_j"] = make_spatial("sf_j", false);
        exp2.fields["species"] = MakeField("species", 0.0, nx, ny);

        AcesConfig cfg2;
        EmissionLayer layer2 = layer;
        layer2.scale_fields = {"sf_j", "sf_i"};
        cfg2.species_layers["species"] = {layer2};

        AcesStateResolver resolver2(imp2, exp2, empty);
        StackingEngine engine_ji(cfg2);
        engine_ji.Execute(resolver2, nx, ny, 1, {}, 0, 0, 0);
        exp2.fields["species"].sync<Kokkos::HostSpace>();
        auto result_ji = exp2.fields["species"].view_host();

        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                EXPECT_NEAR(result_ij(i, j, 0), result_ji(i, j, 0), 1e-15)
                    << "Spatial scale commutativity failed at (" << i << "," << j << ")";
                double expected = base * (i + 1) * (j + 1);
                EXPECT_NEAR(result_ij(i, j, 0), expected, 1e-12)
                    << "Spatial scale value wrong at (" << i << "," << j << ")";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 6. 3D fields – commutativity holds across all vertical levels
// ---------------------------------------------------------------------------

/**
 * @brief Scale factor commutativity must hold for 3D emission fields across
 *        all vertical levels.
 *
 * This test creates 3D fields and uses RANGE vertical distribution to apply
 * emissions across all vertical levels (k=0 to k=nz-1).
 */
TEST_F(ScaleFactorCommutativityTest, ThreeDFieldsCommute) {
    const int nx = 4, ny = 4, nz = 6;
    const double base = 3.0, sf_a = 2.5, sf_b = 0.4;

    // Helper lambda to run with RANGE vertical distribution across all layers
    auto run_3d = [&](const std::vector<std::string>& scale_names,
                      const std::vector<double>& scale_vals) {
        AcesImportState imp;
        AcesExportState exp;

        imp.fields["base"] = MakeField("base", base, nx, ny, nz);
        for (size_t i = 0; i < scale_names.size(); ++i) {
            imp.fields[scale_names[i]] = MakeField(scale_names[i], scale_vals[i], nx, ny, nz);
        }
        exp.fields["species"] = MakeField("species", 0.0, nx, ny, nz);

        AcesConfig cfg;
        EmissionLayer layer;
        layer.field_name = "base";
        layer.operation = "add";
        layer.hierarchy = 1;
        layer.scale = 1.0;
        layer.scale_fields = scale_names;
        // Use RANGE vertical distribution to apply to all layers
        layer.vdist_method = VerticalDistributionMethod::RANGE;
        layer.vdist_layer_start = 0;
        layer.vdist_layer_end = nz - 1;
        cfg.species_layers["species"] = {layer};

        std::unordered_map<std::string, std::string> empty;
        AcesStateResolver resolver(imp, exp, empty);
        StackingEngine engine(cfg);
        engine.Execute(resolver, nx, ny, nz, {}, 0, 0, 0);
        exp.fields["species"].sync<Kokkos::HostSpace>();
        return exp.fields["species"].view_host();
    };

    auto result_ab = run_3d({"sf_a", "sf_b"}, {sf_a, sf_b});
    auto result_ba = run_3d({"sf_b", "sf_a"}, {sf_b, sf_a});

    EXPECT_LT(MaxAbsDiff(result_ab, result_ba, nx, ny, nz), 1e-15)
        << "3D scale factor commutativity failed";

    // With RANGE distribution, each layer gets 1/(nz) of the base value
    // BUT wait! For 3D fields natively, the weight is 1.0!
    // Stacking engine says:
    // if (is_3d_field) {
    //      in_vertical_range = true;
    //      weight = 1.0;
    // }
    const double expected = base * sf_a * sf_b;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k)
                EXPECT_NEAR(result_ab(i, j, k), expected, 1e-12)
                    << "3D value wrong at (" << i << "," << j << "," << k << ")";
}

// ---------------------------------------------------------------------------
// 7. Multiple species – commutativity is independent per species
// ---------------------------------------------------------------------------

/**
 * @brief Scale factor commutativity holds independently for each species;
 *        reordering scale fields for one species must not affect another.
 */
TEST_F(ScaleFactorCommutativityTest, CommutativityIsPerSpecies) {
    const int nx = 6, ny = 6;

    auto run_two_species = [&](std::vector<std::string> co_scales,
                               std::vector<std::string> nox_scales) {
        AcesImportState imp;
        AcesExportState exp;
        imp.fields["co_base"] = MakeField("co_base", 2.0, nx, ny);
        imp.fields["nox_base"] = MakeField("nox_base", 5.0, nx, ny);
        imp.fields["sf_x"] = MakeField("sf_x", 3.0, nx, ny);
        imp.fields["sf_y"] = MakeField("sf_y", 0.5, nx, ny);
        exp.fields["co"] = MakeField("co", 0.0, nx, ny);
        exp.fields["nox"] = MakeField("nox", 0.0, nx, ny);

        AcesConfig cfg;
        EmissionLayer co_layer;
        co_layer.field_name = "co_base";
        co_layer.operation = "add";
        co_layer.hierarchy = 1;
        co_layer.scale = 1.0;
        co_layer.scale_fields = co_scales;

        EmissionLayer nox_layer;
        nox_layer.field_name = "nox_base";
        nox_layer.operation = "add";
        nox_layer.hierarchy = 1;
        nox_layer.scale = 1.0;
        nox_layer.scale_fields = nox_scales;

        cfg.species_layers["co"] = {co_layer};
        cfg.species_layers["nox"] = {nox_layer};

        std::unordered_map<std::string, std::string> empty;
        AcesStateResolver resolver(imp, exp, empty);
        StackingEngine engine(cfg);
        engine.Execute(resolver, nx, ny, 1, {}, 0, 0, 0);
        exp.fields["co"].sync<Kokkos::HostSpace>();
        exp.fields["nox"].sync<Kokkos::HostSpace>();
        return std::make_pair(exp.fields["co"].view_host(), exp.fields["nox"].view_host());
    };

    auto [co_xy, nox_xy] = run_two_species({"sf_x", "sf_y"}, {"sf_x", "sf_y"});
    auto [co_yx, nox_yx] = run_two_species({"sf_y", "sf_x"}, {"sf_y", "sf_x"});

    EXPECT_LT(MaxAbsDiff(co_xy, co_yx, nx, ny), 1e-15) << "CO commutativity failed";
    EXPECT_LT(MaxAbsDiff(nox_xy, nox_yx, nx, ny), 1e-15) << "NOx commutativity failed";
}

// ---------------------------------------------------------------------------
// 8. Property test – 100 random configurations
// ---------------------------------------------------------------------------

/**
 * @brief Property 20 (randomized): FOR ALL layers with 2–5 scale factors,
 *        applying those scale factors in any permutation SHALL produce
 *        identical final emissions within 1e-15 absolute error.
 *
 * Runs 100 iterations with different random seeds, grid sizes, base field
 * values, and scale factor values. For each iteration, all permutations of
 * the scale_fields list are tested against the canonical (sorted) order.
 */
TEST_F(ScaleFactorCommutativityTest, RandomizedCommutativityProperty) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dim_dist(2, 12);
    std::uniform_int_distribution<int> num_scales_dist(2, 5);
    std::uniform_real_distribution<double> val_dist(1.0e-10, 1.0e-4);
    std::uniform_real_distribution<double> scale_dist(0.1, 10.0);

    constexpr int kIterations = 100;

    for (int iter = 0; iter < kIterations; ++iter) {
        const int nx = dim_dist(rng);
        const int ny = dim_dist(rng);
        const int num_scales = num_scales_dist(rng);
        const double base_val = val_dist(rng);

        // Generate scale field names and values
        std::vector<std::string> scale_names;
        std::vector<double> scale_vals;
        for (int s = 0; s < num_scales; ++s) {
            scale_names.push_back("sf_" + std::to_string(iter) + "_" + std::to_string(s));
            scale_vals.push_back(scale_dist(rng));
        }

        // Canonical result: scale_fields in original order
        auto canonical = RunWithScaleOrder("base_" + std::to_string(iter), base_val, scale_names,
                                           scale_vals, nx, ny);

        // Test all permutations
        std::vector<int> perm_idx(num_scales);
        std::iota(perm_idx.begin(), perm_idx.end(), 0);

        int perm_count = 0;
        do {
            std::vector<std::string> perm_names;
            std::vector<double> perm_vals;
            for (int idx : perm_idx) {
                perm_names.push_back(scale_names[idx]);
                perm_vals.push_back(scale_vals[idx]);
            }

            auto permuted = RunWithScaleOrder("base_" + std::to_string(iter), base_val, perm_names,
                                              perm_vals, nx, ny);

            double diff = MaxAbsDiff(canonical, permuted, nx, ny);
            EXPECT_LT(diff, 1e-15)
                << "Iter " << iter << " permutation " << perm_count << " (nx=" << nx << " ny=" << ny
                << " num_scales=" << num_scales << "): max abs diff = " << diff;

            ++perm_count;
        } while (std::next_permutation(perm_idx.begin(), perm_idx.end()));
    }
}

// ---------------------------------------------------------------------------
// 9. Layer-level scale combined with scale_fields – both commute
// ---------------------------------------------------------------------------

/**
 * @brief The layer-level `scale` multiplier combined with `scale_fields` must
 *        also be commutative: the result must equal base * layer.scale * prod(scale_fields).
 */
TEST_F(ScaleFactorCommutativityTest, LayerScaleAndScaleFieldsCommute) {
    const int nx = 6, ny = 6;
    const double base = 4.0, layer_scale = 0.25, sf_a = 3.0, sf_b = 2.0;
    const double expected = base * layer_scale * sf_a * sf_b;  // = 6.0

    // Build both orderings with a non-unity layer.scale
    auto run = [&](std::vector<std::string> sf_order, std::vector<double> sv_order) {
        AcesImportState imp;
        AcesExportState exp;
        imp.fields["base"] = MakeField("base", base, nx, ny);
        for (size_t i = 0; i < sf_order.size(); ++i)
            imp.fields[sf_order[i]] = MakeField(sf_order[i], sv_order[i], nx, ny);
        exp.fields["species"] = MakeField("species", 0.0, nx, ny);

        AcesConfig cfg;
        EmissionLayer layer;
        layer.field_name = "base";
        layer.operation = "add";
        layer.hierarchy = 1;
        layer.scale = layer_scale;
        layer.scale_fields = sf_order;
        cfg.species_layers["species"] = {layer};

        std::unordered_map<std::string, std::string> empty;
        AcesStateResolver resolver(imp, exp, empty);
        StackingEngine engine(cfg);
        engine.Execute(resolver, nx, ny, 1, {}, 0, 0, 0);
        exp.fields["species"].sync<Kokkos::HostSpace>();
        return exp.fields["species"].view_host();
    };

    auto result_ab = run({"sf_a", "sf_b"}, {sf_a, sf_b});
    auto result_ba = run({"sf_b", "sf_a"}, {sf_b, sf_a});

    EXPECT_LT(MaxAbsDiff(result_ab, result_ba, nx, ny), 1e-15)
        << "Layer scale + scale_fields commutativity failed";

    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            EXPECT_NEAR(result_ab(i, j, 0), expected, 1e-12)
                << "Expected " << expected << " at (" << i << "," << j << ")";
}

}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
