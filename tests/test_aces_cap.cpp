#include <gtest/gtest.h>
#include "ESMC.h"
#include <iostream>

// Declare the functions we want to test
extern "C" {
void ACES_SetServices(ESMC_GridComp comp, int* rc);
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock clock, ESMC_VM vm, int* rc);
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock clock, ESMC_VM vm, int* rc);
}

TEST(ACES_Cap_Test, SetServices) {
    int rc = -1;
    ACES_SetServices(nullptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);
}

TEST(ACES_Cap_Test, Lifecycle) {
    int rc = -1;

    // Initialize
    // Note: This initializes Kokkos
    ACES_Initialize(nullptr, nullptr, nullptr, nullptr, nullptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);

    // Finalize
    // Note: This finalizes Kokkos. After this, Kokkos cannot be re-initialized in this process.
    ACES_Finalize(nullptr, nullptr, nullptr, nullptr, nullptr, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);
}
