#include <gtest/gtest.h>

#include <cstring>  // for memset
#include <fstream>
#include <iostream>

#include "ESMC.h"

// Declare the functions we want to test
extern "C" {
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                     ESMC_Clock* clock, int* rc);
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                   ESMC_Clock* clock, int* rc);
}

TEST(ACESCapTest, Lifecycle) {
    std::cout << "Test: Creating config" << std::endl;
    std::ofstream config_file("aces_config.yaml");
    config_file << "species:\n  nox: []\nphysics_schemes: []\n";
    config_file.close();

    int rc = -1;
    ESMC_GridComp comp;
    std::memset(&comp, 0, sizeof(comp));
    comp.ptr = nullptr;

    ESMC_State importState;
    std::memset(&importState, 0, sizeof(importState));
    importState.ptr = nullptr;

    ESMC_State exportState;
    std::memset(&exportState, 0, sizeof(exportState));
    exportState.ptr = nullptr;

    ESMC_Clock clock;
    std::memset(&clock, 0, sizeof(clock));
    clock.ptr = nullptr;

    std::cout << "Test: Calling ACES_Initialize" << std::endl;
    ACES_Initialize(comp, importState, exportState, &clock, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);

    std::cout << "Test: Calling ACES_Finalize" << std::endl;
    ACES_Finalize(comp, importState, exportState, &clock, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);
    std::cout << "Test: Finished" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
