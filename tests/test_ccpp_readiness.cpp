#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <filesystem>

#include "cece/cece_logger.hpp"

// Forward declare standard C++ core C-linkage APIs
extern "C" {
void cece_set_config_file_path(const char* config_path, int path_len);
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
}

TEST(CCPPLinkTest, CompileIsolation) {
    // Assert that we can call core initialize and finalize APIs with absolutely
    // no symbols or linkages to any other driver, AMIO, AXIS, or DAGR libraries
    void* data_ptr = nullptr;
    int rc = -1;

    // Resolve the mock configuration against the source tree so the test works
    // from any build directory (out-of-source builds put the ctest cwd where no
    // cwd-relative guess can reach tests/).
    std::string config_file = std::string(CECE_SOURCE_DIR) + "/tests/cece_control_mock.yaml";
    ASSERT_TRUE(std::filesystem::exists(config_file)) << "missing " << config_file;
    cece_set_config_file_path(config_file.c_str(), static_cast<int>(config_file.length()));

    // Phase 1 Initialization
    cece_core_initialize_p1(&data_ptr, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(data_ptr, nullptr);

    // Core Finalization
    cece_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, 0);
}
