#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>

#include "aces/aces_config.hpp"
#include "aces/aces_data_ingestor.hpp"

// Mock CDEPS functions for testing
static bool init_called = false;
static bool finalize_called = false;
static std::string last_read_stream = "";

extern "C" {
void cdeps_inline_init(const char* config_file) {
    init_called = true;
}
void cdeps_inline_read(double* buffer, const char* stream_name) {
    last_read_stream = stream_name;
    // Mock data
    buffer[0] = 1.23;
}
void cdeps_inline_finalize() {
    finalize_called = true;
}

// Mock ESMF functions if not available or to avoid complex setup
int ESMC_StateGetField(ESMC_State state, const char* name, ESMC_Field* field) {
    if (std::string(name) == "temperature" || std::string(name) == "wind_speed_10m") {
        field->ptr = (void*)0xDEADBEEF;  // Dummy non-null
        return ESMF_SUCCESS;
    }
    return ESMF_FAILURE;
}
void* ESMC_FieldGetPtr(ESMC_Field field, int localDe, int* rc) {
    if (rc) *rc = ESMF_SUCCESS;
    static double dummy_data[1000];
    return dummy_data;
}
}

namespace aces {
namespace test {

class IngestorTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
        init_called = false;
        finalize_called = false;
        last_read_stream = "";
    }
};

TEST_F(IngestorTest, IngestMeteorology) {
    AcesDataIngestor ingestor;
    ESMC_State importState;
    importState.ptr = (void*)0x1;
    AcesImportState aces_state;
    int nx = 10, ny = 10, nz = 10;

    ingestor.IngestMeteorology(importState, aces_state, nx, ny, nz);

    EXPECT_NE(aces_state.temperature.view_host().data(), nullptr);
    EXPECT_NE(aces_state.wind_speed_10m.view_host().data(), nullptr);
}

TEST_F(IngestorTest, IngestEmissionsInline) {
    AcesDataIngestor ingestor;
    AcesCdepsConfig config;
    CdepsStreamConfig stream;
    stream.name = "base_anthropogenic_nox";
    stream.file_path = "mock_emissions.nc";
    stream.interpolation_method = "linear";
    config.streams.push_back(stream);

    AcesImportState aces_state;
    int nx = 10, ny = 10, nz = 10;

    ingestor.IngestEmissionsInline(config, aces_state, nx, ny, nz);

    EXPECT_TRUE(init_called);
    EXPECT_TRUE(finalize_called);
    EXPECT_EQ(last_read_stream, "base_anthropogenic_nox");
    EXPECT_NE(aces_state.base_anthropogenic_nox.view_host().data(), nullptr);
    EXPECT_DOUBLE_EQ(aces_state.base_anthropogenic_nox.view_host()(0, 0, 0), 1.23);

    // Verify files were generated
    std::ifstream sfile("aces_emissions.streams");
    EXPECT_TRUE(sfile.good());
    // Verify it contains ESMF config style entries
    std::string line;
    bool found_id = false;
    while (std::getline(sfile, line)) {
        if (line.find("file_id: \"stream\"") != std::string::npos) found_id = true;
    }
    EXPECT_TRUE(found_id);

    std::ifstream nfile("cdeps_in.nml");
    EXPECT_TRUE(nfile.good());
}

}  // namespace test
}  // namespace aces
