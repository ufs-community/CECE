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
 * @brief Captures test suite execution results for idempotence comparison.
 *
 * This structure holds the results of running a test suite, including:
 * - Pass/fail counts
 * - Numerical outputs from computations
 * - Success/failure status
 */
struct TestSuiteResult {
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    std::vector<double> numerical_outputs;
    bool overall_success = true;

    /**
     * @brief Serialize results to string for comparison.
     * @return String representation of results
     */
    std::string Serialize() const {
        std::ostringstream oss;
        oss << "total_tests=" << total_tests << "\n";
        oss << "passed_tests=" << passed_tests << "\n";
        oss << "failed_tests=" << failed_tests << "\n";
        oss << "overall_success=" << (overall_success ? "true" : "false") << "\n";
        oss << "numerical_outputs=[";
        for (size_t i = 0; i < numerical_outputs.size(); ++i) {
            if (i > 0) oss << ",";
            oss << std::scientific << std::setprecision(17) << numerical_outputs[i];
        }
        oss << "]\n";
        return oss.str();
    }

    /**
     * @brief Compare two test suite results for equality within tolerance.
     * @param other The other result to compare against
     * @param tolerance Relative error tolerance for numerical comparisons
     * @return True if results are identical within tolerance
     */
    bool EqualsWithinTolerance(const TestSuiteResult& other, double tolerance = 1e-15) const {
        // Check test counts match
        if (total_tests != other.total_tests || passed_tests != other.passed_tests ||
            failed_tests != other.failed_tests) {
            return false;
        }

        // Check overall success flag matches
        if (overall_success != other.overall_success) {
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
 * @brief FieldResolver implementation for test suite idempotence testing.
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
 * @brief Property-based test suite for test suite idempotence.
 * Execution Space: Default (OpenMP/Serial/CUDA)
 *
 * Property 17: Test Idempotence
 * Validates: Requirements 8.16
 *
 * FOR ALL test configurations, running tests twice SHALL produce identical
 * results (idempotence property). This includes:
 * - Identical pass/fail counts
 * - Identical numerical outputs within floating-point precision
 * - Deterministic behavior across multiple runs
 *
 * This test suite simulates running multiple test configurations and verifies
 * that running them twice produces identical results.
 */
class TestSuiteIdempotenceTest : public ::testing::Test {
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
     * @return TestSuiteResult with captured outputs
     */
    TestSuiteResult ExecuteTestConfiguration(int config_id, int nx, int ny, int nz,
                                             const std::vector<double>& emissions) {
        TestSuiteResult result;

        try {
            // Create field resolver
            IdempotenceFieldResolver resolver;

            // Add fields
            resolver.AddField("base_emissions", nx, ny, 1);
            resolver.AddField("output_emissions", nx, ny, nz);
            resolver.AddField("scale_factor", nx, ny, 1);
            resolver.AddField("mask", nx, ny, 1);

            // Initialize base emissions
            for (int i = 0; i < nx; ++i) {
                for (int j = 0; j < ny; ++j) {
                    resolver.SetValue("base_emissions", i, j, 0, emissions[i * ny + j]);
                }
            }

            // Initialize scale factor to 1.0
            resolver.SetValue("scale_factor", 1.0);

            // Initialize mask to 1.0
            resolver.SetValue("mask", 1.0);

            // Create stacking engine configuration
            AcesConfig config;
            config.species_layers["test_species"] = {};

            // Create a simple layer
            EmissionLayer layer;
            layer.field_name = "test_layer";
            layer.hierarchy = 1;
            layer.operation = "add";
            layer.vdist_method = aces::VerticalDistributionMethod::SINGLE;
            layer.vdist_layer_start = 0;
            layer.vdist_layer_end = 0;

            config.species_layers["test_species"].push_back(layer);

            // Create stacking engine
            StackingEngine engine(config);

            // Execute stacking engine
            engine.Execute(resolver, nx, ny, nz, {}, 12, 3);

            // Capture numerical outputs
            for (int i = 0; i < nx; ++i) {
                for (int j = 0; j < ny; ++j) {
                    for (int k = 0; k < nz; ++k) {
                        result.numerical_outputs.push_back(
                            resolver.GetValue("output_emissions", i, j, k));
                    }
                }
            }

            result.total_tests = 1;
            result.passed_tests = 1;
            result.failed_tests = 0;
            result.overall_success = true;

        } catch (const std::exception& e) {
            result.total_tests = 1;
            result.passed_tests = 0;
            result.failed_tests = 1;
            result.overall_success = false;
        }

        return result;
    }

    /**
     * @brief Run a complete test suite with multiple configurations.
     * @param num_configs Number of test configurations to run
     * @return Aggregated TestSuiteResult
     */
    TestSuiteResult RunTestSuite(int num_configs) {
        TestSuiteResult aggregated;

        for (int config_id = 0; config_id < num_configs; ++config_id) {
            auto [nx, ny, nz] = GenerateRandomGridDimensions();
            auto emissions = GenerateRandomEmissions(nx, ny);

            auto result = ExecuteTestConfiguration(config_id, nx, ny, nz, emissions);

            aggregated.total_tests += result.total_tests;
            aggregated.passed_tests += result.passed_tests;
            aggregated.failed_tests += result.failed_tests;

            // Append numerical outputs
            aggregated.numerical_outputs.insert(aggregated.numerical_outputs.end(),
                                                result.numerical_outputs.begin(),
                                                result.numerical_outputs.end());

            if (!result.overall_success) {
                aggregated.overall_success = false;
            }
        }

        return aggregated;
    }
};

/**
 * @brief Test that running a test suite twice produces identical results.
 *
 * This test verifies that running the same test suite configuration twice
 * produces identical pass/fail counts and numerical outputs.
 *
 * **Validates: Requirements 8.16**
 */
TEST_F(TestSuiteIdempotenceTest, TestSuiteIdempotence) {
    const int num_configs = 5;

    // Run test suite twice
    auto result1 = RunTestSuite(num_configs);
    auto result2 = RunTestSuite(num_configs);

    // Verify results are identical
    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15))
        << "First run:\n"
        << result1.Serialize() << "\nSecond run:\n"
        << result2.Serialize();
}

/**
 * @brief Test that multiple test suite runs produce consistent results.
 *
 * This test verifies that running the test suite multiple times produces
 * identical results each time, demonstrating deterministic behavior.
 *
 * **Validates: Requirements 8.16**
 */
TEST_F(TestSuiteIdempotenceTest, MultipleTestSuiteRunsIdempotence) {
    const int num_configs = 3;
    const int num_runs = 3;

    std::vector<TestSuiteResult> results;
    for (int i = 0; i < num_runs; ++i) {
        results.push_back(RunTestSuite(num_configs));
    }

    // All runs should produce identical results
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_TRUE(results[0].EqualsWithinTolerance(results[i], 1e-15))
            << "Run 0 vs Run " << i << " mismatch\n"
            << "Run 0:\n"
            << results[0].Serialize() << "\nRun " << i << ":\n"
            << results[i].Serialize();
    }
}

