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

    // CMake supplies the fixture path; out-of-source builds and arbitrary
    // working directories must exercise the same core-link isolation test.
    const std::string config_file = CECE_TEST_CONFIG;
    ASSERT_TRUE(std::filesystem::is_regular_file(config_file));
    cece_set_config_file_path(config_file.c_str(), static_cast<int>(config_file.length()));

    // Phase 1 Initialization
    cece_core_initialize_p1(&data_ptr, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(data_ptr, nullptr);

    // Core Finalization
    cece_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, 0);
}
