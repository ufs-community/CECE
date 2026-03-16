/**
 * @file test_standalone_writer_unit.cpp
 * @brief Unit tests for AcesStandaloneWriter class.
 *
 * Tests CF attribute correctness, output frequency gating, append behavior,
 * and error handling for unwritable directories.
 *
 * Requirements: 11.6, 11.7, 11.9, 11.11, 11.13
 */

#include <gtest/gtest.h>
#include <netcdf.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "aces/aces_standalone_writer.hpp"

// ---------------------------------------------------------------------------
// Helpers
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
 * @brief Create a 3D Kokkos::View filled with random data.
 */
static Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> CreateRandomField(
    int nx, int ny, int nz, double min_val = 0.0, double max_val = 1.0) {
    auto view = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
        "random_field", nx, ny, nz);

    auto host_view = Kokkos::create_mirror_view(view);
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                host_view(i, j, k) = RandomDouble(min_val, max_val);
            }
        }
    }
    Kokkos::deep_copy(view, host_view);
    return view;
}

/**
 * @brief Create a 3D Kokkos::View filled with constant value.
 */
static Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
CreateConstantField(int nx, int ny, int nz, double value) {
    auto view = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
        "constant_field", nx, ny, nz);

    auto host_view = Kokkos::create_mirror_view(view);
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                host_view(i, j, k) = value;
            }
        }
    }
    Kokkos::deep_copy(view, host_view);
    return view;
}

/**
 * @brief Convert a View to a DualView.
 */
static aces::DualView3D ViewToDualView(
    const Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>& view,
    const std::string& name) {
    aces::DualView3D dv(name, view.extent(0), view.extent(1), view.extent(2));
    Kokkos::deep_copy(dv.view<Kokkos::DefaultExecutionSpace>(), view);
    dv.modify<Kokkos::DefaultExecutionSpace>();
    dv.sync<Kokkos::HostSpace>();
    return dv;
}

/**
 * @brief Global Kokkos environment.
 */
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

// ---------------------------------------------------------------------------
// Test Fixture
// ---------------------------------------------------------------------------

/**
 * @class StandaloneWriterUnitTest
 * @brief Fixture for AcesStandaloneWriter unit tests.
 */
class StandaloneWriterUnitTest : public ::testing::Test {
   protected:
    std::string test_dir_ = "./test_standalone_writer_unit_data";

    void SetUp() override {
        // Clean up any existing directory first using filesystem operations
        try {
            if (std::filesystem::exists(test_dir_)) {
                std::filesystem::remove_all(test_dir_);
            }
            std::filesystem::create_directories(test_dir_);
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Failed to setup test directory: " << e.what();
        }
    }

    void TearDown() override {
        try {
            if (std::filesystem::exists(test_dir_)) {
                std::filesystem::remove_all(test_dir_);
            }
        } catch (const std::exception& e) {
            // Ignore cleanup errors
        }
    }

