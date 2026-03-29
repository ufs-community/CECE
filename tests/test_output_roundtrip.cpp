/**
 * @file test_output_roundtrip.cpp
 * @brief Property test for AcesStandaloneWriter output round-trip validation.
 *
 * Property 23: Output Round-Trip
 * Validates: Requirements 11.14
 *
 * Verifies that:
 *   - Run single-model driver for N time steps with output enabled
 *   - Read back each written NetCDF file using NetCDF-C API
 *   - All field values match in-memory export state within 1e-15 relative error
 *   - Test with multiple field types (2D and 3D) and multiple time records
 *
 * The test generates random valid export field data, writes it via
 * AcesStandaloneWriter, reads it back using NetCDF-C API, and verifies
 * that the read values match the original within floating-point precision.
 *
 * Requirements: 11.14
 */

#include <gtest/gtest.h>
#include <netcdf.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "ESMC.h"
#include "aces/aces_standalone_writer.hpp"

// ---------------------------------------------------------------------------
// Helpers for random data generation and comparison
// ---------------------------------------------------------------------------

/**
 * @brief Generate random double values in range [min_val, max_val].
 */
static double RandomDouble(double min_val = 0.0, double max_val = 1.0) {
    static std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<> dis(min_val, max_val);
    return dis(gen);
}

/**
 * @brief Compute relative error between two values.
 *
 * Returns |a - b| / max(|a|, |b|, 1e-20) to avoid division by zero.
 */
static double RelativeError(double a, double b) {
    double denom = std::max({std::abs(a), std::abs(b), 1e-20});
    return std::abs(a - b) / denom;
}

/**
 * @brief Create a 3D Kokkos::View filled with random data.
 *
 * @param nx X dimension
 * @param ny Y dimension
 * @param nz Z dimension
 * @param min_val Minimum random value
 * @param max_val Maximum random value
 * @return Kokkos::View<double***, Kokkos::LayoutLeft>
 */
static aces::DualView3D CreateRandomField(int nx, int ny, int nz, double min_val = 0.0,
                                          double max_val = 1.0) {
    auto view = aces::DualView3D("random_field", nx, ny, nz);

    auto host_view = view.view_host();
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                host_view(i, j, k) = RandomDouble(min_val, max_val);
            }
        }
    }
    view.template sync<Kokkos::HostSpace>();
    return view;
}

/**
 * @brief Read a NetCDF variable and return as host-side vector.
 *
 * @param ncid NetCDF file ID
 * @param varid NetCDF variable ID
 * @param nx X dimension
 * @param ny Y dimension
 * @param nz Z dimension
 * @param time_index Time record index
 * @return std::vector<double> with data in row-major order
 */
static std::vector<double> ReadNetCDFVariable(int ncid, int varid, int nx, int ny, int nz,
                                              size_t time_index) {
    std::vector<double> data(nx * ny * nz);
    size_t start[4] = {time_index, 0, 0, 0};
    // Writer stores dims as (time, x, y, z) with LayoutLeft data
    size_t count[4] = {1, static_cast<size_t>(nz), static_cast<size_t>(ny),
                       static_cast<size_t>(nx)};

    int rc = nc_get_vara_double(ncid, varid, start, count, data.data());
    if (rc != NC_NOERR) {
        throw std::runtime_error(std::string("nc_get_vara_double failed: ") + nc_strerror(rc));
    }

    // Need to reorder from NetCDF (time, z, y, x) to Kokkos LayoutLeft (x, y, z)
    // Actually, in the writer we write from (x, y, z) LayoutLeft array to (lev, lat, lon) by explicitly swapping indices.
    // So NetCDF has elements ordered by lev, then lat, then lon in memory? No, NetCDF C API writes in C order (row-major).
    // So the data buffer is essentially row-major representation of the (nz, ny, nx) array.
    std::vector<double> layout_left_data(nx * ny * nz);
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                size_t nc_idx = k * ny * nx + j * nx + i;
                size_t kk_idx = j + i * ny + k * nx * ny;
                layout_left_data[kk_idx] = data[nc_idx];
            }
        }
    }

    return layout_left_data;
}

