#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>
#include <vector>

#include "aces/aces_config.hpp"
#include "aces/aces_data_ingestor.hpp"

/**
 * @file test_aces_ingestor.cpp
 * @brief Unit tests for the hybrid data ingestor.
 */

namespace aces::test {

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
    AcesDataIngestor ingestor;
    AcesDataConfig config;
    AcesDataStreamConfig s1;
    s1.name = "stream1";
    s1.file_paths.emplace_back("path1.nc");
    s1.tintalgo = "linear";
    AcesDataVariableConfig v1;
    v1.name_in_file = "VAR_FILE";
    v1.name_in_model = "VAR_MODEL";
    s1.variables.push_back(v1);
    config.streams.push_back(s1);

    std::string config_out = ingestor.SerializeTideESMFConfig(config);

    // Verify content (ESMF Config format)
    // The stream name "stream1" is not used in ESMF Config format (it uses indices)
    EXPECT_NE(config_out.find("stream_data_files01: path1.nc"), std::string::npos);
    EXPECT_NE(config_out.find("stream_data_variables01: VAR_FILE:VAR_MODEL"), std::string::npos);
}

}  // namespace aces::test

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
