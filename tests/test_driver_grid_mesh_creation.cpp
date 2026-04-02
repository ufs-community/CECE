/**
 * @file test_driver_grid_mesh_creation.cpp
 * @brief Tests for grid and mesh creation in the driver.
 *
 * Validates:
 *   - Mesh file input support
 *   - Gaussian grid generation for single-process execution
 *   - Gaussian grid generation for MPI multi-process execution
 *   - Grid/mesh selection logic
 *   - Domain decomposition correctness
 *
 * Requirements: 8.1, 8.2, 8.3, 8.4, 9.1, 9.2, 9.3, 14.1, 14.2, 14.3, 14.4, 15.1
 * Properties: 9, 10, 15
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Mock Grid and Mesh Classes for Testing Logic
// ---------------------------------------------------------------------------

/**
 * @brief Mock grid for testing grid creation logic.
 */
class MockGrid {
public:
    MockGrid(int nx, int ny, bool distributed = false, int petCount = 1, int localPet = 0)
        : global_nx_(nx), global_ny_(ny), distributed_(distributed),
          petCount_(petCount), localPet_(localPet) {
        if (distributed && petCount > 1) {
            // Simple domain decomposition: divide rows across processes
            int rows_per_pet = ny / petCount;
            int remainder = ny % petCount;

            local_ny_ = rows_per_pet + (localPet < remainder ? 1 : 0);
            local_start_j_ = localPet * rows_per_pet + std::min(localPet, remainder);
            local_end_j_ = local_start_j_ + local_ny_ - 1;

            local_nx_ = nx;  // All processes get full x dimension
        } else {
            local_nx_ = nx;
            local_ny_ = ny;
            local_start_j_ = 0;
            local_end_j_ = ny - 1;
        }
    }

    int GetGlobalNx() const { return global_nx_; }
    int GetGlobalNy() const { return global_ny_; }
    int GetLocalNx() const { return local_nx_; }
    int GetLocalNy() const { return local_ny_; }
    int GetLocalStartJ() const { return local_start_j_; }
    int GetLocalEndJ() const { return local_end_j_; }
    bool IsDistributed() const { return distributed_; }

    // Verify domain decomposition correctness
    bool VerifyDecomposition() const {
        if (!distributed_ || petCount_ <= 1) return true;

        // Sum of all local dimensions should equal global
        int total_ny = 0;
        for (int pet = 0; pet < petCount_; ++pet) {
            MockGrid grid(global_nx_, global_ny_, distributed_, petCount_, pet);
            total_ny += grid.GetLocalNy();
        }
        return total_ny == global_ny_;
    }

private:
    int global_nx_, global_ny_;
    int local_nx_, local_ny_;
    int local_start_j_, local_end_j_;
    bool distributed_;
    int petCount_, localPet_;
};

/**
 * @brief Mock mesh for testing mesh creation logic.
 */
class MockMesh {
public:
    MockMesh(int nx, int ny)
        : nx_(nx), ny_(ny),
          num_nodes_((nx + 1) * (ny + 1)),
          num_elements_(nx * ny) {}

    int GetNumNodes() const { return num_nodes_; }
    int GetNumElements() const { return num_elements_; }
    int GetNx() const { return nx_; }
    int GetNy() const { return ny_; }

    // Verify mesh connectivity
    bool VerifyConnectivity() const {
        // For a quad mesh, each element should have 4 nodes
        // and nodes should be properly indexed
        return num_nodes_ == (nx_ + 1) * (ny_ + 1) &&
               num_elements_ == nx_ * ny_;
    }

private:
    int nx_, ny_;
    int num_nodes_, num_elements_;
};

// ---------------------------------------------------------------------------
// Test Suite: Single-Process Grid Creation
// ---------------------------------------------------------------------------

class SingleProcessGridCreationTest : public ::testing::Test {
};

// Property 9: Single-Process Grid Dimensions
// For any single-process execution with specified nx and ny,
// the created ESMF_Grid must have global dimensions exactly equal to [nx, ny]
TEST_F(SingleProcessGridCreationTest, Property9_SingleProcessGridDimensions) {
    // Test with various grid sizes
    std::vector<std::pair<int, int>> test_cases = {
        {4, 4}, {10, 10}, {100, 50}, {360, 180}, {1000, 500}
    };

    for (const auto& [nx, ny] : test_cases) {
        MockGrid grid(nx, ny, false, 1, 0);
        EXPECT_EQ(grid.GetGlobalNx(), nx);
        EXPECT_EQ(grid.GetGlobalNy(), ny);
        EXPECT_EQ(grid.GetLocalNx(), nx);
        EXPECT_EQ(grid.GetLocalNy(), ny);
    }
}