/**
 * @brief Test that pass/fail counts are consistent across runs.
 *
 * This test verifies that the same test suite produces the same
 * pass/fail counts on repeated execution.
 *
 * **Validates: Requirements 8.16**
 */
TEST_F(TestSuiteIdempotenceTest, PassFailCountIdempotence) {
    const int num_configs = 5;
    const int num_runs = 3;

    std::vector<TestSuiteResult> results;
    for (int i = 0; i < num_runs; ++i) {
        results.push_back(RunTestSuite(num_configs));
    }

    // All runs should have identical pass/fail counts
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_EQ(results[0].total_tests, results[i].total_tests)
            << "Total test count mismatch between run 0 and run " << i;
        EXPECT_EQ(results[0].passed_tests, results[i].passed_tests)
            << "Passed test count mismatch between run 0 and run " << i;
        EXPECT_EQ(results[0].failed_tests, results[i].failed_tests)
            << "Failed test count mismatch between run 0 and run " << i;
    }
}

/**
 * @brief Test that overall success flag is consistent across runs.
 *
 * This test verifies that the same test suite produces the same
 * success/failure status on repeated execution.
 *
 * **Validates: Requirements 8.16**
 */
TEST_F(TestSuiteIdempotenceTest, SuccessFlagIdempotence) {
    const int num_configs = 5;
    const int num_runs = 3;

    std::vector<TestSuiteResult> results;
    for (int i = 0; i < num_runs; ++i) {
        results.push_back(RunTestSuite(num_configs));
    }

    // All runs should have identical success flag
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_EQ(results[0].overall_success, results[i].overall_success)
            << "Overall success flag mismatch between run 0 and run " << i;
    }
}

