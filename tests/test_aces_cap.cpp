#include <gtest/gtest.h>

#include <cstring>  // for memset
#include <fstream>
#include <iostream>
#include <string>

// Declare the functions we want to test (decoupled architecture - no ESMF)
extern "C" {
void aces_core_initialize_p1(void** data_ptr_ptr, int* rc);
void aces_core_finalize(void* data_ptr, int* rc);
void aces_set_config_file_path(const char* config_path, int path_len);
}

static std::string g_config_file = "aces_config.yaml";

TEST(ACESCapTest, Lifecycle) {
    std::cout << "Test: Creating config from: " << g_config_file << std::endl;

    // Only create default config if no custom config specified
    if (g_config_file == "aces_config.yaml") {
        std::ofstream config_file("aces_config.yaml");
        config_file << "species:\n  nox: []\nphysics_schemes: []\n";
        config_file.close();
    }

    // Set the config file path for ACES
    aces_set_config_file_path(g_config_file.c_str(), static_cast<int>(g_config_file.length()));

    int rc = -1;
    void* data_ptr = nullptr;

    std::cout << "Test: Calling aces_core_initialize_p1" << std::endl;
    aces_core_initialize_p1(&data_ptr, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(data_ptr, nullptr);

    std::cout << "Test: Calling aces_core_finalize" << std::endl;
    aces_core_finalize(data_ptr, &rc);
    EXPECT_EQ(rc, 0);
    std::cout << "Test: Finished" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Process remaining arguments for config file
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg.find(".yaml") != std::string::npos || arg.find(".yml") != std::string::npos) {
            g_config_file = arg;
            std::cout << "Using config file: " << g_config_file << std::endl;
            break;
        }
    }

    return RUN_ALL_TESTS();
}
