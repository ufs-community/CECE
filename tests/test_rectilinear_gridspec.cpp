// SPDX-License-Identifier: Apache-2.0
// Real AMIO/AXIS/writer regression: CF bounds must survive, including polar caps.
#include <gtest/gtest.h>
#include <mpi.h>
#include <netcdf.h>

#include <Kokkos_Core.hpp>
#include <chrono>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_regridder_utils.hpp"
#include "cece/cece_standalone_writer.hpp"

namespace {
void nc_check(int rc) {
    if (rc != NC_NOERR) throw std::runtime_error(nc_strerror(rc));
}

class RectilinearGridspecTest : public ::testing::Test {
   protected:
    std::filesystem::path folder, grid;
    const std::vector<double> lon{-180., -175.}, lat{-89., -86., 89.};
    const std::vector<double> lon_bounds{-182.5, -177.5, -177.5, -172.5};
    const std::vector<double> lat_bounds{-90., -88., -88., -84., 88., 90.};

    void SetUp() override {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        folder = std::filesystem::temp_directory_path() / ("cece_cf_grid_" + std::to_string(stamp));
        ASSERT_TRUE(std::filesystem::create_directory(folder));
        grid = folder / "grid.nc";
        int f;
        nc_check(nc_create(grid.c_str(), NC_CLOBBER | NC_NETCDF4, &f));
        int x, y, nv, xv, yv, xb, yb;
        nc_check(nc_def_dim(f, "lon", 2, &x));
        nc_check(nc_def_dim(f, "lat", 3, &y));
        nc_check(nc_def_dim(f, "nv", 2, &nv));
        const int xd[]{x, nv}, yd[]{y, nv};
        nc_check(nc_def_var(f, "lon", NC_DOUBLE, 1, &x, &xv));
        nc_check(nc_def_var(f, "lat", NC_DOUBLE, 1, &y, &yv));
        // Non-default names ensure that the CF bounds attributes are followed.
        nc_check(nc_def_var(f, "longitude_edges", NC_DOUBLE, 2, xd, &xb));
        nc_check(nc_def_var(f, "latitude_edges", NC_DOUBLE, 2, yd, &yb));
        nc_check(nc_put_att_text(f, xv, "units", 12, "degrees_east"));
        nc_check(nc_put_att_text(f, yv, "units", 13, "degrees_north"));
        nc_check(nc_put_att_text(f, xv, "bounds", 15, "longitude_edges"));
        nc_check(nc_put_att_text(f, yv, "bounds", 14, "latitude_edges"));
        nc_check(nc_enddef(f));
        nc_check(nc_put_var_double(f, xv, lon.data()));
        nc_check(nc_put_var_double(f, yv, lat.data()));
        nc_check(nc_put_var_double(f, xb, lon_bounds.data()));
        nc_check(nc_put_var_double(f, yb, lat_bounds.data()));
        nc_check(nc_close(f));
    }
    void TearDown() override {
        std::filesystem::remove_all(folder);
    }
    auto mesh(int nband, int j0) {
        return cece::io::build_axis_mesh(2, nband, j0, lon, lat, grid.string());
    }
    void attribute(const char* var, const char* name, const char* value) {
        int f, v;
        nc_check(nc_open(grid.c_str(), NC_WRITE, &f));
        nc_check(nc_inq_varid(f, var, &v));
        nc_check(nc_redef(f));
        if (value)
            nc_check(nc_put_att_text(f, v, name, std::string(value).size(), value));
        else
            nc_check(nc_del_att(f, v, name));
        nc_check(nc_enddef(f));
        nc_check(nc_close(f));
    }
    void read_exact(int f, const char* name, const std::vector<double>& expected) {
        int v;
        nc_check(nc_inq_varid(f, name, &v));
        int rank, dims[NC_MAX_VAR_DIMS];
        nc_check(nc_inq_varndims(f, v, &rank));
        nc_check(nc_inq_vardimid(f, v, dims));
        size_t size = 1;
        for (int i = 0; i < rank; ++i) {
            size_t n;
            nc_check(nc_inq_dimlen(f, dims[i], &n));
            size *= n;
        }
        ASSERT_EQ(size, expected.size());
        std::vector<double> actual(size);
        nc_check(nc_get_var_double(f, v, actual.data()));
        EXPECT_EQ(actual, expected) << name;
    }
};

TEST_F(RectilinearGridspecTest, ExplicitBoundsAndSingleRowBand) {
    auto all = mesh(3, 0);
    auto nodes = all.node_coords();
    EXPECT_EQ(nodes.extent(0), 24u);
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 2; ++i) {
            const int p = 4 * (j * 2 + i);
            EXPECT_EQ(nodes(p, 0), lon_bounds[2 * i]);
            EXPECT_EQ(nodes(p + 1, 0), lon_bounds[2 * i + 1]);
            EXPECT_EQ(nodes(p, 1), lat_bounds[2 * j]);
            EXPECT_EQ(nodes(p + 3, 1), lat_bounds[2 * j + 1]);
        }
    }
    auto band = mesh(1, 2);
    EXPECT_EQ(band.node_coords().extent(0), 8u);
    EXPECT_EQ(band.node_coords()(0, 1), 88.);
    EXPECT_EQ(band.node_coords()(3, 1), 90.);
}

