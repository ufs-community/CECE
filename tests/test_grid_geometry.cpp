#include <amio/amio.h>
#include <gtest/gtest.h>
#include <mpi.h>
#include <netcdf.h>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "cece/cece_grid_geometry.hpp"
#include "cece/cece_regridder_utils.hpp"

extern "C" void amio_set_parent_communicator(MPI_Fint comm);

namespace {
void nc_check(int rc) {
    if (rc != NC_NOERR) throw std::runtime_error(nc_strerror(rc));
}

class AmioGeometryFixture : public ::testing::Test {
   protected:
    std::filesystem::path directory;
    amio_core_handle core{nullptr};
    amio_dataset_handle dataset{nullptr};
    bool usable{false};

    std::filesystem::path make_cf_bounds_file(bool malformed_latitude_bounds = false) {
        const auto path = directory / "cf_bounds.nc";
        int file = -1, lon_dim = -1, lat_dim = -1, vertex_dim = -1;
        int lon_var = -1, lat_var = -1, lon_bounds_var = -1, lat_bounds_var = -1;
        nc_check(nc_create(path.c_str(), NC_CLOBBER | NC_NETCDF4, &file));
        nc_check(nc_def_dim(file, "lon", 3, &lon_dim));
        nc_check(nc_def_dim(file, "lat", 2, &lat_dim));
        nc_check(nc_def_dim(file, "vertices", 2, &vertex_dim));
        const int lon_dims[]{lon_dim}, lat_dims[]{lat_dim}, lon_bounds_dims[]{lon_dim, vertex_dim}, lat_bounds_dims[]{lat_dim, vertex_dim};
        nc_check(nc_def_var(file, "lon", NC_DOUBLE, 1, lon_dims, &lon_var));
        nc_check(nc_def_var(file, "lat", NC_DOUBLE, 1, lat_dims, &lat_var));
        nc_check(nc_def_var(file, "lon_bnds", NC_DOUBLE, 2, lon_bounds_dims, &lon_bounds_var));
        nc_check(nc_def_var(file, "lat_bnds", NC_DOUBLE, 2, lat_bounds_dims, &lat_bounds_var));
        nc_check(nc_put_att_text(file, lon_var, "units", 12, "degrees_east"));
        nc_check(nc_put_att_text(file, lat_var, "units", 13, "degrees_north"));
        nc_check(nc_put_att_text(file, lon_var, "bounds", 8, "lon_bnds"));
        nc_check(nc_put_att_text(file, lat_var, "bounds", 8, "lat_bnds"));
        nc_check(nc_enddef(file));
        const double lon[]{120.0, 0.0, -120.0}, lat[]{45.0, -45.0};
        const double lon_bounds[]{180.0, 60.0, 60.0, -60.0, -60.0, -180.0};
        const double lat_bounds[]{0.0, 90.0, -90.0, 0.0};
        nc_check(nc_put_var_double(file, lon_var, lon));
        nc_check(nc_put_var_double(file, lat_var, lat));
        nc_check(nc_put_var_double(file, lon_bounds_var, lon_bounds));
        nc_check(nc_put_var_double(file, lat_bounds_var, lat_bounds));
        nc_check(nc_close(file));
        if (malformed_latitude_bounds) {
            nc_check(nc_open(path.c_str(), NC_WRITE, &file));
            nc_check(nc_inq_varid(file, "lat", &lat_var));
            nc_check(nc_redef(file));
            nc_check(nc_put_att_text(file, lat_var, "bounds", 8, "lon_bnds"));
            nc_check(nc_enddef(file));
            nc_check(nc_close(file));
        }
        return path;
    }

