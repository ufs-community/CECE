#include <amio/amio.h>
#include <gtest/gtest.h>
#include <mpi.h>
#include <netcdf.h>
#include <unistd.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_grid_geometry.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_regridder_utils.hpp"
#include "cece/cece_standalone_writer.hpp"
#include "cece/cece_utils.hpp"

extern "C" void amio_set_parent_communicator(MPI_Fint comm);

extern "C" {
void cece_core_write_step(void* data_ptr, double time_seconds, int step_index, int* rc);
}

extern std::unique_ptr<cece::CeceStandaloneWriter> g_standalone_writer;

namespace cece::test {

class CeceUtilsTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Kokkos and MPI are managed by the shared cece_test_main environment
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

static std::string find_grid_file(const std::string& filename) {
    // The repo's data/ dir is known at compile time (CECE_SOURCE_DIR) so the
    // lookup works from any build directory; the cwd-relative guesses remain
    // for builds without the define.
    std::vector<std::string> candidate_paths = {
#ifdef CECE_SOURCE_DIR
        std::string(CECE_SOURCE_DIR) + "/data/" + filename,
#endif
        "data/" + filename,
        "../data/" + filename,
        "tests/data/" + filename,
        "../tests/data/" + filename,
        "/work/data/" + filename,
    };
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

// A projected-grid CF file names its horizontal coordinates "x" and "y" too, but
// they are cartesian metres on an even-sized cell-centre lattice rather than the
// odd (2*n + 1) geographic-degree corner lattice of an FV3 supergrid. The
// supergrid branch must not claim such a file; it should fall through to the
// unsupported-convention error.
TEST_F(CeceUtilsTest, BuildAxisMeshRejectsProjectedXyAsSupergrid) {
    const std::filesystem::path nc_path = std::filesystem::temp_directory_path() / "cece_projected_xy_tile.nc";
    std::filesystem::remove(nc_path);

    constexpr int nx = 96, ny = 96;
    int ncid = -1;
    ASSERT_EQ(nc_create(nc_path.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid), NC_NOERR);
    int x_dim = -1, y_dim = -1;
    ASSERT_EQ(nc_def_dim(ncid, "x", nx, &x_dim), NC_NOERR);
    ASSERT_EQ(nc_def_dim(ncid, "y", ny, &y_dim), NC_NOERR);
    int x_var = -1, y_var = -1;
    const int dims[2] = {y_dim, x_dim};
    ASSERT_EQ(nc_def_var(ncid, "x", NC_DOUBLE, 2, dims, &x_var), NC_NOERR);
    ASSERT_EQ(nc_def_var(ncid, "y", NC_DOUBLE, 2, dims, &y_var), NC_NOERR);
    const char x_units[] = "m";
    const char y_units[] = "m";
    const char x_name[] = "projection_x_coordinate";
    const char y_name[] = "projection_y_coordinate";
    ASSERT_EQ(nc_put_att_text(ncid, x_var, "units", sizeof(x_units) - 1, x_units), NC_NOERR);
    ASSERT_EQ(nc_put_att_text(ncid, y_var, "units", sizeof(y_units) - 1, y_units), NC_NOERR);
    ASSERT_EQ(nc_put_att_text(ncid, x_var, "standard_name", sizeof(x_name) - 1, x_name), NC_NOERR);
    ASSERT_EQ(nc_put_att_text(ncid, y_var, "standard_name", sizeof(y_name) - 1, y_name), NC_NOERR);
    ASSERT_EQ(nc_enddef(ncid), NC_NOERR);
    std::vector<double> coords(static_cast<size_t>(nx) * ny, 0.0);
    ASSERT_EQ(nc_put_var_double(ncid, x_var, coords.data()), NC_NOERR);
    ASSERT_EQ(nc_put_var_double(ncid, y_var, coords.data()), NC_NOERR);
    ASSERT_EQ(nc_close(ncid), NC_NOERR);

    std::vector<double> dummy_lons(nx, 0.0);
    std::vector<double> dummy_lats(ny, 0.0);
    EXPECT_THROW(cece::io::build_axis_mesh(nx, ny, 0, dummy_lons, dummy_lats, nc_path.string()), std::runtime_error);

    std::filesystem::remove(nc_path);
}

// The grid_spec fixture stores its centers with the FV3 unit spellings
// ("degrees_E"/"degrees_N"), which the CF geographic check in
// load_grid_geometry rejects. Production therefore pairs a CF-unit field
// dataset with the gridspec file; this helper writes that field file by
// mirroring the spec's own grid_lont/grid_latt centers.
static std::filesystem::path write_c96_center_field_file(const std::string& spec_file) {
    constexpr int n = 96;
    int spec_id = -1;
    if (nc_open(spec_file.c_str(), NC_NOWRITE, &spec_id) != NC_NOERR) return {};
    int lon_id = -1, lat_id = -1;
    bool ok = nc_inq_varid(spec_id, "grid_lont", &lon_id) == NC_NOERR && nc_inq_varid(spec_id, "grid_latt", &lat_id) == NC_NOERR;
    std::vector<float> lon(static_cast<size_t>(n) * n), lat(static_cast<size_t>(n) * n);
    if (ok) {
        ok = nc_get_var_float(spec_id, lon_id, lon.data()) == NC_NOERR && nc_get_var_float(spec_id, lat_id, lat.data()) == NC_NOERR;
    }
    nc_close(spec_id);
    if (!ok) return {};

    // PID-unique path: ctest runs each discovered case as its own process of
    // this binary, and a shared file name would race between TearDowns.
    const auto path = std::filesystem::temp_directory_path() / ("cece_C96_tile1_centers_field_" + std::to_string(getpid()) + ".nc");
    int field_id = -1;
    ok = nc_create(path.c_str(), NC_CLOBBER | NC_NETCDF4, &field_id) == NC_NOERR;
    int lat_dim = -1, lon_dim = -1;
    if (ok) ok = nc_def_dim(field_id, "lat", n, &lat_dim) == NC_NOERR && nc_def_dim(field_id, "lon", n, &lon_dim) == NC_NOERR;
    int lon_var = -1, lat_var = -1;
    const int dims[2] = {lat_dim, lon_dim};
    if (ok)
        ok = nc_def_var(field_id, "lon", NC_DOUBLE, 2, dims, &lon_var) == NC_NOERR &&
             nc_def_var(field_id, "lat", NC_DOUBLE, 2, dims, &lat_var) == NC_NOERR;
    if (ok) {
        ok = nc_put_att_text(field_id, lon_var, "units", sizeof("degrees_east") - 1, "degrees_east") == NC_NOERR &&
             nc_put_att_text(field_id, lat_var, "units", sizeof("degrees_north") - 1, "degrees_north") == NC_NOERR && nc_enddef(field_id) == NC_NOERR;
    }
    if (ok) {
        const std::vector<double> lon_values(lon.begin(), lon.end()), lat_values(lat.begin(), lat.end());
        ok = nc_put_var_double(field_id, lon_var, lon_values.data()) == NC_NOERR &&
             nc_put_var_double(field_id, lat_var, lat_values.data()) == NC_NOERR;
    }
    if (field_id >= 0 && nc_close(field_id) != NC_NOERR) ok = false;
    if (!ok) {
        std::filesystem::remove(path);
        return {};
    }
    return path;
}

// Writes a MACCity-4x5-style global rectilinear inventory file: 1-D longitude
// (72 x 5 deg) and latitude (45 x 4 deg) axes with CF `bounds` attributes, the
// layout ufs-chem emissions inventories ship in. No field variable is needed —
// load_grid_geometry consumes only the coordinates.
static std::filesystem::path write_rectilinear_inventory_file() {
    constexpr int nx = 72;  // 5-degree longitude cells
    constexpr int ny = 45;  // 4-degree latitude cells
    const auto path = std::filesystem::temp_directory_path() / ("cece_maccity_style_inventory_" + std::to_string(getpid()) + ".nc");
    int file_id = -1;
    bool ok = nc_create(path.c_str(), NC_CLOBBER | NC_NETCDF4, &file_id) == NC_NOERR;
    int lon_dim = -1, lat_dim = -1, bnd_dim = -1;
    if (ok) {
        ok = nc_def_dim(file_id, "lon", nx, &lon_dim) == NC_NOERR && nc_def_dim(file_id, "lat", ny, &lat_dim) == NC_NOERR &&
             nc_def_dim(file_id, "bnds", 2, &bnd_dim) == NC_NOERR;
    }
    int lon_var = -1, lat_var = -1, lon_bnd_var = -1, lat_bnd_var = -1;
    const int lon_dims[2] = {lon_dim, bnd_dim};
    const int lat_dims[2] = {lat_dim, bnd_dim};
    if (ok) {
        ok = nc_def_var(file_id, "lon", NC_DOUBLE, 1, &lon_dim, &lon_var) == NC_NOERR &&
             nc_def_var(file_id, "lat", NC_DOUBLE, 1, &lat_dim, &lat_var) == NC_NOERR &&
             nc_def_var(file_id, "lon_bnds", NC_DOUBLE, 2, lon_dims, &lon_bnd_var) == NC_NOERR &&
             nc_def_var(file_id, "lat_bnds", NC_DOUBLE, 2, lat_dims, &lat_bnd_var) == NC_NOERR;
    }
    if (ok) {
        ok = nc_put_att_text(file_id, lon_var, "units", sizeof("degrees_east") - 1, "degrees_east") == NC_NOERR &&
             nc_put_att_text(file_id, lat_var, "units", sizeof("degrees_north") - 1, "degrees_north") == NC_NOERR &&
             nc_put_att_text(file_id, lon_var, "bounds", sizeof("lon_bnds") - 1, "lon_bnds") == NC_NOERR &&
             nc_put_att_text(file_id, lat_var, "bounds", sizeof("lat_bnds") - 1, "lat_bnds") == NC_NOERR && nc_enddef(file_id) == NC_NOERR;
    }
    if (ok) {
        std::vector<double> lon(nx), lat(ny), lon_bnds(2 * nx), lat_bnds(2 * ny);
        for (int i = 0; i < nx; ++i) {
            lon[i] = (i + 0.5) * (360.0 / nx);
            lon_bnds[2 * i] = i * (360.0 / nx);
            lon_bnds[2 * i + 1] = (i + 1) * (360.0 / nx);
        }
        for (int j = 0; j < ny; ++j) {
            lat[j] = -90.0 + (j + 0.5) * (180.0 / ny);
            lat_bnds[2 * j] = -90.0 + j * (180.0 / ny);
            lat_bnds[2 * j + 1] = -90.0 + (j + 1) * (180.0 / ny);
        }
        ok = nc_put_var_double(file_id, lon_var, lon.data()) == NC_NOERR && nc_put_var_double(file_id, lat_var, lat.data()) == NC_NOERR &&
             nc_put_var_double(file_id, lon_bnd_var, lon_bnds.data()) == NC_NOERR &&
             nc_put_var_double(file_id, lat_bnd_var, lat_bnds.data()) == NC_NOERR;
    }
    if (file_id >= 0 && nc_close(file_id) != NC_NOERR) ok = false;
    if (!ok) {
        std::filesystem::remove(path);
        return {};
    }
    return path;
}

// Reads the tile-1 cell centers out of the C96 grid_spec fixture with plain
// netcdf (the AMIO dataset in the fixture covers the separate field file).
static bool read_c96_tile_centers(const std::string& spec_file, std::vector<double>& lon, std::vector<double>& lat) {
    constexpr int n = 96;
    int file_id = -1;
    if (nc_open(spec_file.c_str(), NC_NOWRITE, &file_id) != NC_NOERR) return false;
    int lon_id = -1, lat_id = -1;
    bool ok = nc_inq_varid(file_id, "grid_lont", &lon_id) == NC_NOERR && nc_inq_varid(file_id, "grid_latt", &lat_id) == NC_NOERR;
    std::vector<float> lon_f(static_cast<size_t>(n) * n), lat_f(static_cast<size_t>(n) * n);
    if (ok) {
        ok = nc_get_var_float(file_id, lon_id, lon_f.data()) == NC_NOERR && nc_get_var_float(file_id, lat_id, lat_f.data()) == NC_NOERR;
    }
    nc_close(file_id);
    if (!ok) return false;
    lon.assign(lon_f.begin(), lon_f.end());
    lat.assign(lat_f.begin(), lat_f.end());
    return true;
}

// Source side of the mixed-geometry review case (ufs-community/CECE#124):
// a global rectilinear CF-bounds inventory, MACCity-4x5-style, loaded as an
// AMIO dataset so the production load_grid_geometry path produces the strip
// cell mesh.
class RectilinearInventoryFixture : public ::testing::Test {
   protected:
    amio_core_handle core{nullptr};
    amio_dataset_handle dataset{nullptr};
    std::filesystem::path inventory_file;
    std::string spec_file;

    void SetUp() override {
        spec_file = find_grid_file("C96_grid_spec.tile1.nc");
        if (!std::filesystem::exists(spec_file)) return;
        inventory_file = write_rectilinear_inventory_file();
        if (inventory_file.empty()) return;
        const auto manifest = std::string("backend: netcdf4\npath: ") + inventory_file.string() +
                              "\ndata_model: enhanced\nstaging_pool:\n  buffer_count: 2\n  buffer_capacity_bytes: 1048576\nworker_pool:\n"
                              "  threads: 1\nprefetch:\n  depth: 1\n  read_timeout_s: 30\nstaging_timeout_ms: 30000\n";
        amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
        if (amio_init_from_string(manifest.c_str(), "yaml", &core) != AMIO_OK) return;
        if (amio_open_dataset_from_string(core, manifest.c_str(), "yaml", AMIO_MODE_READ, &dataset) != AMIO_OK) {
            amio_finalize(core);
            core = nullptr;
            dataset = nullptr;
        }
    }

    void TearDown() override {
        if (dataset) amio_close_dataset(dataset);
        if (core) amio_finalize(core);
        if (!inventory_file.empty()) std::filesystem::remove(inventory_file);
    }
};

// A rectilinear inventory (ConstantLatitude/ConstantLatitudeStrip) against a
// cubed-sphere destination (GreatCircle/SphericalExcess) must pass
// prerequisite validation and build conservative weights whose spherical
// clipping is mass-exact: a constant field maps to the same constant on the
// tile, and the tile-area-weighted integral of the mapped field equals the
// analytic integral of that constant over the face. This is the reviewer's
// mixed-pair use case (MACCity-style emissions inventories onto FV3 grids).
TEST_F(RectilinearInventoryFixture, RectilinearInventoryToC96TileIsMassConservative) {
    if (!dataset) GTEST_SKIP() << "Missing test file or AMIO unavailable";
    constexpr int nx = 72, ny = 45, n = 96;

    const cece::io::GeometrySettings inventory_settings;
    const cece::io::GridGeometry source = cece::io::load_grid_geometry(dataset, inventory_settings);
    ASSERT_EQ(source.nx, nx);
    ASSERT_EQ(source.ny, ny);
    ASSERT_TRUE(source.has_cell_geometry);
    ASSERT_TRUE(source.supplied_cells);
    EXPECT_EQ(source.metadata.provenance, axis::topology::GeometryProvenance::SourceAuthoritative);
    EXPECT_EQ(source.metadata.boundary_model, axis::topology::BoundaryModel::ConstantLatitude);
    EXPECT_EQ(source.metadata.area_model, axis::topology::AreaModel::ConstantLatitudeStrip);

    std::vector<double> target_lon, target_lat;
    ASSERT_TRUE(read_c96_tile_centers(spec_file, target_lon, target_lat));

    cece::io::RegridPlan plan;
    const bool built = cece::io::build_regrid_plan(dataset, n, n, target_lon, target_lat, "consd", 0, n, "", spec_file, plan);
    ASSERT_TRUE(built) << "mixed rectilinear->cubed-sphere conservative plan was rejected";
    EXPECT_TRUE(plan.built);
    EXPECT_FALSE(plan.identity);
    EXPECT_EQ(plan.matrix.n_src(), static_cast<std::size_t>(nx) * ny);
    EXPECT_EQ(plan.matrix.n_dst(), static_cast<std::size_t>(n) * n);
    EXPECT_GT(plan.matrix.nnz(), 0u);

    // A conservative1st row sum equals one exactly when the source grid fully
    // covers the sphere, so an all-ones field must map to an all-ones tile.
    const std::vector<double> source_field(static_cast<size_t>(nx) * ny, 1.0);
    std::vector<double> mapped;
    ASSERT_TRUE(cece::io::apply_regrid_plan(plan, 0, false, source_field.data(), nx, ny, n, mapped));
    ASSERT_EQ(mapped.size(), static_cast<size_t>(n) * n);
    double max_delta = 0.0;
    for (const double value : mapped) max_delta = std::max(max_delta, std::abs(value - 1.0));
    EXPECT_LT(max_delta, 1.0e-9) << "constant field was not preserved by mixed-pair conservative weights";

    // Integral check against the AXIS-computed tile areas: the face of a
    // unit-sphere cube holds 4*pi/6 steradians, so sum(value * area) must
    // match the analytic face integral of the constant field.
    const auto tile_mesh = cece::io::load_mesh_from_file(n, n, 0, spec_file);
    const auto areas = tile_mesh.cell_areas();
    ASSERT_EQ(areas.size(), static_cast<std::size_t>(n) * n);
    double integral = 0.0, total_area = 0.0;
    for (std::size_t cell = 0; cell < areas.size(); ++cell) {
        integral += mapped[cell] * areas(cell);
        total_area += areas(cell);
    }
    EXPECT_NEAR(integral, 4.0 * M_PI / 6.0, 1.0e-6);
    EXPECT_NEAR(integral, total_area, 1.0e-6);
}

// Exercises the production load_grid_geometry -> build_regrid_plan pipeline on
// the real C96 tile-1 grid_spec fixture.
class C96GridSpecAmioFixture : public ::testing::Test {
   protected:
    amio_core_handle core{nullptr};
    amio_dataset_handle dataset{nullptr};
    std::string spec_file;
    std::filesystem::path field_file;

    void SetUp() override {
        spec_file = find_grid_file("C96_grid_spec.tile1.nc");
        if (!std::filesystem::exists(spec_file)) return;
        field_file = write_c96_center_field_file(spec_file);
        if (field_file.empty()) return;
        const auto manifest = std::string("backend: netcdf4\npath: ") + field_file.string() +
                              "\ndata_model: enhanced\nstaging_pool:\n  buffer_count: 2\n  buffer_capacity_bytes: 1048576\nworker_pool:\n"
                              "  threads: 1\nprefetch:\n  depth: 1\n  read_timeout_s: 30\nstaging_timeout_ms: 30000\n";
        amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
        if (amio_init_from_string(manifest.c_str(), "yaml", &core) != AMIO_OK) return;
        if (amio_open_dataset_from_string(core, manifest.c_str(), "yaml", AMIO_MODE_READ, &dataset) != AMIO_OK) {
            amio_finalize(core);
            core = nullptr;
            dataset = nullptr;
        }
    }

    void TearDown() override {
        if (dataset) amio_close_dataset(dataset);
        if (core) amio_finalize(core);
        if (!field_file.empty()) std::filesystem::remove(field_file);
    }
};

// Branches A-D of load_mesh_from_file must stamp authoritative great-circle
// metadata and compute cell areas, otherwise validate_regrid_prerequisites
// rejects every conservative gridspec file with "unknown provenance".
TEST_F(C96GridSpecAmioFixture, LoadMeshFromC96GridSpecStampsGreatCircleMetadata) {
    if (!std::filesystem::exists(spec_file)) GTEST_SKIP() << "Missing test file: " << spec_file;

    const auto mesh = cece::io::load_mesh_from_file(96, 96, 0, spec_file);
    ASSERT_EQ(mesh.n_cells(), static_cast<std::size_t>(96 * 96));

    const auto metadata = mesh.geometry_metadata();
    EXPECT_EQ(metadata.provenance, axis::topology::GeometryProvenance::SourceAuthoritative);
    EXPECT_EQ(metadata.boundary_model, axis::topology::BoundaryModel::GreatCircle);
    EXPECT_EQ(metadata.area_model, axis::topology::AreaModel::SphericalExcess);
    EXPECT_TRUE(metadata.longitude_periodicity.increasing);

    // compute_areas() fills the previously empty area view with unit-sphere
    // spherical-excess areas; the six cubed-sphere tiles partition the sphere,
    // so one tile sums to 4*pi/6.
    const auto areas = mesh.cell_areas();
    ASSERT_EQ(areas.size(), mesh.n_cells());
    double total = 0.0;
    for (std::size_t cell = 0; cell < mesh.n_cells(); ++cell) {
        ASSERT_GT(areas(cell), 0.0) << "cell " << cell << " has a non-positive area";
        total += areas(cell);
    }
    EXPECT_NEAR(total, 4.0 * M_PI / 6.0, 1e-6);
}

// The reviewer-facing end-to-end check: a conservative plan whose source and
// destination endpoints both resolve to C96_grid_spec.tile1.nc must pass
// prerequisite validation and produce weights.
TEST_F(C96GridSpecAmioFixture, BuildRegridPlanConservativeAcceptsC96GridSpec) {
    if (!dataset) GTEST_SKIP() << "Missing test file or AMIO unavailable: " << spec_file;

    cece::io::GeometrySettings settings;
    settings.gridspec_file = spec_file;
    const cece::io::GridGeometry geometry = cece::io::load_grid_geometry(dataset, settings);
    ASSERT_EQ(geometry.nx, 96);
    ASSERT_EQ(geometry.ny, 96);
    ASSERT_TRUE(geometry.has_cell_geometry);
    ASSERT_TRUE(geometry.supplied_cells);

    cece::io::RegridPlan plan;
    const bool built = cece::io::build_regrid_plan(dataset, geometry.nx, geometry.ny, geometry.center_lon, geometry.center_lat, "consd", 0,
                                                   geometry.ny, spec_file, spec_file, plan);
    ASSERT_TRUE(built) << "conservative plan from C96 grid_spec was rejected";
    EXPECT_TRUE(plan.built);
    EXPECT_FALSE(plan.identity);
    EXPECT_EQ(plan.file_nx, 96);
    EXPECT_EQ(plan.file_ny, 96);
    EXPECT_EQ(plan.j0, 0);
    EXPECT_EQ(plan.j1, 96);
    EXPECT_GT(plan.matrix.nnz(), 0u);
    EXPECT_EQ(plan.matrix.n_src(), static_cast<std::size_t>(96 * 96));
    EXPECT_EQ(plan.matrix.n_dst(), static_cast<std::size_t>(96 * 96));

    // Identical source/destination grids: every destination row references
    // its own source row, so the band-scoped read window covers the full grid.
    EXPECT_EQ(plan.src_j0, 0);
    EXPECT_EQ(plan.src_rows, 96);
}

}  // namespace cece::test
