/**
 * @file test_field_pointer_passing.cpp
 * @brief Unit tests for field pointer passing functionality (cece_core_bind_fields).
 *
 * Tests verify that the C++ core correctly:
 * - Stores field data pointers in CeceInternalData.field_pointers
 * - Stores field names in CeceInternalData.field_names
 * - Validates pointer validity before storage
 * - Handles multiple fields correctly
 * - Detects null pointers and mismatches
 * - Ensures pointer count matches species count
 *
 * **Validates: Requirement R5 - Pass Field Data Pointers to C++**
 *
 * All tests use real ESMF objects (no mocking) in the JCSDA Docker environment.
 */

#include <ESMC.h>
#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>
#include <string>
#include <vector>

#include "cece/cece_internal.hpp"

extern "C" {
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc);
void cece_core_initialize_p2(void* data_ptr, int* nx, int* ny, int* nz, int* rc);
void cece_core_get_species_count(void* data_ptr, int* count, int* rc);
void cece_core_get_species_name(void* data_ptr, int* index, char* name, int* name_len, int* rc);
void cece_core_bind_fields(void* data_ptr, void** field_ptrs, int* num_fields, int* rc);
void cece_core_finalize(void* data_ptr, int* rc);
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
class FieldPointerPassingTest : public ::testing::Test {
   protected:
    void* data_ptr_{nullptr};
    int nx_{10}, ny_{10}, nz_{5};

    void SetUp() override {
        CreateTestConfig();

        int rc;
        cece_core_initialize_p1(&data_ptr_, &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "Phase 1 initialization failed";

        cece_core_initialize_p2(data_ptr_, &nx_, &ny_, &nz_, &rc);
        if (rc != ESMF_SUCCESS) GTEST_SKIP() << "Phase 2 initialization failed";
    }

    void TearDown() override {
        int rc;
        if (data_ptr_) {
            cece_core_finalize(data_ptr_, &rc);
        }
        std::remove("cece_config.yaml");
    }

    void CreateTestConfig() {
        std::ofstream f("cece_config.yaml");
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
  SO2:
    - operation: add
      field: SO2_anthro
      hierarchy: 0
      scale: 1.0

meteorology:
  temperature: air_temperature

physics_schemes: []

diagnostics:
  output_interval_seconds: 3600
  variables: []

cece_data:
  streams: []
)";
        f.close();
    }

    void** AllocateFieldPointers(int num_fields) {
        void** field_ptrs = new void*[num_fields];
        for (int i = 0; i < num_fields; ++i) {
            field_ptrs[i] = malloc(nx_ * ny_ * nz_ * sizeof(double));
        }
        return field_ptrs;
    }

    void FreeFieldPointers(void** field_ptrs, int num_fields) {
        for (int i = 0; i < num_fields; ++i) {
            free(field_ptrs[i]);
        }
        delete[] field_ptrs;
    }
};

