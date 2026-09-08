#include <gtest/gtest.h>
#include <mpi.h>
#include <netcdf.h>
#include <unistd.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_regridder_utils.hpp"
#include "cece/cece_standalone_writer.hpp"
#include "cece/cece_utils.hpp"

extern "C" {
void cece_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc);
}

extern std::unique_ptr<cece::CeceStandaloneWriter> g_standalone_writer;

namespace cece::test {

class CeceUtilsTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Kokkos and MPI are managed by KokkosMpiEnvironment
    }
};

TEST_F(CeceUtilsTest, WrapESMCFieldUpdatesRawData) {
    const int nx = 10;
    const int ny = 5;
    const int nz = 2;
    std::vector<double> raw_data(static_cast<size_t>(nx) * ny * nz, 0.0);

    UnmanagedHostView3D view;

    // On real ESMF, ESMC_FieldGetPtr would SegFault on a fake handle.
    // We test the Kokkos wrapping logic directly here to prove that
    // LayoutLeft and Unmanaged traits work as expected for ESMF data.
    view = UnmanagedHostView3D(raw_data.data(), nx, ny, nz);

    // Verify dimensions
    EXPECT_EQ(view.extent(0), nx);
    EXPECT_EQ(view.extent(1), ny);
    EXPECT_EQ(view.extent(2), nz);

    // Modify the Kokkos View
    view(2, 3, 1) = 42.0;

    // Verify the raw data is updated
    // Since it's LayoutLeft (Fortran order):
    // index = i + j*nx + k*nx*ny
    int expected_index = 2 + 3 * nx + 1 * nx * ny;
    EXPECT_DOUBLE_EQ(raw_data[expected_index], 42.0);

    // Also verify through the view
    EXPECT_DOUBLE_EQ(view(2, 3, 1), 42.0);
}

TEST_F(CeceUtilsTest, Hemco4x5CoordinatesAreTheSameOrderedGrid) {
    constexpr int nx = 72;
    constexpr int ny = 46;

    std::vector<double> lons(nx);
    for (int i = 0; i < nx; ++i) {
        lons[i] = -180.0 + 5.0 * i;
    }

    std::vector<double> lats;
    lats.reserve(ny);
    lats.push_back(-89.0);
    for (double lat = -86.0; lat <= 86.0; lat += 4.0) {
        lats.push_back(lat);
    }
    lats.push_back(89.0);
    ASSERT_EQ(lats.size(), static_cast<size_t>(ny));

    EXPECT_TRUE(io::same_spherical_grid_coordinates(nx, ny, lons, lats, lons, lats));

    // A 0..360 convention is equivalent only when cell ordering is unchanged.
    std::vector<double> lons_360 = lons;
    for (double& lon : lons_360) {
        if (lon < 0.0) lon += 360.0;
    }
    EXPECT_TRUE(io::same_spherical_grid_coordinates(nx, ny, lons_360, lats, lons, lats));

    std::vector<double> cyclically_shifted_lons = lons;
    std::rotate(cyclically_shifted_lons.begin(), cyclically_shifted_lons.begin() + 1, cyclically_shifted_lons.end());
    EXPECT_FALSE(io::same_spherical_grid_coordinates(nx, ny, cyclically_shifted_lons, lats, lons, lats));

    std::vector<double> shifted_lats = lats;
    shifted_lats.front() = -90.0;
    EXPECT_FALSE(io::same_spherical_grid_coordinates(nx, ny, lons, shifted_lats, lons, lats));
}

TEST_F(CeceUtilsTest, IdentityRegridPlanCopiesTheOwnedRowsExactly) {
    io::RegridPlan plan;
    plan.file_nx = 3;
    plan.file_ny = 4;
    plan.j0 = 1;
    plan.j1 = 3;
    plan.identity = true;
    plan.built = true;

    const std::vector<double> source = {0.0, 1.0, 2.0, 10.0, 11.0, 12.0, 20.0, 21.0, 22.0, 30.0, 31.0, 32.0};
    std::vector<double> destination;

    ASSERT_TRUE(io::apply_regrid_plan(plan, 0, false, source.data(), 3, 4, 3, destination));
    const std::vector<double> expected = {10.0, 11.0, 12.0, 20.0, 21.0, 22.0};
    EXPECT_EQ(destination, expected);

    plan.j0 = 0;
    plan.j1 = 4;
    const std::vector<float> source_with_offset = {-999.0F, 0.0F, 1.0F, 2.0F, 10.0F, 11.0F, 12.0F, 20.0F, 21.0F, 22.0F, 30.0F, 31.0F, 32.0F};
    ASSERT_TRUE(io::apply_regrid_plan(plan, 1, true, source_with_offset.data(), 3, 4, 3, destination));
    EXPECT_EQ(destination, source);
}

