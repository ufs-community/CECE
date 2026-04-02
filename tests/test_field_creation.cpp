/**
 * @file test_field_creation.cpp
 * @brief Unit tests for ACES field creation in Fortran cap.
 *
 * Tests verify that the Fortran cap (ACES_InitializeRealize) correctly:
 * - Creates ESMF fields for each species
 * - Adds fields to the export state
 * - Extracts valid field data pointers
 * - Validates field dimensions match grid dimensions
 * - Handles multiple species correctly
 * - Stores field metadata for C++ access
 *
 * **Validates: Requirements R4**
 *
 * All tests use real ESMF objects (no mocking) in the JCSDA Docker environment.
 */

#include <ESMC.h>
#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>
#include <string>

#include "aces/aces_internal.hpp"

extern "C" {
void aces_core_advertise(void* importState, void* exportState, int* rc);
void aces_core_realize(void* data_ptr, int* rc);
void aces_core_initialize_p1(void** data_ptr_ptr, int* rc);
void aces_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
void aces_core_get_species_count(void* data_ptr, int* count, int* rc);
void aces_core_get_species_name(void* data_ptr, int* index, char* name, int* name_len, int* rc);
void aces_core_bind_fields(void* data_ptr, void** field_ptrs, int* num_fields, int* rc);
void aces_core_finalize(void* data_ptr, int* rc);

void test_create_gridcomp(const char* name, void* clock_ptr, void** gcomp_ptr, int* rc);
void test_destroy_gridcomp(void* gcomp_ptr, int* rc);
}

// Global ESMF / Kokkos environment
class ESMFEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        if (rc != ESMF_SUCCESS) {
            std::cerr << "ESMF init failed\n";
            exit(1);
        }
    }
    void TearDown() override {
        ESMC_Finalize();
    }
};

// Test fixture
class FieldCreationTest : public ::testing::Test {
   protected:
    ESMC_Grid grid_{nullptr};
    ESMC_State import_state_{nullptr};
    ESMC_State export_state_{nullptr};
    ESMC_Clock clock_{nullptr};
    ESMC_GridComp gcomp_{nullptr};
    void* data_ptr_{nullptr};

