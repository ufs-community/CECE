#include <gtest/gtest.h>
#include <mpi.h>
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

static std::string find_grid_file(const std::string& filename) {
    std::vector<std::string> candidate_paths = {"data/" + filename, "../data/" + filename, "tests/data/" + filename, "../tests/data/" + filename,
                                                "/work/data/" + filename};
    for (const auto& path : candidate_paths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return filename;
}

TEST_F(CeceUtilsTest, BuildAxisMeshFV3GridSpec) {
    std::string spec_file = find_grid_file("C96_grid_spec.tile1.nc");
    ASSERT_TRUE(std::filesystem::exists(spec_file)) << "Missing test file: " << spec_file;

    std::vector<double> dummy_lons(96, 0.0);
    std::vector<double> dummy_lats(96, 0.0);

    auto mesh = cece::io::build_axis_mesh(96, 96, 0, dummy_lons, dummy_lats, spec_file);

    EXPECT_EQ(mesh.n_cells(), static_cast<size_t>(96 * 96));

    auto node_coords = mesh.node_coords();
    EXPECT_EQ(node_coords.extent(0), static_cast<size_t>(96 * 96 * 4));
    EXPECT_EQ(node_coords.extent(1), static_cast<size_t>(2));
}

TEST_F(CeceUtilsTest, BuildAxisMeshFV3NativeGrid) {
    std::string grid_file = find_grid_file("C96_grid.tile1.nc");
    ASSERT_TRUE(std::filesystem::exists(grid_file)) << "Missing test file: " << grid_file;

    std::vector<double> dummy_lons(96, 0.0);
    std::vector<double> dummy_lats(96, 0.0);

    auto mesh = cece::io::build_axis_mesh(96, 96, 0, dummy_lons, dummy_lats, grid_file);

    EXPECT_EQ(mesh.n_cells(), static_cast<size_t>(96 * 96));

    auto node_coords = mesh.node_coords();
    EXPECT_EQ(node_coords.extent(0), static_cast<size_t>(96 * 96 * 4));
    EXPECT_EQ(node_coords.extent(1), static_cast<size_t>(2));
}

TEST_F(CeceUtilsTest, BuildAxisMeshMPIRankSlicing) {
    std::string spec_file = find_grid_file("C96_grid_spec.tile1.nc");
    ASSERT_TRUE(std::filesystem::exists(spec_file)) << "Missing test file: " << spec_file;

    std::vector<double> dummy_lons(96, 0.0);
    std::vector<double> dummy_lats(48, 0.0);

    // Simulate Rank 0: j0 = 0, nband = 48
    auto mesh_rank0 = cece::io::build_axis_mesh(96, 48, 0, dummy_lons, dummy_lats, spec_file);
    EXPECT_EQ(mesh_rank0.n_cells(), static_cast<size_t>(96 * 48));
    EXPECT_EQ(mesh_rank0.node_coords().extent(0), static_cast<size_t>(96 * 48 * 4));

    // Simulate Rank 1: j0 = 48, nband = 48
    auto mesh_rank1 = cece::io::build_axis_mesh(96, 48, 48, dummy_lons, dummy_lats, spec_file);
    EXPECT_EQ(mesh_rank1.n_cells(), static_cast<size_t>(96 * 48));
    EXPECT_EQ(mesh_rank1.node_coords().extent(0), static_cast<size_t>(96 * 48 * 4));

    // Verify that coordinates for rank 0 and rank 1 are different (rank 1 is higher latitude band)
    auto coords0 = mesh_rank0.node_coords();
    auto coords1 = mesh_rank1.node_coords();
    EXPECT_NE(coords0(0, 1), coords1(0, 1));
}

TEST_F(CeceUtilsTest, BuildAxisMeshFastFailOnInvalidGridspec) {
    std::vector<double> dummy_lons(96, 0.0);
    std::vector<double> dummy_lats(96, 0.0);

    // Non-existent file must throw std::runtime_error
    EXPECT_THROW(cece::io::build_axis_mesh(96, 96, 0, dummy_lons, dummy_lats, "non_existent_grid.nc"), std::runtime_error);

    // Dimension mismatch must throw std::runtime_error
    std::string spec_file = find_grid_file("C96_grid_spec.tile1.nc");
    if (std::filesystem::exists(spec_file)) {
        EXPECT_THROW(cece::io::build_axis_mesh(200, 200, 0, dummy_lons, dummy_lats, spec_file), std::runtime_error);
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