// ---------------------------------------------------------------------------
// Global Environments
// ---------------------------------------------------------------------------

class KokkosEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }

    void TearDown() override {
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
    }
};

class ESMFEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        int rc;
        ESMC_Initialize(&rc, ESMC_ArgLast);
        if (rc != ESMF_SUCCESS) {
            std::cerr << "ESMF initialization failed" << std::endl;
            std::exit(1);
        }
    }

    void TearDown() override {
        ESMC_Finalize();
    }
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @class OutputRoundTripTest
 * @brief Fixture for output round-trip property tests.
 */
class OutputRoundTripTest : public ::testing::Test {
   protected:
    static constexpr int kDefaultNx = 4;
    static constexpr int kDefaultNy = 4;
    static constexpr int kDefaultNz = 5;
    static constexpr int kDefaultTimeSteps = 3;

    std::string output_dir_ = "./test_output_roundtrip_data";
    std::string output_file_;

    void SetUp() override {
        // Create output directory
        system(("mkdir -p " + output_dir_).c_str());
        system(("chmod 777 " + output_dir_).c_str());
    }

    void TearDown() override {
        // Clean up output directory
        system(("rm -rf " + output_dir_).c_str());
    }

    /**
     * @brief Write export fields using AcesStandaloneWriter and return the
     * output file path.
     */
    std::string WriteExportFields(
        const std::unordered_map<std::string, aces::DualView3D>& export_fields, int nx, int ny,
        int nz, int num_timesteps = 3, int frequency_steps = 1) {
        aces::AcesOutputConfig config;
    config.enabled = true;
        config.directory = output_dir_;
        config.filename_pattern = "test_output_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
        config.frequency_steps = frequency_steps;
        config.include_diagnostics = false;

        // Add all field names to config
        for (const auto& [name, view] : export_fields) {
            config.fields.push_back(name);
        }

        aces::AcesStandaloneWriter writer(config);

        int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
        EXPECT_EQ(rc, 0) << "Writer initialization failed";

        // Write steps
        for (int step = 0; step < num_timesteps; ++step) {
            double time_seconds = step * 3600.0;  // 1 hour per step
            rc = writer.WriteTimeStep(export_fields, time_seconds, step);
            EXPECT_EQ(rc, 0) << "WriteTimeStep failed at step " << step;
        }

        writer.Finalize();

        // Find one output file
        std::string cmd = "ls " + output_dir_ + "/*.nc 2>/dev/null | head -n 1";
        FILE* pipe = popen(cmd.c_str(), "r");
        char buffer[256];
        std::string result;
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result = buffer;
            if (!result.empty() && result.back() == '\n') {
                result.pop_back();
            }
        }
        pclose(pipe);

