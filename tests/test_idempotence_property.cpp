#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <vector>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"

namespace aces {

/**
 * @brief FieldResolver implementation for idempotence property testing.
 */
class IdempotenceFieldResolver : public FieldResolver {
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
 * @brief Captures numerical output from a test execution.
 */
struct TestExecutionResult {
    std::vector<double> numerical_outputs;
    int pass_count = 0;
    int fail_count = 0;
    bool success = true;

    // Serialize to string for comparison
    std::string Serialize() const {
        std::ostringstream oss;
        oss << "pass_count=" << pass_count << "\n";
        oss << "fail_count=" << fail_count << "\n";
        oss << "success=" << (success ? "true" : "false") << "\n";
        oss << "numerical_outputs=[";
        for (size_t i = 0; i < numerical_outputs.size(); ++i) {
            if (i > 0) oss << ",";
            oss << std::scientific << std::setprecision(17) << numerical_outputs[i];
        }
        oss << "]\n";
        return oss.str();
    }

    bool EqualsWithinTolerance(const TestExecutionResult& other, double tolerance = 1e-15) const {
        // Check pass/fail counts match
        if (pass_count != other.pass_count || fail_count != other.fail_count) {
            return false;
        }

        // Check success flag matches
        if (success != other.success) {
            return false;
        }

        // Check numerical outputs match
        if (numerical_outputs.size() != other.numerical_outputs.size()) {
            return false;
        }

        for (size_t i = 0; i < numerical_outputs.size(); ++i) {
            double val1 = numerical_outputs[i];
            double val2 = other.numerical_outputs[i];

            // Handle special cases
            if (std::isnan(val1) && std::isnan(val2)) {
                continue;  // Both NaN, considered equal
            }
            if (std::isinf(val1) && std::isinf(val2)) {
                if ((val1 > 0) == (val2 > 0)) {
                    continue;  // Same infinity, considered equal
                }
                return false;
            }

            // Compute relative error
            double abs_diff = std::abs(val1 - val2);
            double max_abs = std::max(std::abs(val1), std::abs(val2));

            if (max_abs > 0) {
                double rel_error = abs_diff / max_abs;
                if (rel_error > tolerance) {
                    return false;
                }
            } else if (abs_diff > tolerance) {
                return false;
            }
        }

        return true;
    }
};

/**
 * @brief Property-based test suite for test idempotence.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 *
 * Property 17: Test Idempotence
 * Validates: Requirements 8.16
 *
 * FOR ALL test configurations, running tests twice SHALL produce identical
 * results (idempotence property). This includes:
 * - Identical pass/fail status for each test
 * - Identical numerical outputs within floating-point precision
 * - Deterministic behavior across multiple runs
 */
class IdempotencePropertyTest : public ::testing::Test {
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
     * @return Vector of emission values
     */
    std::vector<double> GenerateRandomEmissions(int nx, int ny) {
        std::uniform_real_distribution<double> emis_dist(0.1, 100.0);
        std::vector<double> emissions(nx * ny);

        for (int i = 0; i < nx * ny; ++i) {
            emissions[i] = emis_dist(rng);
        }

        return emissions;
    }