TEST_F(RectilinearGridspecTest, InvalidBandFails) {
    EXPECT_THROW(mesh(2, 2), std::runtime_error);
    EXPECT_THROW(mesh(1, -1), std::runtime_error);
}
TEST_F(RectilinearGridspecTest, MissingBoundsMustNotInferPolarEdges) {
    attribute("lat", "bounds", nullptr);
    EXPECT_THROW(mesh(3, 0), std::runtime_error);
}
TEST_F(RectilinearGridspecTest, InvalidBoundsShapeFails) {
    attribute("lat", "bounds", "lon");
    EXPECT_THROW(mesh(3, 0), std::runtime_error);
}
TEST_F(RectilinearGridspecTest, ProjectedUnitsMustNotBeTreatedAsDegrees) {
    attribute("lon", "units", "m");
    EXPECT_THROW(mesh(3, 0), std::runtime_error);
}
TEST_F(RectilinearGridspecTest, NonfiniteBoundsFail) {
    int f, v;
    nc_check(nc_open(grid.c_str(), NC_WRITE, &f));
    nc_check(nc_inq_varid(f, "latitude_edges", &v));
    auto bad = lat_bounds;
    bad[1] = std::numeric_limits<double>::quiet_NaN();
    nc_check(nc_put_var_double(f, v, bad.data()));
    nc_check(nc_close(f));
    EXPECT_THROW(mesh(3, 0), std::runtime_error);
}
TEST_F(RectilinearGridspecTest, WriterPreservesCentresBoundsAndFieldValues) {
    cece::CeceOutputConfig cfg;
    cfg.enabled = true;
    cfg.directory = folder.string();
    cfg.filename_pattern = "output.nc";
    cfg.frequency_steps = 1;
    cfg.fields = {{"isoprene", {{"units", "kg m-2 s-1"}}}};
    cfg.fields.SetTimeUnits("2021-06-20T12:00:00");
    cece::CeceStandaloneWriter writer(cfg);
    ASSERT_EQ(writer.InitializeWithCoords("2021-06-20T12:00:00", 2, 3, 1, lon, lat, grid.string()), 0);
    cece::DualView3D flux("isoprene", 2, 3, 1);
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 2; ++i) flux.view_host()(i, j, 0) = 1.0 + j * 2 + i;
    flux.modify_host();
    std::unordered_map<std::string, cece::DualView3D> fields{{"isoprene", flux}};
    ASSERT_EQ(writer.WriteTimeStep(fields, 3600., 0), 0);
    writer.Finalize();
    int f;
    nc_check(nc_open((folder / "output.nc").c_str(), NC_NOWRITE, &f));
    read_exact(f, "lon", lon);
    read_exact(f, "lat", lat);
    read_exact(f, "lon_bnds", lon_bounds);
    read_exact(f, "lat_bnds", lat_bounds);
    read_exact(f, "isoprene", {1., 2., 3., 4., 5., 6.});
    nc_check(nc_close(f));
}
}  // namespace