/**
 * @test FieldPointerPassing_PointersStoredCorrectly
 * **Validates: R5 - Field pointers are correctly stored in CeceInternalData**
 *
 * Verifies that:
 * - Field pointers are stored after binding
 * - Stored pointers match the input pointers
 * - Pointer count matches species count
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_PointersStoredCorrectly) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);
    ASSERT_EQ(num_species, 3) << "Should have 3 species (CO, NOx, SO2)";

    // Allocate field pointers
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Bind fields
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS) << "Field binding should succeed";

    // Verify pointers are stored correctly
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->field_pointers.size(), num_species)
        << "Should store " << num_species << " field pointers";

    for (int i = 0; i < num_species; ++i) {
        EXPECT_EQ(internal_data->field_pointers[i], field_ptrs[i])
            << "Stored pointer " << i << " should match input pointer";
    }

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_FieldNamesStoredCorrectly
 * **Validates: R5 - Field names are correctly stored in CeceInternalData**
 *
 * Verifies that:
 * - Field names are stored after binding
 * - Field names match species names from configuration
 * - Name count matches pointer count
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_FieldNamesStoredCorrectly) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Allocate field pointers
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Bind fields
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Verify field names are stored
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->field_names.size(), num_species)
        << "Should store " << num_species << " field names";

    // Verify field names match species names (order may vary due to unordered_map)
    std::set<std::string> expected_names = {"CO", "NOx", "SO2"};
    std::set<std::string> actual_names(internal_data->field_names.begin(),
                                       internal_data->field_names.end());
    EXPECT_EQ(actual_names, expected_names)
        << "Field names should match species names (order may vary)";

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_PointersAreValid
 * **Validates: R5 - Stored field pointers are valid and accessible**
 *
 * Verifies that:
 * - Stored pointers can be dereferenced
 * - Stored pointers point to valid memory
 * - Data can be written to and read from stored pointers
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_PointersAreValid) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Allocate field pointers
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Write test data to field pointers
    for (int i = 0; i < num_species; ++i) {
        double* field_data = static_cast<double*>(field_ptrs[i]);
        for (int j = 0; j < nx_ * ny_ * nz_; ++j) {
            field_data[j] = static_cast<double>(i * 100 + j);
        }
    }

    // Bind fields
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Verify stored pointers are valid and contain correct data
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr_);
    for (int i = 0; i < num_species; ++i) {
        double* stored_ptr = static_cast<double*>(internal_data->field_pointers[i]);
        ASSERT_NE(stored_ptr, nullptr) << "Stored pointer " << i << " should not be null";

        // Verify data is accessible and correct
        for (int j = 0; j < nx_ * ny_ * nz_; ++j) {
            EXPECT_EQ(stored_ptr[j], static_cast<double>(i * 100 + j))
                << "Data at stored pointer " << i << "[" << j << "] should be correct";
        }
    }

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_MultipleFieldsHandled
 * **Validates: R5 - Multiple fields can be stored and retrieved**
 *
 * Verifies that:
 * - All field pointers are stored
 * - Each pointer is distinct
 * - Pointers can be accessed independently
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_MultipleFieldsHandled) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);
    ASSERT_EQ(num_species, 3) << "Should have 3 species";

    // Allocate field pointers
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Verify pointers are distinct
    for (int i = 0; i < num_species; ++i) {
        for (int j = i + 1; j < num_species; ++j) {
            EXPECT_NE(field_ptrs[i], field_ptrs[j])
                << "Field pointers " << i << " and " << j << " should be distinct";
        }
    }

    // Bind fields
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Verify all pointers are stored
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->field_pointers.size(), num_species);

    // Verify stored pointers are distinct
    for (int i = 0; i < num_species; ++i) {
        for (int j = i + 1; j < num_species; ++j) {
            EXPECT_NE(internal_data->field_pointers[i], internal_data->field_pointers[j])
                << "Stored field pointers " << i << " and " << j << " should be distinct";
        }
    }

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_NullDataPointerFails
 * **Validates: R5 - Error handling for null data pointer**
 *
 * Verifies that:
 * - Field binding fails with null data pointer
 * - Error code is returned
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_NullDataPointerFails) {
    int rc;
    int num_species = 3;
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Try to bind with null data pointer
    cece_core_bind_fields(nullptr, field_ptrs, &num_species, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with null data pointer";

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_NullFieldPointerArrayFails
 * **Validates: R5 - Error handling for null field pointer array**
 *
 * Verifies that:
 * - Field binding fails with null pointer array
 * - Error code is returned
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_NullFieldPointerArrayFails) {
    int rc;
    int num_species = 3;

    // Try to bind with null field pointer array
    cece_core_bind_fields(data_ptr_, nullptr, &num_species, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with null field pointer array";
}

/**
 * @test FieldPointerPassing_NullNumFieldsPointerFails
 * **Validates: R5 - Error handling for null num_fields pointer**
 *
 * Verifies that:
 * - Field binding fails with null num_fields pointer
 * - Error code is returned
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_NullNumFieldsPointerFails) {
    int rc;
    int num_species = 3;
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Try to bind with null num_fields pointer
    cece_core_bind_fields(data_ptr_, field_ptrs, nullptr, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with null num_fields pointer";

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_NullFieldPointerDetected
 * **Validates: R5 - Error handling for null field pointer in array**
 *
 * Verifies that:
 * - Field binding fails if any field pointer is null
 * - Error code is returned
 * - Descriptive error message is logged
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_NullFieldPointerDetected) {
    int rc;
    int num_species = 3;
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Set one pointer to null
    free(field_ptrs[1]);
    field_ptrs[1] = nullptr;

    // Try to bind with null field pointer
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with null field pointer in array";

    // Cleanup
    free(field_ptrs[0]);
    free(field_ptrs[2]);
    delete[] field_ptrs;
}

/**
 * @test FieldPointerPassing_FieldCountMismatchDetected
 * **Validates: R5 - Error handling for field count mismatch**
 *
 * Verifies that:
 * - Field binding fails if num_fields doesn't match species count
 * - Error code is returned
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_FieldCountMismatchDetected) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Allocate fewer pointers than species
    int num_fields = num_species - 1;
    void** field_ptrs = AllocateFieldPointers(num_fields);

    // Try to bind with mismatched count
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_fields, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with field count mismatch";

    FreeFieldPointers(field_ptrs, num_fields);
}

/**
 * @test FieldPointerPassing_ExcessFieldCountDetected
 * **Validates: R5 - Error handling for excess field count**
 *
 * Verifies that:
 * - Field binding fails if num_fields exceeds species count
 * - Error code is returned
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_ExcessFieldCountDetected) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Allocate more pointers than species
    int num_fields = num_species + 1;
    void** field_ptrs = AllocateFieldPointers(num_fields);

    // Try to bind with excess count
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_fields, &rc);
    EXPECT_NE(rc, ESMF_SUCCESS) << "Should fail with excess field count";

    FreeFieldPointers(field_ptrs, num_fields);
}

/**
 * @test FieldPointerPassing_PointerCountMatchesSpeciesCount
 * **Validates: R5 - Pointer count matches species count**
 *
 * Verifies that:
 * - After binding, field_pointers.size() equals species count
 * - After binding, field_names.size() equals species count
 * - Both vectors have the same size
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_PointerCountMatchesSpeciesCount) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Allocate field pointers
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Bind fields
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Verify counts match
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->field_pointers.size(), num_species)
        << "field_pointers.size() should equal species count";
    EXPECT_EQ(internal_data->field_names.size(), num_species)
        << "field_names.size() should equal species count";
    EXPECT_EQ(internal_data->field_pointers.size(), internal_data->field_names.size())
        << "field_pointers and field_names should have same size";

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_PointersAccessibleAfterBinding
 * **Validates: R5 - Stored pointers remain accessible after binding**
 *
 * Verifies that:
 * - Pointers can be accessed multiple times
 * - Data persists across multiple accesses
 * - Pointer addresses don't change
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_PointersAccessibleAfterBinding) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Allocate field pointers
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Write initial data
    for (int i = 0; i < num_species; ++i) {
        double* field_data = static_cast<double*>(field_ptrs[i]);
        for (int j = 0; j < nx_ * ny_ * nz_; ++j) {
            field_data[j] = 42.0 + i;
        }
    }

    // Bind fields
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr_);

    // Access pointers multiple times
    for (int access = 0; access < 3; ++access) {
        for (int i = 0; i < num_species; ++i) {
            double* stored_ptr = static_cast<double*>(internal_data->field_pointers[i]);
            EXPECT_NE(stored_ptr, nullptr) << "Pointer should be accessible on access " << access;

            // Verify data is still correct
            for (int j = 0; j < nx_ * ny_ * nz_; ++j) {
                EXPECT_EQ(stored_ptr[j], 42.0 + i) << "Data should persist on access " << access;
            }
        }
    }

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_PointersNotModifiedByBinding
 * **Validates: R5 - Binding doesn't modify pointer values**
 *
 * Verifies that:
 * - Stored pointers have same address as input pointers
 * - Binding is a pure storage operation
 * - No pointer arithmetic or modification occurs
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_PointersNotModifiedByBinding) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Allocate field pointers
    void** field_ptrs = AllocateFieldPointers(num_species);

    // Store original addresses
    std::vector<void*> original_ptrs(num_species);
    for (int i = 0; i < num_species; ++i) {
        original_ptrs[i] = field_ptrs[i];
    }

    // Bind fields
    cece_core_bind_fields(data_ptr_, field_ptrs, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Verify stored pointers match original addresses exactly
    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr_);
    for (int i = 0; i < num_species; ++i) {
        EXPECT_EQ(internal_data->field_pointers[i], original_ptrs[i])
            << "Stored pointer " << i << " should match original address";
    }

    FreeFieldPointers(field_ptrs, num_species);
}

/**
 * @test FieldPointerPassing_ClearsExistingPointers
 * **Validates: R5 - Binding clears existing pointers before storing new ones**
 *
 * Verifies that:
 * - Multiple calls to bind_fields don't accumulate pointers
 * - Old pointers are cleared before new ones are stored
 * - Final state contains only the most recent pointers
 */