    /**
     * @brief Execute a single test configuration and capture results.
     * @param config_id Unique identifier for this configuration
     * @param nx Grid X dimension
     * @param ny Grid Y dimension
     * @param nz Grid Z dimension
     * @param emissions 2D emission field
     * @return TestExecutionResult with captured outputs
     */
    TestExecutionResult ExecuteTestConfiguration(int config_id, int nx, int ny, int nz,
                                                 const std::vector<double>& emissions) {
        TestExecutionResult result;

        try {
            // Create field resolver
            IdempotenceFieldResolver resolver;

            // Add fields. Zero-initialize to ensure determinism.
            resolver.AddField("base_emissions", nx, ny, 1);
            resolver.SetValue("base_emissions", 0.0);
            resolver.AddField("output_emissions", nx, ny, nz);
            resolver.SetValue("output_emissions", 0.0);
            resolver.AddField("scale_factor", nx, ny, 1);
            resolver.SetValue("scale_factor", 1.0);
            resolver.AddField("mask", nx, ny, 1);
            resolver.SetValue("mask", 1.0);

            // Initialize base emissions
            for (int i = 0; i < nx; ++i) {
                for (int j = 0; j < ny; ++j) {
                    resolver.SetValue("base_emissions", i, j, 0, emissions[i * ny + j]);
                }
            }

            // Create stacking engine configuration
            AcesConfig config;
            config.species_layers["test_species"] = {};

            // Create a simple layer. Ensure field_name matches an added field.
            EmissionLayer layer;
            layer.field_name = "base_emissions";
            layer.hierarchy = 1;
            layer.operation = "add";
            layer.vdist_method = aces::VerticalDistributionMethod::SINGLE;
            layer.vdist_layer_start = 0;
            layer.vdist_layer_end = 0;

            config.species_layers["test_species"].push_back(layer);

            // Create stacking engine
            StackingEngine engine(config);

            // Execute stacking engine
            engine.Execute(resolver, nx, ny, nz, {}, 12, 3, 0);

            // Capture numerical outputs
            for (int i = 0; i < nx; ++i) {
                for (int j = 0; j < ny; ++j) {
                    for (int k = 0; k < nz; ++k) {
                        result.numerical_outputs.push_back(
                            resolver.GetValue("output_emissions", i, j, k));
                    }
                }
            }

            result.pass_count = 1;
            result.fail_count = 0;
            result.success = true;

        } catch (const std::exception& e) {
            result.pass_count = 0;
            result.fail_count = 1;
            result.success = false;
        }

        return result;
    }
};

/**
 * @brief Test that a single configuration produces identical results on repeated execution.
 *
 * This test verifies that running the same test configuration twice produces
 * identical numerical outputs and pass/fail status.
 */
TEST_F(IdempotencePropertyTest, SingleConfigurationIdempotence) {
    auto [nx, ny, nz] = GenerateRandomGridDimensions();
    auto emissions = GenerateRandomEmissions(nx, ny);

    // Execute the same configuration twice
    auto result1 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);
    auto result2 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);

    // Verify results are identical
    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15))
        << "First run:\n"
        << result1.Serialize() << "\nSecond run:\n"
        << result2.Serialize();
}

/**
 * @brief Test that multiple configurations each produce idempotent results.
 *
 * This test verifies that different test configurations each produce
 * identical results when run twice.
 */
TEST_F(IdempotencePropertyTest, MultipleConfigurationsIdempotence) {
    const int num_configs = 5;

    for (int config_id = 0; config_id < num_configs; ++config_id) {
        auto [nx, ny, nz] = GenerateRandomGridDimensions();
        auto emissions = GenerateRandomEmissions(nx, ny);

        // Execute the same configuration twice
        auto result1 = ExecuteTestConfiguration(config_id, nx, ny, nz, emissions);
        auto result2 = ExecuteTestConfiguration(config_id, nx, ny, nz, emissions);

        // Verify results are identical
        EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15))
            << "Configuration " << config_id << " failed idempotence check\n"
            << "First run:\n"
            << result1.Serialize() << "\nSecond run:\n"
            << result2.Serialize();
    }
}

/**
 * @brief Test that small grid configurations are idempotent.
 *
 * This test verifies idempotence for minimal grid sizes.
 */
TEST_F(IdempotencePropertyTest, SmallGridIdempotence) {
    const int nx = 2;
    const int ny = 2;
    const int nz = 5;

    auto emissions = GenerateRandomEmissions(nx, ny);

    auto result1 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);
    auto result2 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);

    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15));
}

/**
 * @brief Test that large grid configurations are idempotent.
 *
 * This test verifies idempotence for larger grid sizes.
 */
TEST_F(IdempotencePropertyTest, LargeGridIdempotence) {
    const int nx = 20;
    const int ny = 20;
    const int nz = 50;

    auto emissions = GenerateRandomEmissions(nx, ny);

    auto result1 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);
    auto result2 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);

    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15));
}

/**
 * @brief Test that zero emission configurations are idempotent.
 *
 * This test verifies idempotence when all emissions are zero.
 */