TEST_F(CeceUtilsTest, IdentityRegridPlanRejectsDimensionsDifferentFromVerifiedGrid) {
    io::RegridPlan plan;
    plan.file_nx = 3;
    plan.file_ny = 4;
    plan.j0 = 0;
    plan.j1 = 4;
    plan.identity = true;
    plan.built = true;

    const std::vector<double> source(12, 1.0);
    std::vector<double> destination;
    EXPECT_FALSE(io::apply_regrid_plan(plan, 0, false, source.data(), 2, 4, 3, destination));
}

TEST_F(CeceUtilsTest, StandaloneWriterPreservesHemco4x5CoordinateCenters) {
    constexpr int nx = 72;
    constexpr int ny = 46;
    const std::string test_dir = "test_output_dir_hemco_coords_" + std::to_string(getpid());

    std::vector<double> lons(nx);
    for (int i = 0; i < nx; ++i) {
        lons[i] = -180.0 + 5.0 * i;
    }
    std::vector<double> lats;
    lats.reserve(ny);
    lats.push_back(-89.0);
    for (double lat = -86.0; lat <= 86.0; lat += 4.0) {
        lats.push_back(lat);
    }
    lats.push_back(89.0);

    CeceOutputConfig config;
    config.enabled = true;
    config.directory = test_dir;
    config.filename_pattern = "hemco_grid.nc";
    config.fields = {{"test_field", {}}};
    config.fields.SetTimeUnits("2021-06-20T12:00:00");

    CeceStandaloneWriter writer(config);
    ASSERT_EQ(writer.InitializeWithCoords("2021-06-20T12:00:00", nx, ny, 1, lons, lats), 0);

    DualView3D test_field("test_field", nx, ny, 1);
    test_field.sync<Kokkos::HostSpace>();
    auto test_field_host = test_field.view_host();
    Kokkos::deep_copy(test_field_host, 0.0);
    test_field.modify<Kokkos::HostSpace>();
    std::unordered_map<std::string, DualView3D> fields;
    fields["test_field"] = test_field;
    ASSERT_EQ(writer.WriteTimeStep(fields, 3600.0, 1), 0);
    writer.Finalize();

    const std::string output_path = test_dir + "/hemco_grid.nc";
    int ncid = -1;
    ASSERT_EQ(nc_open(output_path.c_str(), NC_NOWRITE, &ncid), NC_NOERR);

    int lon_varid = -1;
    int lat_varid = -1;
    ASSERT_EQ(nc_inq_varid(ncid, "lon", &lon_varid), NC_NOERR);
    ASSERT_EQ(nc_inq_varid(ncid, "lat", &lat_varid), NC_NOERR);
    std::vector<double> written_lons(nx);
    std::vector<double> written_lats(ny);
    ASSERT_EQ(nc_get_var_double(ncid, lon_varid, written_lons.data()), NC_NOERR);
    ASSERT_EQ(nc_get_var_double(ncid, lat_varid, written_lats.data()), NC_NOERR);
    EXPECT_EQ(written_lons, lons);
    EXPECT_EQ(written_lats, lats);
    EXPECT_EQ(nc_close(ncid), NC_NOERR);

    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
}