    void SetUp() override {
        directory = std::filesystem::temp_directory_path() /
                    ("cece_geometry_" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto input = directory / "centers.nc";
        int file = -1, lon_dim = -1, lat_dim = -1, lon_var = -1, lat_var = -1;
        bool ok = nc_create(input.c_str(), NC_CLOBBER | NC_NETCDF4, &file) == NC_NOERR;
        ok = ok && nc_def_dim(file, "lon", 3, &lon_dim) == NC_NOERR && nc_def_dim(file, "lat", 2, &lat_dim) == NC_NOERR;
        ok = ok && nc_def_var(file, "lon", NC_SHORT, 1, &lon_dim, &lon_var) == NC_NOERR &&
             nc_def_var(file, "lat", NC_SHORT, 1, &lat_dim, &lat_var) == NC_NOERR;
        const double scale = 0.5;
        ok = ok && nc_put_att_text(file, lon_var, "units", 12, "degrees_east") == NC_NOERR &&
             nc_put_att_text(file, lat_var, "units", 13, "degrees_north") == NC_NOERR &&
             nc_put_att_double(file, lon_var, "scale_factor", NC_DOUBLE, 1, &scale) == NC_NOERR &&
             nc_put_att_double(file, lat_var, "scale_factor", NC_DOUBLE, 1, &scale) == NC_NOERR && nc_enddef(file) == NC_NOERR;
        const short lon_values[]{-4, 0, 4};
        const short lat_values[]{-2, 2};
        ok = ok && nc_put_var_short(file, lon_var, lon_values) == NC_NOERR && nc_put_var_short(file, lat_var, lat_values) == NC_NOERR;
        if (file >= 0) ok = (nc_close(file) == NC_NOERR) && ok;
        if (!ok) return;

        const auto manifest = std::string("backend: netcdf4\npath: ") + input.string() +
                              "\ndata_model: enhanced\nstaging_pool:\n  buffer_count: 2\n  buffer_capacity_bytes: 1048576\nworker_pool:\n  threads: "
                              "1\nprefetch:\n  depth: 1\n  read_timeout_s: 30\nstaging_timeout_ms: 30000\n";
        amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
        if (amio_init_from_string(manifest.c_str(), "yaml", &core) != AMIO_OK) return;
        if (amio_open_dataset_from_string(core, manifest.c_str(), "yaml", AMIO_MODE_READ, &dataset) != AMIO_OK) return;
        usable = dataset != nullptr;
    }

    void TearDown() override {
        if (dataset) amio_close_dataset(dataset);
        if (core) amio_finalize(core);
        std::filesystem::remove_all(directory);
    }
};
}  // namespace

TEST(GridGeometryLoading, NullDatasetFailsWithGeometryStageDiagnostic) {
    try {
        (void)cece::io::load_grid_geometry(nullptr);
        FAIL() << "expected null AMIO dataset to fail";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.stage(), cece::io::RegridStage::GeometryLoad);
        EXPECT_EQ(error.role(), cece::io::GridRole::Source);
    }
}

TEST_F(AmioGeometryFixture, LoadsPackedCentersWithoutAlgorithmSpecificGeometry) {
    if (!usable) GTEST_SKIP() << "AMIO could not open the generated NetCDF geometry fixture";
    const auto first = cece::io::load_grid_geometry(dataset);
    const auto second = cece::io::load_grid_geometry(dataset);
    EXPECT_EQ(first.nx, 3);
    EXPECT_EQ(first.ny, 2);
    EXPECT_EQ(first.center_lon, (std::vector<double>{-2.0, 0.0, 2.0}));
    EXPECT_EQ(first.center_lat, (std::vector<double>{-1.0, 1.0}));
    EXPECT_FALSE(first.has_cell_geometry);
    EXPECT_EQ(first.center_lon, second.center_lon);
    EXPECT_EQ(first.center_lat, second.center_lat);
}

TEST_F(AmioGeometryFixture, LoadsDescendingNonuniformCfBoundsAsAuthoritativeCells) {
    if (!usable) GTEST_SKIP() << "AMIO could not open the generated NetCDF geometry fixture";
    const auto path = make_cf_bounds_file();
    const auto manifest = std::string("backend: netcdf4\npath: ") + path.string() +
                          "\ndata_model: enhanced\nstaging_pool:\n  buffer_count: 2\n  buffer_capacity_bytes: 1048576\nworker_pool:\n  threads: "
                          "1\nprefetch:\n  depth: 1\n  read_timeout_s: 30\nstaging_timeout_ms: 30000\n";
    amio_dataset_handle bounds_dataset = nullptr;
    ASSERT_EQ(amio_open_dataset_from_string(core, manifest.c_str(), "yaml", AMIO_MODE_READ, &bounds_dataset), AMIO_OK);
    const auto geometry = cece::io::load_grid_geometry(bounds_dataset);
    ASSERT_EQ(amio_close_dataset(bounds_dataset), AMIO_OK);
    EXPECT_EQ(geometry.center_lon, (std::vector<double>{120.0, 0.0, -120.0}));
    EXPECT_EQ(geometry.center_lat, (std::vector<double>{45.0, -45.0}));
    EXPECT_EQ(geometry.corner_lon, (std::vector<double>{180.0, 60.0, 60.0, -60.0, -60.0, -180.0}));
    EXPECT_EQ(geometry.corner_lat, (std::vector<double>{0.0, 90.0, -90.0, 0.0}));
    EXPECT_TRUE(geometry.has_explicit_bounds);
    ASSERT_TRUE(geometry.supplied_cells);
    EXPECT_EQ(geometry.supplied_cells->n_cells(), 6u);
    EXPECT_EQ(geometry.metadata.provenance, axis::topology::GeometryProvenance::SourceAuthoritative);
}