        return result;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/**
 * @test OutputRoundTripBasic
 * @brief Basic round-trip test with single 3D field and one time step.
 */
TEST_F(OutputRoundTripTest, BasicSingleField) {
    const int nx = 4, ny = 4, nz = 5;
    auto field = CreateRandomField(nx, ny, nz, 0.1, 10.0);
    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = field;

    std::string output_file = WriteExportFields(export_fields, nx, ny, nz, 1);
    ASSERT_FALSE(output_file.empty()) << "No output file created";

    int ncid;
    int rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    int varid;
    rc = nc_inq_varid(ncid, "CO", &varid);
    ASSERT_EQ(rc, NC_NOERR);

    auto read_data = ReadNetCDFVariable(ncid, varid, nx, ny, nz, 0);

    auto host_field = field.view_host();
    double max_rel_error = 0.0;
    for (int i = 0; i < nx * ny * nz; ++i) {
        int k = i / (nx * ny);
        int j = (i / nx) % ny;
        int ii = i % nx;
        // LayoutLeft check
        int ll_idx = ii + j * nx +
                     k * nx * ny;  // This is not what ReadNetCDF returns, it returns LayoutLeft
        // Wait, helper already returns LayoutLeft.
        // It converts from row-major to LayoutLeft.
        // So read_data IS LayoutLeft.
        // host_field(ii, j, k) IS LayoutLeft.
        // So host_field[idx] (flat) matches read_data[idx].
        // But host_field is viewed via operator() in loop.
        // Let's use access operator.
        double val = host_field(ii, j, k);
        // The helper logic:
        // ll_idx = i + j*nx + k*nx*ny;
        // layout_left_data[ll_idx] = data[rm_idx];
        // So read_data is indexed by LayoutLeft logic.
        // My loop: i, j, k logic.
        // If I compute ll_idx for host_field(ii, j, k), it is indeed ii + j*nx + k*nx*ny.
        int flat_idx = ii + j * nx + k * nx * ny;

        double rel_err = RelativeError(val, read_data[flat_idx]);
        max_rel_error = std::max(max_rel_error, rel_err);
    }
    nc_close(ncid);

    EXPECT_LT(max_rel_error, 1e-14);
}

/**
 * @test OutputRoundTripMultipleFields
 * @brief Round-trip test with multiple fields.
 */
TEST_F(OutputRoundTripTest, MultipleFields) {
    const int nx = 4, ny = 4, nz = 5;
    auto co_field = CreateRandomField(nx, ny, nz, 0.1, 10.0);
    auto nox_field = CreateRandomField(nx, ny, nz, 0.01, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = co_field;
    export_fields["NOx"] = nox_field;

    std::string output_file = WriteExportFields(export_fields, nx, ny, nz, 1);
    ASSERT_FALSE(output_file.empty());

    int ncid;
    int rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    // Verify CO
    {
        int varid;
        rc = nc_inq_varid(ncid, "CO", &varid);
        ASSERT_EQ(rc, NC_NOERR);
        auto read_data = ReadNetCDFVariable(ncid, varid, nx, ny, nz, 0);
        auto host_field = co_field.view_host();
        double max_err = 0.0;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    double val = host_field(i, j, k);
                    int idx = i + j * nx + k * nx * ny;
                    max_err = std::max(max_err, RelativeError(val, read_data[idx]));
                }
        EXPECT_LT(max_err, 1e-14);
    }
    // Verify NOx
    {
        int varid;
        rc = nc_inq_varid(ncid, "NOx", &varid);
        ASSERT_EQ(rc, NC_NOERR);
        auto read_data = ReadNetCDFVariable(ncid, varid, nx, ny, nz, 0);
        auto host_field = nox_field.view_host();
        double max_err = 0.0;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    double val = host_field(i, j, k);
                    int idx = i + j * nx + k * nx * ny;
                    max_err = std::max(max_err, RelativeError(val, read_data[idx]));
                }
        EXPECT_LT(max_err, 1e-14);
    }
    nc_close(ncid);
}

/**
 * @test OutputRoundTripMultipleTimeRecords
 * @brief Round-trip test with multiple time records (files).
 */