TEST_F(CeceUtilsTest, StandaloneWriterDuplicateCoordsDetection) {
    std::string test_dir = "test_output_dir_" + std::to_string(getpid());
    CeceOutputConfig config;
    config.enabled = true;
    config.directory = test_dir;

    CeceStandaloneWriter writer(config);

    // Standard non-duplicate coordinates
    std::vector<double> lon_ok = {-180.0, -90.0, 0.0, 90.0};
    std::vector<double> lat_ok = {-90.0, -45.0, 0.0, 45.0};

    // 1. Unique coordinates should succeed
    int rc_ok = writer.InitializeWithCoords("2026-06-29T12:00:00", 4, 4, 1, lon_ok, lat_ok);
    EXPECT_EQ(rc_ok, 0);
    writer.Finalize();

    // 2. Duplicate longitudes should fail
    std::vector<double> lon_dup = {-180.0, 0.0, 0.0, 90.0};
    int rc_lon_dup = writer.InitializeWithCoords("2026-06-29T12:00:00", 4, 4, 1, lon_dup, lat_ok);
    EXPECT_EQ(rc_lon_dup, -1);
    writer.Finalize();

    // 3. Duplicate latitudes should fail
    std::vector<double> lat_dup = {-90.0, 0.0, 0.0, 45.0};
    int rc_lat_dup = writer.InitializeWithCoords("2026-06-29T12:00:00", 4, 4, 1, lon_ok, lat_dup);
    EXPECT_EQ(rc_lat_dup, -1);
    writer.Finalize();

    // Cleanup output directory if created
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
}

TEST_F(CeceUtilsTest, StandaloneWriterCustomCommunicator) {
    std::string test_dir = "test_output_dir_comm_" + std::to_string(getpid());
    CeceOutputConfig config;
    config.enabled = true;
    config.directory = test_dir;

    MPI_Comm custom_comm;
    MPI_Comm_dup(MPI_COMM_SELF, &custom_comm);

    CeceStandaloneWriter writer(config, custom_comm);

    std::vector<double> lon_ok = {-180.0, -90.0, 0.0, 90.0};
    std::vector<double> lat_ok = {-90.0, -45.0, 0.0, 45.0};

    int rc = writer.InitializeWithCoords("2026-06-29T12:00:00", 4, 4, 1, lon_ok, lat_ok);
    EXPECT_EQ(rc, 0);
    writer.Finalize();

    MPI_Comm_free(&custom_comm);

    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
}

TEST_F(CeceUtilsTest, StandaloneWriterDuplicateFieldsFiltering) {
    std::string test_dir = "test_output_dir_fields_" + std::to_string(getpid());
    CeceOutputConfig config;
    config.enabled = true;
    config.directory = test_dir;
    // Coordinate variables are seeded automatically (configuring them is a
    // duplicate-name error); only the data field is listed.
    config.fields = {{"test_field", {}}};
    // Hand-built configs (no ParseConfig) set time's units themselves.
    config.fields.SetTimeUnits("2020-01-01T00:00:00");

    CeceStandaloneWriter writer(config);

    std::vector<double> lon_ok = {-180.0, -90.0, 0.0, 90.0};
    std::vector<double> lat_ok = {-90.0, -45.0, 0.0, 45.0};

    int rc = writer.InitializeWithCoords("2026-06-29T12:00:00", 4, 4, 1, lon_ok, lat_ok);
    ASSERT_EQ(rc, 0);

    // Create a mock field map
    std::unordered_map<std::string, DualView3D> fields;
    DualView3D test_field_view("test_field", 4, 4, 1);
    test_field_view.sync<Kokkos::HostSpace>();
    auto h_view = test_field_view.view_host();
    Kokkos::deep_copy(h_view, 1.0);
    test_field_view.modify<Kokkos::HostSpace>();

    // Also put coordinate fields if they are present in the map to see if they are skipped
    DualView3D lon_field_view("lon", 4, 4, 1);
    fields["test_field"] = test_field_view;
    fields["lon"] = lon_field_view;  // This should be skipped safely!

    // Write a time step
    int rc_write = writer.WriteTimeStep(fields, 0.0, 0);
    EXPECT_EQ(rc_write, 0);  // Should succeed and write test_field, skipping lon!

    writer.Finalize();

    // Verify file exists
    std::string expected_file = test_dir + "/cece_output_20260629_120000.nc";
    EXPECT_TRUE(std::filesystem::exists(expected_file));

    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
}