TEST_F(AmioGeometryFixture, MalformedAdvertisedCfBoundsFailWithoutFallback) {
    if (!usable) GTEST_SKIP() << "AMIO could not open the generated NetCDF geometry fixture";
    const auto path = make_cf_bounds_file(true);
    const auto manifest = std::string("backend: netcdf4\npath: ") + path.string() +
                          "\ndata_model: enhanced\nstaging_pool:\n  buffer_count: 2\n  buffer_capacity_bytes: 1048576\nworker_pool:\n  threads: "
                          "1\nprefetch:\n  depth: 1\n  read_timeout_s: 30\nstaging_timeout_ms: 30000\n";
    amio_dataset_handle bounds_dataset = nullptr;
    ASSERT_EQ(amio_open_dataset_from_string(core, manifest.c_str(), "yaml", AMIO_MODE_READ, &bounds_dataset), AMIO_OK);
    EXPECT_THROW(cece::io::load_grid_geometry(bounds_dataset), cece::io::RegridError);
    ASSERT_EQ(amio_close_dataset(bounds_dataset), AMIO_OK);
}

TEST_F(AmioGeometryFixture, BundledMacCityFixtureIsRejectedAsNonAuthoritative) {
    if (!usable) GTEST_SKIP() << "AMIO could not open the generated NetCDF geometry fixture";
    const auto path = (std::filesystem::path(__FILE__).parent_path().parent_path() / "data/speciation/MACCity_4x5.nc").lexically_normal();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "the local negative MACCity fixture is not present";
    const auto manifest = std::string("backend: netcdf4\npath: ") + path.string() +
                          "\ndata_model: enhanced\nstaging_pool:\n  buffer_count: 2\n  buffer_capacity_bytes: 1048576\nworker_pool:\n  threads: "
                          "1\nprefetch:\n  depth: 1\n  read_timeout_s: 30\nstaging_timeout_ms: 30000\n";
    amio_dataset_handle mac_city = nullptr;
    ASSERT_EQ(amio_open_dataset_from_string(core, manifest.c_str(), "yaml", AMIO_MODE_READ, &mac_city), AMIO_OK);
    const auto geometry = cece::io::load_grid_geometry(mac_city);
    ASSERT_EQ(amio_close_dataset(mac_city), AMIO_OK);
    EXPECT_FALSE(geometry.has_explicit_bounds);
    EXPECT_FALSE(geometry.has_cell_geometry);
    cece::io::GeometrySettings reconstruction;
    reconstruction.reconstruction = cece::io::ReconstructionModel::RectilinearMidpoint;
    EXPECT_THROW(cece::io::prepare_grid_geometry(std::make_shared<const cece::io::GridGeometry>(geometry), reconstruction), cece::io::RegridError);
}

TEST(GridGeometryPreparation, CenterOnlyByDefault) {
    auto geometry = std::make_shared<cece::io::GridGeometry>();
    geometry->nx = 2;
    geometry->ny = 2;
    geometry->center_lon = {0.0, 4.0};
    geometry->center_lat = {-3.0, 5.0};
    const auto prepared = cece::io::prepare_grid_geometry(geometry, {});
    EXPECT_FALSE(prepared.cells.has_value());
    EXPECT_EQ(prepared.geometry->center_lon, geometry->center_lon);
    EXPECT_EQ(prepared.geometry->center_lat, geometry->center_lat);
}

TEST(GridGeometryPreparation, ExplicitMidpointDeclarationPreservesCentersAndProvenance) {
    auto geometry = std::make_shared<cece::io::GridGeometry>();
    geometry->nx = 2;
    geometry->ny = 2;
    geometry->center_lon = {0.0, 4.0};
    geometry->center_lat = {-3.0, 5.0};
    cece::io::GeometrySettings settings;
    settings.reconstruction = cece::io::ReconstructionModel::RectilinearMidpoint;
    const auto prepared = cece::io::prepare_grid_geometry(geometry, settings);
    ASSERT_TRUE(prepared.cells.has_value());
    EXPECT_EQ(prepared.cells->n_cells(), 4u);
    EXPECT_EQ(prepared.metadata.provenance, axis::topology::GeometryProvenance::DeclaredGridModel);
    EXPECT_EQ(prepared.geometry->center_lon, (std::vector<double>{0.0, 4.0}));
    EXPECT_FALSE(geometry->has_cell_geometry);
}

