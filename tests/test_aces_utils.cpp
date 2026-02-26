#include <gtest/gtest.h>
#include <Kokkos_Core.hpp>
#include "aces/aces_utils.hpp"
#include <vector>
#include <cstring>
#include <algorithm>

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

    // We manually populate the handle's internal pointer for the test.
    // This is safe because we are either using our mock (which we know)
    // or the real ESMF (where handles are pointers or simple structs).
    ESMC_Field mockField;
    std::memset(&mockField, 0, sizeof(mockField));
    void* ptr = raw_data.data();
    std::memcpy(&mockField, &ptr, std::min(sizeof(ESMC_Field), sizeof(void*)));

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