TEST_F(CeceUtilsTest, CoreWriteStepSkipsInitialStep) {
    std::string test_dir = "test_output_dir_core_write_" + std::to_string(getpid());

    // Create internal data and configure output
    cece::CeceInternalData d;
    d.standalone_mode = true;
    d.config.output_config.enabled = true;
    d.config.output_config.directory = test_dir;
    d.config.output_config.fields = {{"test_field", {}}};
    // Hand-built configs (no ParseConfig) set time's units themselves.
    d.config.output_config.fields.SetTimeUnits("2020-01-01T00:00:00");
    d.config.output_config.frequency_steps = 1;
    d.config.output_config.filename_pattern = "cece_output_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc";

    // Setup coordinates and field data
    d.nx = 4;
    d.ny = 4;
    d.nz = 1;
    std::vector<double> lon_ok = {-180.0, -90.0, 0.0, 90.0};
    std::vector<double> lat_ok = {-90.0, -45.0, 0.0, 45.0};

    // Ensure directory does not exist before initialization
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }

    // Initialize global standalone writer
    g_standalone_writer = std::make_unique<cece::CeceStandaloneWriter>(d.config.output_config);
    int rc_init = g_standalone_writer->InitializeWithCoords("2026-06-29T12:00:00", 4, 4, 1, lon_ok, lat_ok);
    ASSERT_EQ(rc_init, 0);

    // Create a mock field map in d.export_state
    DualView3D test_field_view("test_field", 4, 4, 1);
    test_field_view.sync<Kokkos::HostSpace>();
    auto h_view = test_field_view.view_host();
    Kokkos::deep_copy(h_view, 1.0);
    test_field_view.modify<Kokkos::HostSpace>();
    d.export_state.fields["test_field"] = test_field_view;

    // 1. Call cece_core_write_step for step 0 / time 0.0 -> Should be SKIPPED!
    int rc = -1;
    cece_core_write_step(&d, 0.0, 0, &rc);
    EXPECT_EQ(rc, 0);

    // Verify that NO NetCDF files were created inside test_dir
    int file_count_step0 = 0;
    if (std::filesystem::exists(test_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
            if (entry.path().extension() == ".nc") {
                file_count_step0++;
            }
        }
    }
    EXPECT_EQ(file_count_step0, 0);

    // 2. Call cece_core_write_step for step 1 / time 3600.0 -> Should NOT be skipped!
    cece_core_write_step(&d, 3600.0, 1, &rc);
    EXPECT_EQ(rc, 0);

    // Verify that a file was successfully written in test_dir
    int file_count_step1 = 0;
    if (std::filesystem::exists(test_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
            if (entry.path().extension() == ".nc") {
                file_count_step1++;
            }
        }
    }
    EXPECT_EQ(file_count_step1, 1);

    // Finalize and cleanup
    g_standalone_writer.reset();

    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
}

}  // namespace cece::test

// Custom GTest Environment to manage Kokkos & MPI lifecycle globally
class KokkosMpiEnvironment : public ::testing::Environment {
   private:
    int argc_;
    char** argv_;

   public:
    KokkosMpiEnvironment(int argc, char** argv) : argc_(argc), argv_(argv) {}

    void SetUp() override {
        // Initialize MPI first
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc_, &argv_, MPI_THREAD_MULTIPLE, &provided);
        }

        // Initialize Kokkos
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize(argc_, argv_);
        }
    }
    void TearDown() override {
        // Finalize Kokkos
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }

        // Finalize MPI
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized) {
            MPI_Finalize();
        }
    }
};

int main(int argc, char** argv) {
    bool is_discovery = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gtest_list_tests") {
            is_discovery = true;
            break;
        }
    }

    if (!is_discovery) {
        // Prevent Intel MPI from detecting Slurm and attempting PMI/PMIX process manager bootstrap during unit tests
        unsetenv("SLURM_JOB_ID");
        unsetenv("SLURM_STEP_ID");
        unsetenv("PMI_RANK");
        unsetenv("PMI_SIZE");

        // Configure Intel MPI to allow standalone, local-only execution on login nodes (prevent PMI2/Hydra aborts)
        setenv("I_MPI_HYDRA_BOOTSTRAP", "none", 0);
        setenv("I_MPI_SHM", "disable", 0);

        // Initialize MPI to check rank and prevent parallel duplicate execution conflicts of local unit tests
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
        }
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rank > 0) {
            MPI_Finalize();
            return 0;
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosMpiEnvironment(argc, argv));
    return RUN_ALL_TESTS();
}