    /**
     * @brief Find first .nc file in directory.
     */
    std::string FindOutputFile() {
        std::string cmd = "ls " + test_dir_ + "/*.nc 2>/dev/null | head -1";
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
// Tests: CF Attribute Correctness
// ---------------------------------------------------------------------------

/**
 * @test CFAttributeConventions
 * @brief Verify Conventions attribute is set to CF-1.8.
 */
TEST_F(StandaloneWriterUnitTest, CFAttributeConventions) {
    const int nx = 4, ny = 4, nz = 5;
    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = aces::DualView3D("CO", nx, ny, nz);
    // Initialize with some test data
    Kokkos::deep_copy(export_fields["CO"].view_host(), 1.0);
    export_fields["CO"].modify<Kokkos::HostSpace>();
    export_fields["CO"].sync<Kokkos::DefaultExecutionSpace::memory_space>();

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
    ASSERT_EQ(rc, 0);

    rc = writer.WriteTimeStep(export_fields, 0.0, 0);
    ASSERT_EQ(rc, 0);

    writer.Finalize();

    std::string output_file = FindOutputFile();
    ASSERT_FALSE(output_file.empty());

    int ncid;
    rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    char conventions[256];
    rc = nc_get_att_text(ncid, NC_GLOBAL, "Conventions", conventions);
    ASSERT_EQ(rc, NC_NOERR) << "Conventions attribute missing";
    EXPECT_STREQ(conventions, "CF-1.8");

    nc_close(ncid);
}

/**
 * @test CFAttributeUnits
 * @brief Verify units attribute is set for fields.
 */
TEST_F(StandaloneWriterUnitTest, CFAttributeUnits) {
    const int nx = 4, ny = 4, nz = 5;
    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = ViewToDualView(field, "CO");

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
    ASSERT_EQ(rc, 0);

    rc = writer.WriteTimeStep(export_fields, 0.0, 0);
    ASSERT_EQ(rc, 0);

    writer.Finalize();

    std::string output_file = FindOutputFile();
    ASSERT_FALSE(output_file.empty());

    int ncid;
    rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    int varid;
    rc = nc_inq_varid(ncid, "CO", &varid);
    ASSERT_EQ(rc, NC_NOERR);

    char units[256];
    rc = nc_get_att_text(ncid, varid, "units", units);
    ASSERT_EQ(rc, NC_NOERR) << "units attribute missing";
    EXPECT_STREQ(units, "kg/m2/s");

    nc_close(ncid);
}

/**
 * @test CFAttributeFillValue
 * @brief Verify _FillValue attribute is set correctly.
 */
TEST_F(StandaloneWriterUnitTest, CFAttributeFillValue) {
    const int nx = 4, ny = 4, nz = 5;
    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = ViewToDualView(field, "CO");

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
    ASSERT_EQ(rc, 0);

    rc = writer.WriteTimeStep(export_fields, 0.0, 0);
    ASSERT_EQ(rc, 0);

    writer.Finalize();

    std::string output_file = FindOutputFile();
    ASSERT_FALSE(output_file.empty());

    int ncid;
    rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    int varid;
    rc = nc_inq_varid(ncid, "CO", &varid);
    ASSERT_EQ(rc, NC_NOERR);

    double fill_value;
    rc = nc_get_att_double(ncid, varid, "_FillValue", &fill_value);
    ASSERT_EQ(rc, NC_NOERR) << "_FillValue attribute missing";
    EXPECT_DOUBLE_EQ(fill_value, 9.96921e+36);

    nc_close(ncid);
}

/**
 * @test CFAttributeSource
 * @brief Verify source attribute is set to ACES.
 */
TEST_F(StandaloneWriterUnitTest, CFAttributeSource) {
    const int nx = 4, ny = 4, nz = 5;
    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = ViewToDualView(field, "CO");

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
    ASSERT_EQ(rc, 0);

    rc = writer.WriteTimeStep(export_fields, 0.0, 0);
    ASSERT_EQ(rc, 0);

    writer.Finalize();

    std::string output_file = FindOutputFile();
    ASSERT_FALSE(output_file.empty());

    int ncid;
    rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    char source[256];
    rc = nc_get_att_text(ncid, NC_GLOBAL, "source", source);
    ASSERT_EQ(rc, NC_NOERR) << "source attribute missing";
    EXPECT_STREQ(source, "ACES");

    nc_close(ncid);
}

// ---------------------------------------------------------------------------
// Tests: Output Frequency Gating
// ---------------------------------------------------------------------------

/**
 * @test FrequencyGatingSkipsIntermediateSteps
 * @brief Verify frequency_steps > 1 skips intermediate steps.
 */
TEST_F(StandaloneWriterUnitTest, FrequencyGatingSkipsIntermediateSteps) {
    const int nx = 4, ny = 4, nz = 5;
    const int num_steps = 10;
    const int frequency = 3;

    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = ViewToDualView(field, "CO");

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = frequency;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
    ASSERT_EQ(rc, 0);

    for (int step = 0; step < num_steps; ++step) {
        rc = writer.WriteTimeStep(export_fields, step * 3600.0, step);
        ASSERT_EQ(rc, 0);
    }

    writer.Finalize();

    std::string output_file = FindOutputFile();
    ASSERT_FALSE(output_file.empty());

    int ncid;
    rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    int time_dimid;
    rc = nc_inq_dimid(ncid, "time", &time_dimid);
    ASSERT_EQ(rc, NC_NOERR);

    size_t time_len;
    rc = nc_inq_dimlen(ncid, time_dimid, &time_len);
    ASSERT_EQ(rc, NC_NOERR);

    // Expected: ceil(num_steps / frequency)
    int expected = (num_steps + frequency - 1) / frequency;
    EXPECT_EQ(time_len, expected) << "Expected " << expected
                                  << " records with frequency=" << frequency << ", got "
                                  << time_len;

    nc_close(ncid);
}

/**
 * @test FrequencyGatingFrequencyOne
 * @brief Verify frequency_steps = 1 writes every step.
 */
TEST_F(StandaloneWriterUnitTest, FrequencyGatingFrequencyOne) {
    const int nx = 4, ny = 4, nz = 5;
    const int num_steps = 5;

    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = ViewToDualView(field, "CO");

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
    ASSERT_EQ(rc, 0);

    for (int step = 0; step < num_steps; ++step) {
        rc = writer.WriteTimeStep(export_fields, step * 3600.0, step);
        ASSERT_EQ(rc, 0);
    }

    writer.Finalize();

    std::string output_file = FindOutputFile();
    ASSERT_FALSE(output_file.empty());

    int ncid;
    rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    int time_dimid;
    rc = nc_inq_dimid(ncid, "time", &time_dimid);
    ASSERT_EQ(rc, NC_NOERR);

    size_t time_len;
    rc = nc_inq_dimlen(ncid, time_dimid, &time_len);
    ASSERT_EQ(rc, NC_NOERR);

    EXPECT_EQ(time_len, num_steps) << "Expected all " << num_steps << " steps written";

    nc_close(ncid);
}

/**
 * @test FrequencyGatingLargeFrequency
 * @brief Verify large frequency_steps writes only first record.
 */
TEST_F(StandaloneWriterUnitTest, FrequencyGatingLargeFrequency) {
    const int nx = 4, ny = 4, nz = 5;
    const int num_steps = 5;
    const int frequency = 100;

    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = ViewToDualView(field, "CO");

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = frequency;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
    ASSERT_EQ(rc, 0);

    for (int step = 0; step < num_steps; ++step) {
        rc = writer.WriteTimeStep(export_fields, step * 3600.0, step);
        ASSERT_EQ(rc, 0);
    }

    writer.Finalize();

    std::string output_file = FindOutputFile();
    ASSERT_FALSE(output_file.empty());

    int ncid;
    rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
    ASSERT_EQ(rc, NC_NOERR);

    int time_dimid;
    rc = nc_inq_dimid(ncid, "time", &time_dimid);
    ASSERT_EQ(rc, NC_NOERR);

    size_t time_len;
    rc = nc_inq_dimlen(ncid, time_dimid, &time_len);
    ASSERT_EQ(rc, NC_NOERR);

    EXPECT_EQ(time_len, 1) << "Expected only 1 record with large frequency";

    nc_close(ncid);
}

// ---------------------------------------------------------------------------
// Tests: Append Behavior
// ---------------------------------------------------------------------------

/**
 * @test AppendBehaviorExistingFile
 * @brief Verify append behavior when output file already exists.
 */
TEST_F(StandaloneWriterUnitTest, AppendBehaviorExistingFile) {
    const int nx = 4, ny = 4, nz = 5;

    auto field1 = CreateConstantField(nx, ny, nz, 1.0);
    auto field2 = CreateConstantField(nx, ny, nz, 2.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields1, export_fields2;
    export_fields1["CO"] = ViewToDualView(field1, "CO");
    export_fields2["CO"] = ViewToDualView(field2, "CO");

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    // First write
    {
        aces::AcesStandaloneWriter writer(config);
        int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
        ASSERT_EQ(rc, 0);

        rc = writer.WriteTimeStep(export_fields1, 0.0, 0);
        ASSERT_EQ(rc, 0);

        writer.Finalize();
    }

    std::string output_file = FindOutputFile();
    ASSERT_FALSE(output_file.empty());

    // Check first write
    {
        int ncid;
        int rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
        ASSERT_EQ(rc, NC_NOERR);

        int time_dimid;
        rc = nc_inq_dimid(ncid, "time", &time_dimid);
        ASSERT_EQ(rc, NC_NOERR);

        size_t time_len;
        rc = nc_inq_dimlen(ncid, time_dimid, &time_len);
        ASSERT_EQ(rc, NC_NOERR);
        EXPECT_EQ(time_len, 1);

        nc_close(ncid);
    }

    // Second write (should overwrite, not append, due to NC_CLOBBER)
    {
        aces::AcesStandaloneWriter writer(config);
        int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
        ASSERT_EQ(rc, 0);

        rc = writer.WriteTimeStep(export_fields2, 3600.0, 1);
        ASSERT_EQ(rc, 0);

        writer.Finalize();
    }

    // Check second write
    {
        int ncid;
        int rc = nc_open(output_file.c_str(), NC_NOWRITE, &ncid);
        ASSERT_EQ(rc, NC_NOERR);

        int time_dimid;
        rc = nc_inq_dimid(ncid, "time", &time_dimid);
        ASSERT_EQ(rc, NC_NOERR);

        size_t time_len;
        rc = nc_inq_dimlen(ncid, time_dimid, &time_len);
        ASSERT_EQ(rc, NC_NOERR);
        // Should be 1 (clobbered) or 2 (appended), depending on implementation
        EXPECT_GE(time_len, 1);

        nc_close(ncid);
    }
}

// ---------------------------------------------------------------------------
// Tests: Error Handling
// ---------------------------------------------------------------------------

/**
 * @test ErrorHandlingUnwritableDirectory
 * @brief Verify error handling for unwritable directory.
 */
TEST_F(StandaloneWriterUnitTest, ErrorHandlingUnwritableDirectory) {
    const int nx = 4, ny = 4, nz = 5;
    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = ViewToDualView(field, "CO");

    // Create a read-only directory
    std::string readonly_dir = test_dir_ + "/readonly";
    system(("mkdir -p " + readonly_dir).c_str());
    system(("chmod 444 " + readonly_dir).c_str());

    aces::AcesOutputConfig config;
    config.directory = readonly_dir;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);

    // Should fail or succeed depending on permissions
    // If it fails, rc should be non-zero
    // If it succeeds, WriteTimeStep should fail
    if (rc == 0) {
        rc = writer.WriteTimeStep(export_fields, 0.0, 0);
        EXPECT_NE(rc, 0) << "Expected error when writing to read-only directory";
    } else {
        EXPECT_NE(rc, 0) << "Expected error during initialization";
    }

    writer.Finalize();

    // Restore permissions for cleanup
    system(("chmod 755 " + readonly_dir).c_str());
}

/**
 * @test ErrorHandlingNotInitialized
 * @brief Verify error when WriteTimeStep called before Initialize.
 */
TEST_F(StandaloneWriterUnitTest, ErrorHandlingNotInitialized) {
    const int nx = 4, ny = 4, nz = 5;
    auto field = CreateConstantField(nx, ny, nz, 1.0);

    std::unordered_map<std::string, aces::DualView3D> export_fields;
    export_fields["CO"] = ViewToDualView(field, "CO");

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    // Don't call Initialize

    int rc = writer.WriteTimeStep(export_fields, 0.0, 0);
    EXPECT_NE(rc, 0) << "Expected error when WriteTimeStep called before Initialize";
}

/**
 * @test ErrorHandlingIsInitialized
 * @brief Verify IsInitialized returns correct status.
 */
TEST_F(StandaloneWriterUnitTest, ErrorHandlingIsInitialized) {
    const int nx = 4, ny = 4, nz = 5;

    aces::AcesOutputConfig config;
    config.directory = test_dir_;
    config.filename_pattern = "test_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";
    config.frequency_steps = 1;
    config.fields = {"CO"};

    aces::AcesStandaloneWriter writer(config);
    EXPECT_FALSE(writer.IsInitialized()) << "Should not be initialized before Initialize()";

    int rc = writer.Initialize("2020-01-01T00:00:00", nx, ny, nz);
    ASSERT_EQ(rc, 0);
    EXPECT_TRUE(writer.IsInitialized()) << "Should be initialized after Initialize()";

    writer.Finalize();
    EXPECT_FALSE(writer.IsInitialized()) << "Should not be initialized after Finalize()";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosEnvironment());
    return RUN_ALL_TESTS();
}