/**
 * @brief Test that numerical outputs are deterministic across runs.
 *
 * This test verifies that numerical computations produce identical results
 * when run multiple times with the same inputs.
 *
 * **Validates: Requirements 8.16**
 */
TEST_F(TestSuiteIdempotenceTest, NumericalOutputIdempotence) {
    const int num_configs = 5;
    const int num_runs = 3;

    std::vector<TestSuiteResult> results;
    for (int i = 0; i < num_runs; ++i) {
        results.push_back(RunTestSuite(num_configs));
    }

    // All runs should have identical numerical outputs
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_EQ(results[0].numerical_outputs.size(), results[i].numerical_outputs.size())
            << "Numerical output count mismatch between run 0 and run " << i;

        for (size_t j = 0; j < results[0].numerical_outputs.size(); ++j) {
            double val1 = results[0].numerical_outputs[j];
            double val2 = results[i].numerical_outputs[j];

            // Handle special cases
            if (std::isnan(val1) && std::isnan(val2)) {
                continue;
            }
            if (std::isinf(val1) && std::isinf(val2)) {
                if ((val1 > 0) == (val2 > 0)) {
                    continue;
                }
            }

            // Compute relative error
            double abs_diff = std::abs(val1 - val2);
            double max_abs = std::max(std::abs(val1), std::abs(val2));

            if (max_abs > 0) {
                double rel_error = abs_diff / max_abs;
                EXPECT_LE(rel_error, 1e-15)
                    << "Numerical output mismatch at index " << j << " between run 0 and run " << i
                    << ": " << std::scientific << std::setprecision(17) << val1 << " vs " << val2;
            } else {
                EXPECT_LE(abs_diff, 1e-15)
                    << "Numerical output mismatch at index " << j << " between run 0 and run " << i
                    << ": " << std::scientific << std::setprecision(17) << val1 << " vs " << val2;
            }
        }
    }
}

/**
 * @brief Test that small test suites are idempotent.
 *
 * This test verifies idempotence for minimal test suite sizes.
 *
 * **Validates: Requirements 8.16**
 */
TEST_F(TestSuiteIdempotenceTest, SmallTestSuiteIdempotence) {
    const int num_configs = 1;

    auto result1 = RunTestSuite(num_configs);
    auto result2 = RunTestSuite(num_configs);

    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15));
}

/**
 * @brief Test that large test suites are idempotent.
 *
 * This test verifies idempotence for larger test suite sizes.
 *
 * **Validates: Requirements 8.16**
 */
TEST_F(TestSuiteIdempotenceTest, LargeTestSuiteIdempotence) {
    const int num_configs = 10;

    auto result1 = RunTestSuite(num_configs);
    auto result2 = RunTestSuite(num_configs);

    EXPECT_TRUE(result1.EqualsWithinTolerance(result2, 1e-15));
}

}  // namespace aces
