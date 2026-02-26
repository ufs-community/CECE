#include <gtest/gtest.h>
#include "aces/aces_compute.hpp"
#include <Kokkos_Core.hpp>
#include <vector>

namespace aces {
namespace testing {

class ComputeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }

    static void TearDownTestSuite() {
        // We don't finalize Kokkos here because other tests might need it
        // and Kokkos cannot be re-initialized.
        // Finalization should happen at the very end of the test process if possible.
    }

    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ComputeTest, ScalingAndMasking) {
    int ni = 4, nj = 4, nk = 4;

    AcesImportState imp;
    imp.base_emissions = DualView3D("base", ni, nj, nk);
    imp.scaling_factor = DualView3D("scale", ni, nj, nk);
    imp.mask = DualView3D("mask", ni, nj, nk);

    AcesExportState exp;
    exp.scaled_emissions = DualView3D("result", ni, nj, nk);

    // Initialize data on host
    auto base_h = imp.base_emissions.view<Kokkos::HostSpace>();
    auto scale_h = imp.scaling_factor.view<Kokkos::HostSpace>();
    auto mask_h = imp.mask.view<Kokkos::HostSpace>();

    for (int k = 0; k < nk; ++k) {
        for (int j = 0; j < nj; ++j) {
            for (int i = 0; i < ni; ++i) {
                base_h(i, j, k) = 10.0;
                scale_h(i, j, k) = 1.5;
                // Apply a simple mask pattern: 1.0 for even i, 0.0 for odd i
                mask_h(i, j, k) = (i % 2 == 0) ? 1.0 : 0.0;
            }
        }
    }

    imp.base_emissions.modify<Kokkos::HostSpace>();
    imp.scaling_factor.modify<Kokkos::HostSpace>();
    imp.mask.modify<Kokkos::HostSpace>();

    // Run compute
    ComputeEmissions(imp, exp);

    // Sync results back to host
    exp.scaled_emissions.sync<Kokkos::HostSpace>();
    auto result_h = exp.scaled_emissions.view<Kokkos::HostSpace>();

    // Verify results
    for (int k = 0; k < nk; ++k) {
        for (int j = 0; j < nj; ++j) {
            for (int i = 0; i < ni; ++i) {
                double expected = (i % 2 == 0) ? (10.0 * 1.5 * 1.0) : 0.0;
                EXPECT_DOUBLE_EQ(result_h(i, j, k), expected);
            }
        }
    }
}

} // namespace testing
} // namespace aces

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (Kokkos::is_initialized()) {
        Kokkos::finalize();
    }
    return result;
}
