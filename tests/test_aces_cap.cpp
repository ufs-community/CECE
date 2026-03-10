#include <gtest/gtest.h>

#include <cstring>  // for memset
#include <fstream>
#include <iostream>

#include "ESMC.h"

// Declare the functions we want to test
extern "C" {
void aces_core_initialize(void** data_ptr, void* importState, void* exportState, void* clock,
                          int* rc);
void aces_core_finalize(void* data_ptr, int* rc);
}

TEST(ACESCapTest, Lifecycle) {
    std::cout << "Test: Creating config" << std::endl;
    std::ofstream config_file("aces_config.yaml");
    config_file << "species:\n  nox: []\nphysics_schemes: []\n";
    config_file.close();

    int rc = -1;
    void* data_ptr = nullptr;

    ESMC_State importState;
    std::memset(&importState, 0, sizeof(importState));
    importState.ptr = nullptr;

    ESMC_State exportState;
    std::memset(&exportState, 0, sizeof(exportState));
    exportState.ptr = nullptr;

    ESMC_Clock clock;
    std::memset(&clock, 0, sizeof(clock));
    clock.ptr = nullptr;

    std::cout << "Test: Calling aces_core_initialize" << std::endl;
    aces_core_initialize(&data_ptr, importState.ptr, exportState.ptr, clock.ptr, &rc);
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