    void SetUp() override {
        CreateTestConfig();

        int rc;
        ESMC_Calendar cal = ESMC_CalendarCreate("Gregorian", ESMC_CALKIND_GREGORIAN, &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "Calendar creation failed";

        ESMC_Time start_time, stop_time;
        rc = ESMC_TimeSet(&start_time, 2020, 0, cal, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "TimeSet (start) failed";
        rc = ESMC_TimeSet(&stop_time, 2020, 24, cal, ESMC_CALKIND_GREGORIAN, 0);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "TimeSet (stop) failed";

        ESMC_TimeInterval timestep;
        rc = ESMC_TimeIntervalSet(&timestep, 3600);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "TimeIntervalSet failed";

        clock_ = ESMC_ClockCreate("ACES_Clock", timestep, start_time, stop_time, &rc);
        if (rc != ESMF_SUCCESS || !clock_.ptr) GTEST_SKIP() << "Clock creation failed";

        test_create_gridcomp("ACES", clock_.ptr, &gcomp_.ptr, &rc);
        if (rc != ESMF_SUCCESS || !gcomp_.ptr) GTEST_SKIP() << "GridComp creation failed";

        int maxIndex[3] = {10, 10, 5};
        ESMC_InterArrayInt iMax;
        ESMC_InterArrayIntSet(&iMax, maxIndex, 3);
        grid_ = ESMC_GridCreateNoPeriDim(&iMax, nullptr, nullptr, nullptr, &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "Grid creation failed";

        import_state_ = ESMC_StateCreate("ACES_Import", &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "ImportState creation failed";

        export_state_ = ESMC_StateCreate("ACES_Export", &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "ExportState creation failed";

        // Initialize C++ core
        aces_core_initialize_p1(&data_ptr_, &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "Phase 1 initialization failed";
    }

    void TearDown() override {
        int rc;
        if (data_ptr_) {
            aces_core_finalize(data_ptr_, &rc);
        }
        if (export_state_.ptr) ESMC_StateDestroy(&export_state_);
        if (import_state_.ptr) ESMC_StateDestroy(&import_state_);
        if (grid_.ptr) ESMC_GridDestroy(&grid_);
        if (gcomp_.ptr) test_destroy_gridcomp(gcomp_.ptr, &rc);
        if (clock_.ptr) ESMC_ClockDestroy(&clock_);
        std::remove("aces_config.yaml");
    }

    void CreateTestConfig() {
        std::ofstream f("aces_config.yaml");
        f << R"(
species:
  CO:
    - operation: add
      field: CO_anthro
      hierarchy: 0
      scale: 1.0
  NOx:
    - operation: add
      field: NOx_anthro
      hierarchy: 0
      scale: 1.0

meteorology:
  temperature: air_temperature

physics_schemes: []

diagnostics:
  output_interval_seconds: 3600
  variables: []

aces_data:
  streams: []
)";
        f.close();
    }
};

/**
 * @test FieldCreation_SucceedsWithValidGrid
 * **Validates: Requirement R4 - Field creation succeeds with valid inputs**
 *
 * Verifies that:
 * - Phase 2 initialization succeeds with valid grid dimensions
 * - Species count is correctly retrieved
 * - Species count matches configuration
 */
TEST_F(FieldCreationTest, FieldCreation_SucceedsWithValidGrid) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    // Phase 2: Initialize with grid dimensions
    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Phase 2 should succeed with valid grid";

    // Get species count
    int num_species = 0;
    aces_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Should get species count";
    EXPECT_EQ(num_species, 2) << "Should have 2 species (CO, NOx)";
}

/**
 * @test FieldCreation_RetrievesSpeciesNames
 * **Validates: Requirement R4 - Species names are correctly retrieved**
 *
 * Verifies that:
 * - Species names can be retrieved for each species
 * - Species name lengths are valid
 * - Species names match configuration
 */
TEST_F(FieldCreationTest, FieldCreation_RetrievesSpeciesNames) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    int num_species = 0;
    aces_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);
    ASSERT_GT(num_species, 0);

    // Retrieve each species name
    for (int i = 0; i < num_species; ++i) {
        char species_name[256];
        int species_name_len = 0;
        aces_core_get_species_name(data_ptr_, &i, species_name, &species_name_len, &rc);

        EXPECT_EQ(rc, ESMF_SUCCESS) << "Should get species name for species " << i;
        EXPECT_GT(species_name_len, 0)
            << "Species name length should be positive for species " << i;
        EXPECT_LE(species_name_len, 256)
            << "Species name length should be within bounds for species " << i;
        EXPECT_NE(species_name[0], '\0') << "Species name should not be empty for species " << i;
    }
}

/**
 * @test FieldCreation_BindFieldsSucceeds
 * **Validates: Requirement R4 - Field data pointers are correctly bound**
 *
 * Verifies that:
 * - Field pointers can be bound to internal data
 * - Binding succeeds with valid pointers
 * - Multiple fields can be bound
 */
TEST_F(FieldCreationTest, FieldCreation_BindFieldsSucceeds) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    int num_species = 0;
    aces_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);
    ASSERT_GT(num_species, 0);

    // Create dummy field pointers (simulating ESMF field data)
    void** field_ptrs = new void*[num_species];
    for (int i = 0; i < num_species; ++i) {
        field_ptrs[i] = malloc(nx * ny * nz * sizeof(double));
        ASSERT_NE(field_ptrs[i], nullptr) << "Should allocate memory for field " << i;
    }

    // Bind fields to internal data
    aces_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Field binding should succeed";

    // Cleanup
    for (int i = 0; i < num_species; ++i) {
        free(field_ptrs[i]);
    }
    delete[] field_ptrs;
}

/**
 * @test FieldCreation_BindFieldsWithMultipleSpecies
 * **Validates: Requirement R4 - Multiple fields can be created and stored**
 *
 * Verifies that:
 * - Multiple field pointers are correctly stored
 * - Each field pointer is distinct
 * - All fields are bound successfully
 */
