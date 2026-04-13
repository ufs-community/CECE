#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cstring>
#include <vector>

#include "cece/cece_utils.hpp"

namespace cece::test {

class CeceUtilsTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

TEST_F(CeceUtilsTest, WrapESMCFieldUpdatesRawData) {
    const int nx = 10;
    const int ny = 5;
    const int nz = 2;
    std::vector<double> raw_data(static_cast<size_t>(nx) * ny * nz, 0.0);

    UnmanagedHostView3D view;

    // On real ESMF, ESMC_FieldGetPtr would SegFault on a fake handle.
    // We test the Kokkos wrapping logic directly here to prove that
    // LayoutLeft and Unmanaged traits work as expected for ESMF data.
    view = UnmanagedHostView3D(raw_data.data(), nx, ny, nz);

    // Verify dimensions
    EXPECT_EQ(view.extent(0), nx);
    EXPECT_EQ(view.extent(1), ny);
    EXPECT_EQ(view.extent(2), nz);

    // Modify the Kokkos View
    view(2, 3, 1) = 42.0;

    // Verify the raw data is updated
    // Since it's LayoutLeft (Fortran order):
    // index = i + j*nx + k*nx*ny
    int expected_index = 2 + 3 * nx + 1 * nx * ny;
    EXPECT_DOUBLE_EQ(raw_data[expected_index], 42.0);

    // Also verify through the view
    EXPECT_DOUBLE_EQ(view(2, 3, 1), 42.0);
}

}  // namespace cece::test

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
