#include <gtest/gtest.h>
#include <netcdf.h>

#include <Kokkos_Core.hpp>
#include <cstdio>
#include <fstream>
#include <vector>

#include "aces/aces_config.hpp"
#include "aces/aces_data_ingestor.hpp"

// No mocks. Testing the real bridge and CDEPS layer.

namespace aces {
namespace test {

/**
 * @brief Helper to create a small NetCDF file for testing CDEPS configuration.
 */
void CreateSyntheticNetCDF(const char* filename, int nx, int ny) {
    int ncid, x_dimid, y_dimid, varid;
    int dims[2];
    nc_create(filename, NC_CLOBBER | NC_NETCDF4, &ncid);
    nc_def_dim(ncid, "lon", nx, &x_dimid);
    nc_def_dim(ncid, "lat", ny, &y_dimid);
    dims[0] = x_dimid;
    dims[1] = y_dimid;
    nc_def_var(ncid, "CO", NC_DOUBLE, 2, dims, &varid);
    nc_enddef(ncid);

    std::vector<double> data(nx * ny);
    for (int i = 0; i < nx * ny; ++i) {
        data[i] = static_cast<double>(i + 1);  // Non-zero, non-uniform
    }
    nc_put_var_double(ncid, varid, data.data());
    nc_close(ncid);
}

class CdepsReadTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

TEST_F(CdepsReadTest, FullReadVerification) {
    const char* test_nc = "test_data.nc";
    int nx = 10, ny = 10, nz = 1;
    CreateSyntheticNetCDF(test_nc, nx, ny);

    AcesDataIngestor ingestor;
    AcesCdepsConfig config;
    CdepsStreamConfig s1;
    s1.name = "MACCITY";
    s1.file_path = test_nc;
    s1.variables = {"CO"};
    config.streams.push_back(s1);

    // Initialize CDEPS logic (writes .streams and .nml)
    ingestor.InitializeCDEPS(config);

    AcesImportState state;
    // Ingest data. We expect internal name MACCITY_CO
    ingestor.IngestEmissionsInline(config, state, 20240101, 0, nx, ny, nz);

    ASSERT_TRUE(state.fields.count("MACCITY_CO") > 0);
    auto& dv = state.fields["MACCITY_CO"];
    dv.sync<Kokkos::HostSpace>();
    auto host_v = dv.view_host();

    bool non_zero = false;
    bool non_uniform = false;
    double first_val = host_v(0, 0, 0);

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            if (host_v(i, j, 0) != 0.0) non_zero = true;
            if (host_v(i, j, 0) != first_val) non_uniform = true;
        }
    }

    EXPECT_TRUE(non_zero);
    EXPECT_TRUE(non_uniform);

    // Clean up
    std::remove(test_nc);
    std::remove("aces_emissions.streams");
    std::remove("cdeps_in.nml");
}

}  // namespace test
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