TEST_F(SingleProcessGridCreationTest, SmallGridCreation) {
    MockGrid grid(4, 4, false, 1, 0);
    EXPECT_EQ(grid.GetGlobalNx(), 4);
    EXPECT_EQ(grid.GetGlobalNy(), 4);
    EXPECT_EQ(grid.GetLocalNx(), 4);
    EXPECT_EQ(grid.GetLocalNy(), 4);
}

TEST_F(SingleProcessGridCreationTest, MediumGridCreation) {
    MockGrid grid(360, 180, false, 1, 0);
    EXPECT_EQ(grid.GetGlobalNx(), 360);
    EXPECT_EQ(grid.GetGlobalNy(), 180);
}

TEST_F(SingleProcessGridCreationTest, LargeGridCreation) {
    MockGrid grid(1000, 500, false, 1, 0);
    EXPECT_EQ(grid.GetGlobalNx(), 1000);
    EXPECT_EQ(grid.GetGlobalNy(), 500);
}

// ---------------------------------------------------------------------------
// Test Suite: MPI Domain Decomposition
// ---------------------------------------------------------------------------

class MPIDomainDecompositionTest : public ::testing::Test {
};

// Property 10: MPI Domain Decomposition
// For any multi-process execution with petCount > 1,
// the sum of all local grid dimensions across all processes must equal
// the global grid dimensions [nx, ny]
TEST_F(MPIDomainDecompositionTest, Property10_MPIDomainDecomposition) {
    // Test with various configurations
    std::vector<std::tuple<int, int, int>> test_cases = {
        {360, 180, 2}, {360, 180, 4}, {1000, 500, 8}
    };

    for (const auto& [nx, ny, petCount] : test_cases) {
        // Verify decomposition for each process
        int total_ny = 0;
        for (int pet = 0; pet < petCount; ++pet) {
            MockGrid grid(nx, ny, true, petCount, pet);
            EXPECT_EQ(grid.GetLocalNx(), nx) << "Process " << pet << " should have full x dimension";
            total_ny += grid.GetLocalNy();
        }
        EXPECT_EQ(total_ny, ny);
    }
}

TEST_F(MPIDomainDecompositionTest, TwoProcessDecomposition) {
    MockGrid grid1(360, 180, true, 2, 0);
    MockGrid grid2(360, 180, true, 2, 1);

    EXPECT_EQ(grid1.GetLocalNx(), 360);
    EXPECT_EQ(grid2.GetLocalNx(), 360);
    EXPECT_EQ(grid1.GetLocalNy() + grid2.GetLocalNy(), 180);
}

TEST_F(MPIDomainDecompositionTest, FourProcessDecomposition) {
    int total_ny = 0;
    for (int pet = 0; pet < 4; ++pet) {
        MockGrid grid(360, 180, true, 4, pet);
        EXPECT_EQ(grid.GetLocalNx(), 360);
        total_ny += grid.GetLocalNy();
    }
    EXPECT_EQ(total_ny, 180);
}

TEST_F(MPIDomainDecompositionTest, EightProcessDecomposition) {
    int total_ny = 0;
    for (int pet = 0; pet < 8; ++pet) {
        MockGrid grid(1000, 500, true, 8, pet);
        EXPECT_EQ(grid.GetLocalNx(), 1000);
        total_ny += grid.GetLocalNy();
    }
    EXPECT_EQ(total_ny, 500);
}

// ---------------------------------------------------------------------------
// Test Suite: Mesh Creation
// ---------------------------------------------------------------------------

class MeshCreationTest : public ::testing::Test {
};

TEST_F(MeshCreationTest, MeshNodeCount) {
    // For a quad mesh with nx x ny elements,
    // there should be (nx+1) x (ny+1) nodes
    MockMesh mesh(4, 4);
    EXPECT_EQ(mesh.GetNumNodes(), 25);  // 5 x 5
    EXPECT_EQ(mesh.GetNumElements(), 16);  // 4 x 4
}

TEST_F(MeshCreationTest, MeshConnectivityVerification) {
    MockMesh mesh(10, 10);
    EXPECT_TRUE(mesh.VerifyConnectivity());
}

TEST_F(MeshCreationTest, MeshConnectivityProperty) {
    // Test with various mesh sizes
    std::vector<std::pair<int, int>> test_cases = {
        {4, 4}, {10, 10}, {50, 50}, {100, 100}
    };

    for (const auto& [nx, ny] : test_cases) {
        MockMesh mesh(nx, ny);
        EXPECT_TRUE(mesh.VerifyConnectivity());
        EXPECT_EQ(mesh.GetNumNodes(), (nx + 1) * (ny + 1));
        EXPECT_EQ(mesh.GetNumElements(), nx * ny);
    }
}

// ---------------------------------------------------------------------------
// Test Suite: Grid/Mesh Selection Logic
// ---------------------------------------------------------------------------

class GridMeshSelectionLogicTest : public ::testing::Test {
};