TEST(GridGeometryPreparation, ExplicitCornerLatticePreservesSourceProvenance) {
    auto geometry = std::make_shared<cece::io::GridGeometry>();
    geometry->nx = 2;
    geometry->ny = 2;
    geometry->center_lon = {0.0, 2.0};
    geometry->center_lat = {-1.0, 1.0};
    geometry->corner_lon = {-1.0, 1.0, 3.0, -1.0, 1.0, 3.0, -1.0, 1.0, 3.0};
    geometry->corner_lat = {-2.0, -2.0, -2.0, 0.0, 0.0, 0.0, 2.0, 2.0, 2.0};
    geometry->has_explicit_bounds = true;

    const auto prepared = cece::io::prepare_grid_geometry(geometry, {});
    ASSERT_TRUE(prepared.cells.has_value());
    EXPECT_EQ(prepared.metadata.provenance, axis::topology::GeometryProvenance::SourceAuthoritative);
    EXPECT_EQ(prepared.geometry->center_lon, (std::vector<double>{0.0, 2.0}));
}

TEST(GridGeometryPreparation, MalformedAdvertisedBoundsFailAtLoadStage) {
    auto geometry = std::make_shared<cece::io::GridGeometry>();
    geometry->nx = 2;
    geometry->ny = 2;
    geometry->center_lon = {0.0, 4.0};
    geometry->center_lat = {-3.0, 5.0};
    geometry->has_explicit_bounds = true;
    try {
        (void)cece::io::prepare_grid_geometry(geometry, {});
        FAIL() << "expected malformed bounds error";
    } catch (const cece::io::RegridError& error) {
        EXPECT_EQ(error.stage(), cece::io::RegridStage::GeometryLoad);
    }
}

TEST(GridGeometryPartition, SelectsGlobalRowsAndRebasesConnectivity) {
    using Mesh = axis::topology::UnstructuredMesh<Kokkos::HostSpace>;
    auto geometry = std::make_shared<cece::io::GridGeometry>();
    geometry->nx = 2;
    geometry->ny = 2;
    geometry->center_lon = {0.0, 1.0};
    geometry->center_lat = {0.0, 1.0};
    geometry->metadata.provenance = axis::topology::GeometryProvenance::SourceAuthoritative;
    Kokkos::View<double**, Kokkos::LayoutLeft, Kokkos::HostSpace> coords("partition_fixture_nodes", 16, 2);
    Kokkos::View<axis::index_t*, Kokkos::HostSpace> offsets("partition_fixture_offsets", 5);
    Kokkos::View<axis::index_t*, Kokkos::HostSpace> indices("partition_fixture_indices", 16);
    Kokkos::View<double*, Kokkos::HostSpace> areas("partition_fixture_areas", 4);
    Kokkos::View<int*, Kokkos::HostSpace> masks("partition_fixture_masks", 4);
    for (int cell = 0; cell < 4; ++cell) {
        offsets(cell) = 4 * cell;
        areas(cell) = 10.0 + cell;
        masks(cell) = cell == 2 ? 0 : 1;
        for (int corner = 0; corner < 4; ++corner) {
            const int node = 4 * cell + corner;
            coords(node, 0) = node;
            coords(node, 1) = -node;
            indices(node) = node;
        }
    }
    offsets(4) = 16;
    geometry->supplied_cells =
        std::make_shared<const Mesh>(std::move(coords), std::move(offsets), std::move(indices), axis::topology::CoordinateSystem::SphericalDeg,
                                     std::move(areas), std::move(masks), geometry->metadata);

    const auto band = cece::io::partition_grid_geometry(geometry, 1, 2);
    ASSERT_TRUE(band.cells.has_value());
    EXPECT_EQ(band.global_cell_indices, (std::vector<std::size_t>{2, 3}));
    EXPECT_EQ(band.cells->n_cells(), 2u);
    EXPECT_EQ(band.cells->n_nodes(), 8u);
    EXPECT_EQ(band.cells->geometry_metadata().provenance, geometry->metadata.provenance);
    const auto local_offsets = band.cells->conn_offsets();
    const auto local_indices = band.cells->conn_indices();
    EXPECT_EQ(local_offsets(0), 0);
    EXPECT_EQ(local_offsets(1), 4);
    EXPECT_EQ(local_offsets(2), 8);
    for (std::size_t k = 0; k < 8; ++k) EXPECT_EQ(local_indices(k), static_cast<axis::index_t>(k));
    EXPECT_DOUBLE_EQ(band.cells->cell_areas()(0), 12.0);
    EXPECT_EQ(band.cells->cell_mask()(0), 0);

    const auto empty = cece::io::partition_grid_geometry(geometry, 2, 2);
    ASSERT_TRUE(empty.cells.has_value());
    EXPECT_TRUE(empty.global_cell_indices.empty());
    EXPECT_EQ(empty.cells->n_cells(), 0u);
}