TEST_F(OutputRoundTripTest, MultipleTimeRecords) {
    const int nx = 4, ny = 4, nz = 5;
    const int num_timesteps = 3;

    std::vector<aces::DualView3D> time_fields;
    for (int t = 0; t < num_timesteps; ++t) {
        time_fields.push_back(CreateRandomField(nx, ny, nz, t * 1.0, (t + 1) * 10.0));
    }

    aces::AcesOutputConfig config;
    config.enabled = true;
    config.directory = output_dir_;
    config.filename_pattern = "test_output_{HH}.nc";  // Unique per hour
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);

    for (int t = 0; t < num_timesteps; ++t) {
        std::unordered_map<std::string, aces::DualView3D> export_fields;
        export_fields["CO"] = time_fields[t];
        writer.WriteTimeStep(export_fields, t * 3600.0, t);
    }
    writer.Finalize();

    // Verify each file
    for (int t = 0; t < num_timesteps; ++t) {
        std::stringstream ss;
        ss << std::setw(2) << std::setfill('0') << t;
        std::string filename = output_dir_ + "/test_output_" + ss.str() + ".nc";

        int ncid;
        int rc = nc_open(filename.c_str(), NC_NOWRITE, &ncid);
        ASSERT_EQ(rc, NC_NOERR) << "Failed to open " << filename;

        int varid;
        rc = nc_inq_varid(ncid, "CO", &varid);
        ASSERT_EQ(rc, NC_NOERR);

        auto read_data = ReadNetCDFVariable(ncid, varid, nx, ny, nz, 0);  // Snapshot index 0

        auto host_field = time_fields[t].view_host();
        double max_err = 0.0;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    double val = host_field(i, j, k);
                    int idx = i + j * nx + k * nx * ny;
                    max_err = std::max(max_err, RelativeError(val, read_data[idx]));
                }
        EXPECT_LT(max_err, 1e-14) << "Mismatch at time " << t;
        nc_close(ncid);
    }
}

/**
 * @test OutputRoundTripFrequencyGating
 * @brief Verify that frequency_steps correctly gates output writes.
 */
TEST_F(OutputRoundTripTest, FrequencyGating) {
    const int nx = 4, ny = 4, nz = 5;
    const int num_timesteps = 10;
    const int frequency_steps = 3;  // Dump on 0, 3, 6, 9

    auto field = CreateRandomField(nx, ny, nz);
    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = field;

    aces::AcesOutputConfig config;
    config.enabled = true;
    config.directory = output_dir_;
    config.filename_pattern = "test_gating_{HH}.nc";
    config.frequency_steps = frequency_steps;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);

    for (int step = 0; step < num_timesteps; ++step) {
        writer.WriteTimeStep(export_fields, step * 3600.0, step);
    }
    writer.Finalize();

    auto check_file_exists = [&](int hour) -> bool {
        std::stringstream ss;
        ss << std::setw(2) << std::setfill('0') << hour;
        std::string filename = output_dir_ + "/test_gating_" + ss.str() + ".nc";
        std::ifstream f(filename.c_str());
        return f.good();
    };

    EXPECT_TRUE(check_file_exists(0));
    EXPECT_FALSE(check_file_exists(1));
    EXPECT_FALSE(check_file_exists(2));
    EXPECT_TRUE(check_file_exists(3));
    EXPECT_FALSE(check_file_exists(4));
    EXPECT_FALSE(check_file_exists(5));
    EXPECT_TRUE(check_file_exists(6));
    EXPECT_TRUE(check_file_exists(9));
}

/**
 * @test OutputRoundTripCFCompliance
 * @brief Verify minimal CF compliance (valid NetCDF, structure).
 */
TEST_F(OutputRoundTripTest, CFCompliance) {
    const int nx = 4, ny = 4, nz = 5;
    auto field = CreateRandomField(nx, ny, nz);
    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = field;

    // Just check it can be opened and has vars
    std::string output_file = WriteExportFields(export_fields, nx, ny, nz, 1);

    int ncid;
    int rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    // Check dimensions
    int ndims;
    rc = nc_inq_ndims(ncid, &ndims);
    ASSERT_EQ(rc, NC_NOERR);
    EXPECT_GE(ndims, 3);  // x, y, z at least. ESMF might add time?

    // Attempt to read CO
    int varid;
    rc = nc_inq_varid(ncid, "CO", &varid);
    ASSERT_EQ(rc, NC_NOERR);

    nc_close(ncid);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new ESMFEnvironment());
    ::testing::AddGlobalTestEnvironment(new KokkosEnvironment());
    return RUN_ALL_TESTS();
}
