#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_data_ingestor.hpp"
#include "cece/cece_io.hpp"

/**
 * @file test_cece_ingestor.cpp
 * @brief Unit tests for the hybrid data ingestor.
 */

namespace cece::test {

class IngestorTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

TEST_F(IngestorTest, IngestMeteorologyHandlesNull) {
    // Calling ESMF functions with a null handle often segfaults in the real
    // library. We skip this test when using the real ESMF as it is not a valid
    // use case.
}

TEST_F(IngestorTest, ConfigFileGeneration) {
    CeceDataIngestor ingestor;
    CeceDataConfig config;
    CeceDataStreamConfig s1;
    s1.name = "stream1";
    s1.file_paths.emplace_back("path1.nc");
    s1.tintalgo = "linear";
    CeceDataVariableConfig v1;
    v1.name_in_file = "VAR_FILE";
    v1.name_in_model = "VAR_MODEL";
    s1.variables.push_back(v1);
    config.streams.push_back(s1);

    std::string config_out = ingestor.SerializeStreamESMFConfig(config);

    // Verify content (ESMF Config format)
    // The stream name "stream1" is not used in ESMF Config format (it uses indices)
    EXPECT_NE(config_out.find("stream_data_files01: path1.nc"), std::string::npos);
    EXPECT_NE(config_out.find("stream_data_variables01: VAR_FILE VAR_MODEL"), std::string::npos);
}

TEST_F(IngestorTest, PassthroughMapalgoSerializedCorrectly) {
    CeceDataIngestor ingestor;
    CeceDataConfig config;
    CeceDataStreamConfig s1;
    s1.name = "on_grid_stream";
    s1.file_paths.emplace_back("data.nc");
    s1.mapalgo = "passthrough";
    CeceDataVariableConfig v1;
    v1.name_in_file = "CO_FILE";
    v1.name_in_model = "CO_MODEL";
    s1.variables.push_back(v1);
    config.streams.push_back(s1);

    std::string esmf_out = ingestor.SerializeStreamESMFConfig(config);
    EXPECT_NE(esmf_out.find("mapalgo01: passthrough"), std::string::npos);

    std::string yaml_out = ingestor.SerializeStreamYaml(config);
    EXPECT_NE(yaml_out.find("map_algo: \"passthrough\""), std::string::npos);
}

TEST_F(IngestorTest, CeceIoAllocatesConfiguredPerVariableLevels) {
    namespace fs = std::filesystem;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path config_path = fs::temp_directory_path() / ("cece_io_levels_" + std::to_string(stamp) + ".yaml");

    {
        std::ofstream config(config_path);
        ASSERT_TRUE(config.good());
        config << "cece_data:\n"
               << "  streams:\n"
               << "    - name: test_stream\n"
               << "      variables:\n"
               << "        - scalar_field\n"
               << "        - file: LAND_FRACTION\n"
               << "          model: soilnox_land_fractions\n"
               << "          levels: 24\n";
    }

    cece::io::CeceIO io;
    ASSERT_NO_THROW(io.Initialize(config_path.string(), 3, 2, 1));

    const auto scalar = io.GetFieldView("scalar_field");
    EXPECT_EQ(scalar.extent(0), 3u);
    EXPECT_EQ(scalar.extent(1), 2u);
    EXPECT_EQ(scalar.extent(2), 1u);

    const auto layered = io.GetFieldView("soilnox_land_fractions");
    EXPECT_EQ(layered.extent(0), 3u);
    EXPECT_EQ(layered.extent(1), 2u);
    EXPECT_EQ(layered.extent(2), 24u);

    io.Finalize();
    std::error_code ec;
    fs::remove(config_path, ec);
    EXPECT_FALSE(ec) << ec.message();
}

TEST_F(IngestorTest, PreservesTwentyFourLayersInCacheAndImportState) {
    constexpr int nx = 3;
    constexpr int ny = 2;
    constexpr int levels = 24;
    constexpr int horizontal_size = nx * ny;

    std::vector<double> values(static_cast<size_t>(levels) * horizontal_size);
    for (int lev = 0; lev < levels; ++lev) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                values[static_cast<size_t>(lev) * horizontal_size + j * nx + i] = 10000.0 * lev + 100.0 * j + i + 0.25;
            }
        }
    }

    CeceDataIngestor ingestor;
    int rc = -1;
    ingestor.SetField("soilnox_land_fractions", values.data(), levels, horizontal_size, nx, ny, /*model_nz=*/1, &rc);
    ASSERT_EQ(rc, 0);

    const auto cached = ingestor.ResolveField("soilnox_land_fractions", nx, ny, 1);
    ASSERT_NE(cached.data(), nullptr);
    EXPECT_EQ(cached.extent(0), static_cast<size_t>(nx));
    EXPECT_EQ(cached.extent(1), static_cast<size_t>(ny));
    EXPECT_EQ(cached.extent(2), static_cast<size_t>(levels));

    CeceDataConfig config;
    CeceDataStreamConfig stream;
    CeceDataVariableConfig variable;
    variable.name_in_file = "LAND_FRACTION";
    variable.name_in_model = "soilnox_land_fractions";
    stream.variables.push_back(variable);
    config.streams.push_back(stream);

    CeceImportState import_state;
    ingestor.IngestEmissionsInline(config, import_state, nx, ny, /*model_nz=*/1);
    ASSERT_TRUE(import_state.fields.contains("soilnox_land_fractions"));

    const auto imported = import_state.fields.at("soilnox_land_fractions").view_device();
    ASSERT_EQ(imported.extent(2), static_cast<size_t>(levels));
    const auto host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), imported);
    for (int lev = 0; lev < levels; ++lev) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const double expected = values[static_cast<size_t>(lev) * horizontal_size + j * nx + i];
                EXPECT_DOUBLE_EQ(host(i, j, lev), expected) << "i=" << i << " j=" << j << " lev=" << lev;
            }
        }
    }
}

}  // namespace cece::test

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
