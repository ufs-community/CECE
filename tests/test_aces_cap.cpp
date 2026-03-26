#include <gtest/gtest.h>

#include <cstring>  // for memset
#include <fstream>
#include <iostream>

// Declare the functions we want to test (decoupled architecture - no ESMF)
extern "C" {
void aces_core_initialize_p1(void** data_ptr_ptr, int* rc);
void aces_core_finalize(void* data_ptr, int* rc);
}

TEST(ACESCapTest, Lifecycle) {
    std::cout << "Test: Creating config" << std::endl;
    std::ofstream config_file("aces_config.yaml");
    config_file << "species:\n  nox: []\nphysics_schemes: []\n";
    config_file.close();

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
    return RUN_ALL_TESTS();
}