TEST_F(IdempotencePropertyTest, ZeroEmissionIdempotence) {
    auto [nx, ny, nz] = GenerateRandomGridDimensions();
    std::vector<double> emissions(nx * ny, 0.0);

    auto result1 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);
    auto result2 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);

    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15));
}

/**
 * @brief Test that very small emission values are idempotent.
 *
 * This test verifies idempotence with near-zero emission values.
 */
TEST_F(IdempotencePropertyTest, VerySmallEmissionIdempotence) {
    auto [nx, ny, nz] = GenerateRandomGridDimensions();

    std::uniform_real_distribution<double> emis_dist(1e-10, 1e-8);
    std::vector<double> emissions(nx * ny);
    for (int i = 0; i < nx * ny; ++i) {
        emissions[i] = emis_dist(rng);
    }

    auto result1 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);
    auto result2 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);

    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15));
}

/**
 * @brief Test that very large emission values are idempotent.
 *
 * This test verifies idempotence with large emission values.
 */
TEST_F(IdempotencePropertyTest, VeryLargeEmissionIdempotence) {
    auto [nx, ny, nz] = GenerateRandomGridDimensions();

    std::uniform_real_distribution<double> emis_dist(1e6, 1e8);
    std::vector<double> emissions(nx * ny);
    for (int i = 0; i < nx * ny; ++i) {
        emissions[i] = emis_dist(rng);
    }

    auto result1 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);
    auto result2 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);

    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15));
}

/**
 * @brief Test that mixed emission values are idempotent.
 *
 * This test verifies idempotence with a mix of small and large values.
 */
TEST_F(IdempotencePropertyTest, MixedEmissionIdempotence) {
    auto [nx, ny, nz] = GenerateRandomGridDimensions();

    std::uniform_real_distribution<double> small_dist(1e-6, 1e-3);
    std::uniform_real_distribution<double> large_dist(1e3, 1e6);
    std::uniform_real_distribution<double> selector(0.0, 1.0);

    std::vector<double> emissions(nx * ny);
    for (int i = 0; i < nx * ny; ++i) {
        if (selector(rng) < 0.5) {
            emissions[i] = small_dist(rng);
        } else {
            emissions[i] = large_dist(rng);
        }
    }

    auto result1 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);
    auto result2 = ExecuteTestConfiguration(1, nx, ny, nz, emissions);

    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15));
}

/**
 * @brief Test that pass/fail counts are consistent across runs.
 *
 * This test verifies that the same configuration produces the same
 * pass/fail counts on repeated execution.
 */
TEST_F(IdempotencePropertyTest, PassFailCountIdempotence) {
    const int num_runs = 3;
    auto [nx, ny, nz] = GenerateRandomGridDimensions();
    auto emissions = GenerateRandomEmissions(nx, ny);

    std::vector<TestExecutionResult> results;
    for (int i = 0; i < num_runs; ++i) {
        results.push_back(ExecuteTestConfiguration(1, nx, ny, nz, emissions));
    }

    // All runs should have identical pass/fail counts
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_EQ(results[0].pass_count, results[i].pass_count)
            << "Pass count mismatch between run 0 and run " << i;
        EXPECT_EQ(results[0].fail_count, results[i].fail_count)
            << "Fail count mismatch between run 0 and run " << i;
    }
}

/**
 * @brief Test that success flag is consistent across runs.
 *
 * This test verifies that the same configuration produces the same
 * success/failure status on repeated execution.
 */
TEST_F(IdempotencePropertyTest, SuccessFlagIdempotence) {
    const int num_runs = 3;
    auto [nx, ny, nz] = GenerateRandomGridDimensions();
    auto emissions = GenerateRandomEmissions(nx, ny);

    std::vector<TestExecutionResult> results;
    for (int i = 0; i < num_runs; ++i) {
        results.push_back(ExecuteTestConfiguration(1, nx, ny, nz, emissions));
    }

    // All runs should have identical success flag
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_EQ(results[0].success, results[i].success)
            << "Success flag mismatch between run 0 and run " << i;
    }
}

}  // namespace aces