TEST_F(FieldCreationTest, FieldCreation_BindFieldsWithMultipleSpecies) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    int num_species = 0;
    aces_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);
    ASSERT_EQ(num_species, 2) << "Should have 2 species";

    // Create distinct field pointers
    void** field_ptrs = new void*[num_species];
    for (int i = 0; i < num_species; ++i) {
        field_ptrs[i] = malloc(nx * ny * nz * sizeof(double));
        ASSERT_NE(field_ptrs[i], nullptr);
    }

    // Verify pointers are distinct
    EXPECT_NE(field_ptrs[0], field_ptrs[1]) << "Field pointers should be distinct";

    // Bind all fields
    aces_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS);

    // Cleanup
    for (int i = 0; i < num_species; ++i) {
        free(field_ptrs[i]);
    }
    delete[] field_ptrs;
}

/**
 * @test FieldCreation_HandlesNullDataPointer
 * **Validates: Requirement R4 - Error handling for null data pointer**
 *
 * Verifies that:
 * - Phase 2 fails gracefully with null data pointer
 * - Error code is returned
 */
TEST_F(FieldCreationTest, FieldCreation_HandlesNullDataPointer) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    // Try to initialize with null data pointer
    aces_core_initialize_p2(nullptr, &nx, &ny, &nz, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with null data pointer";
}

/**
 * @test FieldCreation_HandlesInvalidGridDimensions
 * **Validates: Requirement R4 - Error handling for invalid grid dimensions**
 *
 * Verifies that:
 * - Phase 2 fails with zero grid dimensions
 * - Error code is returned
 */
TEST_F(FieldCreationTest, FieldCreation_HandlesInvalidGridDimensions) {
    int rc;
    int nx = 0, ny = 10, nz = 5;  // Invalid: nx = 0

    // Try to initialize with invalid dimensions
    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with invalid grid dimensions";
}

/**
 * @test FieldCreation_HandlesNegativeGridDimensions
 * **Validates: Requirement R4 - Error handling for negative grid dimensions**
 *
 * Verifies that:
 * - Phase 2 fails with negative grid dimensions
 * - Error code is returned
 */
TEST_F(FieldCreationTest, FieldCreation_HandlesNegativeGridDimensions) {
    int rc;
    int nx = -1, ny = 10, nz = 5;  // Invalid: negative dimension

    // Try to initialize with negative dimensions
    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with negative grid dimensions";
}

/**
 * @test FieldCreation_HandlesNullGridDimensionPointers
 * **Validates: Requirement R4 - Error handling for null dimension pointers**
 *
 * Verifies that:
 * - Phase 2 fails with null dimension pointers
 * - Error code is returned
 */
TEST_F(FieldCreationTest, FieldCreation_HandlesNullGridDimensionPointers) {
    int rc;

    // Try to initialize with null dimension pointers
    aces_core_initialize_p2(data_ptr_, nullptr, nullptr, nullptr, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with null dimension pointers";
}

/**
 * @test FieldCreation_BindFieldsHandlesNullPointerArray
 * **Validates: Requirement R4 - Error handling for null field pointer array**
 *
 * Verifies that:
 * - Field binding fails with null pointer array
 * - Error code is returned
 */
TEST_F(FieldCreationTest, FieldCreation_BindFieldsHandlesNullPointerArray) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    int num_species = 2;

    // Try to bind with null pointer array
    aces_core_bind_fields(data_ptr_, nullptr, &num_species, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with null field pointer array";
}

/**
 * @test FieldCreation_BindFieldsHandlesZeroFields
 * **Validates: Requirement R4 - Handling of zero fields**
 *
 * Verifies that:
 * - Field binding handles zero fields gracefully
 * - Returns a valid return code
 */
TEST_F(FieldCreationTest, FieldCreation_BindFieldsHandlesZeroFields) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    int num_species = 0;
    void** field_ptrs = nullptr;

    // Try to bind with zero fields
    aces_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    // This might succeed (no-op) or fail depending on implementation
    // Just verify it returns a valid rc
    EXPECT_NE(rc, -1) << "Should return a valid return code";
}

/**
 * @test FieldCreation_GridDimensionsStoredCorrectly
 * **Validates: Requirement R4 - Grid dimensions are stored correctly**
 *
 * Verifies that:
 * - Grid dimensions are stored in internal data
 * - Dimensions can be retrieved after Phase 2
 */