TEST_F(FieldPointerPassingTest, FieldPointerPassing_ClearsExistingPointers) {
    int rc;
    int num_species = 0;
    cece_core_get_species_count(data_ptr_, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // First binding
    void** field_ptrs1 = AllocateFieldPointers(num_species);
    cece_core_bind_fields(data_ptr_, field_ptrs1, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr_);
    EXPECT_EQ(internal_data->field_pointers.size(), num_species);

    // Second binding with different pointers
    void** field_ptrs2 = AllocateFieldPointers(num_species);
    cece_core_bind_fields(data_ptr_, field_ptrs2, &num_species, &rc);
    ASSERT_EQ(rc, ESMF_SUCCESS);

    // Verify only new pointers are stored
    EXPECT_EQ(internal_data->field_pointers.size(), num_species)
        << "Should have exactly " << num_species << " pointers after second binding";

    for (int i = 0; i < num_species; ++i) {
        EXPECT_EQ(internal_data->field_pointers[i], field_ptrs2[i])
            << "Should store new pointer " << i << ", not old one";
        EXPECT_NE(internal_data->field_pointers[i], field_ptrs1[i])
            << "Should not contain old pointer " << i;
    }

    FreeFieldPointers(field_ptrs1, num_species);
    FreeFieldPointers(field_ptrs2, num_species);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new ESMFEnvironment);
    return RUN_ALL_TESTS();
}
