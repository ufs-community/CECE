#include <gtest/gtest.h>
#include <Kokkos_Core.hpp>
#include "aces/aces_utils.hpp"
#include <vector>

namespace aces {
namespace test {

class AcesUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

TEST_F(AcesUtilsTest, WrapESMCFieldUpdatesRawData) {
    const int nx = 10;
    const int ny = 5;
    const int nz = 2;
    std::vector<double> raw_data(nx * ny * nz, 0.0);

    // In our mock, ESMC_Field is just a void* to the data
    ESMC_Field mockField = static_cast<ESMC_Field>(raw_data.data());

    // Wrap the field
    UnmanagedHostView3D view = WrapESMCField(mockField, nx, ny, nz);

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

} // namespace test
} // namespace aces