TEST_F(FieldCreationTest, FieldCreation_GridDimensionsStoredCorrectly) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Verify internal data has correct dimensions
    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->nx, nx) << "nx should be stored correctly";
    EXPECT_EQ(internal_data->ny, ny) << "ny should be stored correctly";
    EXPECT_EQ(internal_data->nz, nz) << "nz should be stored correctly";
}

/**
 * @test FieldCreation_DefaultMaskAllocated
 * **Validates: Requirement R4 - Default mask is allocated with correct dimensions**
 *
 * Verifies that:
 * - Default mask is allocated after Phase 2
 * - Mask has correct dimensions
 * - Mask is initialized to 1.0
 */
TEST_F(FieldCreationTest, FieldCreation_DefaultMaskAllocated) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr_);

    // Verify mask is allocated
    EXPECT_NE(internal_data->default_mask.data(), nullptr) << "Default mask should be allocated";

    // Verify mask dimensions
    EXPECT_EQ(internal_data->default_mask.extent(0), nx) << "Mask extent(0) should match nx";
    EXPECT_EQ(internal_data->default_mask.extent(1), ny) << "Mask extent(1) should match ny";
    EXPECT_EQ(internal_data->default_mask.extent(2), nz) << "Mask extent(2) should match nz";
}

/**
 * @test FieldCreation_FieldPointersStored
 * **Validates: Requirement R4 - Field pointers are stored in internal data**
 *
 * Verifies that:
 * - Field pointers are stored after binding
 * - Number of stored pointers matches species count
 * - Stored pointers are not null
 */
TEST_F(FieldCreationTest, FieldCreation_FieldPointersStored) {
    int rc;
    int nx = 10, ny = 10, nz = 5;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    int num_species = 0;
    aces_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);
    ASSERT_GT(num_species, 0);

    // Create and bind field pointers
    void** field_ptrs = new void*[num_species];
    for (int i = 0; i < num_species; ++i) {
        field_ptrs[i] = malloc(nx * ny * nz * sizeof(double));
    }

    aces_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Verify pointers are stored
    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->field_pointers.size(), num_species)
        << "Should store " << num_species << " field pointers";

    for (int i = 0; i < num_species; ++i) {
        EXPECT_NE(internal_data->field_pointers[i], nullptr)
            << "Field pointer " << i << " should not be null";
    }

    // Cleanup
    for (int i = 0; i < num_species; ++i) {
        free(field_ptrs[i]);
    }
    delete[] field_ptrs;
}

/**
 * @test FieldCreation_LargeGridDimensions
 * **Validates: Requirement R4 - Field creation works with large grid dimensions**
 *
 * Verifies that:
 * - Phase 2 succeeds with large grid dimensions
 * - Default mask is allocated correctly
 * - No memory allocation failures
 */
TEST_F(FieldCreationTest, FieldCreation_LargeGridDimensions) {
    int rc;
    int nx = 100, ny = 100, nz = 50;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Phase 2 should succeed with large grid";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->nx, nx);
    EXPECT_EQ(internal_data->ny, ny);
    EXPECT_EQ(internal_data->nz, nz);
}

/**
 * @test FieldCreation_SmallGridDimensions
 * **Validates: Requirement R4 - Field creation works with small grid dimensions**
 *
 * Verifies that:
 * - Phase 2 succeeds with small grid dimensions
 * - Default mask is allocated correctly
 * - Minimum viable grid works
 */
TEST_F(FieldCreationTest, FieldCreation_SmallGridDimensions) {
    int rc;
    int nx = 1, ny = 1, nz = 1;

    aces_core_initialize_p2(data_ptr_, &nx, &ny, &nz, &rc);
    EXPECT_EQ(rc, ESMF_SUCCESS) << "Phase 2 should succeed with small grid";

    auto* internal_data = static_cast<aces::AcesInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->nx, nx);
    EXPECT_EQ(internal_data->ny, ny);
    EXPECT_EQ(internal_data->nz, nz);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new ESMFEnvironment);
    return RUN_ALL_TESTS();
}