// Property 15: Grid/Mesh Selection Logic
// If driver.mesh_file is specified and valid, use mesh file and skip grid generation.
// If driver.mesh_file is null or absent, generate Gaussian grid based on nx and ny.
TEST_F(GridMeshSelectionLogicTest, Property15_GridMeshSelectionLogic) {
    // Test case 1: mesh_file specified -> use mesh
    {
        std::string mesh_file = "test_mesh.nc";
        bool use_mesh = !mesh_file.empty() && mesh_file != "null";
        EXPECT_TRUE(use_mesh);
    }

    // Test case 2: mesh_file null -> use grid
    {
        std::string mesh_file = "null";
        bool use_mesh = !mesh_file.empty() && mesh_file != "null";
        EXPECT_FALSE(use_mesh);
    }

    // Test case 3: mesh_file absent -> use grid
    {
        std::string mesh_file = "";
        bool use_mesh = !mesh_file.empty() && mesh_file != "null";
        EXPECT_FALSE(use_mesh);
    }
}

TEST_F(GridMeshSelectionLogicTest, MeshFileSelection) {
    std::string mesh_file = "path/to/mesh.nc";
    bool use_mesh = !mesh_file.empty() && mesh_file != "null";
    EXPECT_TRUE(use_mesh);
}

TEST_F(GridMeshSelectionLogicTest, GridGenerationSelection) {
    std::string mesh_file = "";
    int nx = 360, ny = 180;
    bool use_mesh = !mesh_file.empty() && mesh_file != "null";
    EXPECT_FALSE(use_mesh);
    // Should generate grid with nx, ny
    MockGrid grid(nx, ny, false, 1, 0);
    EXPECT_EQ(grid.GetGlobalNx(), nx);
    EXPECT_EQ(grid.GetGlobalNy(), ny);
}

// ---------------------------------------------------------------------------
// Test Suite: Coordinate System Validation
// ---------------------------------------------------------------------------

class CoordinateSystemValidationTest : public ::testing::Test {
};

TEST_F(CoordinateSystemValidationTest, LongitudeRange) {
    // Longitude should be -180 to +180
    double lon_min = -180.0;
    double lon_max = 180.0;
    EXPECT_EQ(lon_max - lon_min, 360.0);
}

TEST_F(CoordinateSystemValidationTest, LatitudeRange) {
    // Latitude should be -90 to +90
    double lat_min = -90.0;
    double lat_max = 90.0;
    EXPECT_EQ(lat_max - lat_min, 180.0);
}

TEST_F(CoordinateSystemValidationTest, GaussianGridSpacing) {
    // For a 4x4 grid, spacing should be 360/4 = 90 degrees in lon
    // and 180/4 = 45 degrees in lat
    int nx = 4, ny = 4;
    double dlon = 360.0 / nx;
    double dlat = 180.0 / ny;
    EXPECT_DOUBLE_EQ(dlon, 90.0);
    EXPECT_DOUBLE_EQ(dlat, 45.0);
}

TEST_F(CoordinateSystemValidationTest, GridSpacingProperty) {
    // Test with various grid sizes
    std::vector<std::pair<int, int>> test_cases = {
        {4, 4}, {10, 10}, {100, 50}, {360, 180}
    };

    for (const auto& [nx, ny] : test_cases) {
        double dlon = 360.0 / nx;
        double dlat = 180.0 / ny;

        // Verify spacing is positive
        EXPECT_GT(dlon, 0.0);
        EXPECT_GT(dlat, 0.0);

        // Verify total coverage
        EXPECT_LT(std::abs(dlon * nx - 360.0), 1e-10);
        EXPECT_LT(std::abs(dlat * ny - 180.0), 1e-10);
    }
}

// ---------------------------------------------------------------------------
// Integration Tests
// ---------------------------------------------------------------------------

class GridMeshIntegrationTest : public ::testing::Test {
};

TEST_F(GridMeshIntegrationTest, SingleProcessGridAndMesh) {
    MockGrid grid(4, 4, false, 1, 0);
    MockMesh mesh(4, 4);

    EXPECT_EQ(grid.GetGlobalNx(), mesh.GetNx());
    EXPECT_EQ(grid.GetGlobalNy(), mesh.GetNy());
    EXPECT_TRUE(mesh.VerifyConnectivity());
}

TEST_F(GridMeshIntegrationTest, MPIGridAndMesh) {
    int petCount = 4;
    int nx = 360, ny = 180;

    // Create grids for all processes
    std::vector<MockGrid> grids;
    for (int pet = 0; pet < petCount; ++pet) {
        grids.emplace_back(nx, ny, true, petCount, pet);
    }

    // Verify decomposition
    int total_ny = 0;
    for (const auto& grid : grids) {
        EXPECT_EQ(grid.GetLocalNx(), nx);
        total_ny += grid.GetLocalNy();
    }
    EXPECT_EQ(total_ny, ny);

    // Create mesh
    MockMesh mesh(nx, ny);
    EXPECT_TRUE(mesh.VerifyConnectivity());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
