/**
 * @file test_field_resolution_priority.cpp
 * @brief Tests for field resolution in the data ingestor (CDEPS cache only).
 *
 * **Validates: Requirements 3.3, 3.7**
 *
 * After ESMF decoupling, ResolveField checks only the CDEPS cache.
 * The ESMF fallback has been removed; the Fortran cap is responsible for
 * populating the import state before the run phase.
 */

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <memory>
#include <random>
#include <vector>

#include "aces/aces_config.hpp"
#include "aces/aces_data_ingestor.hpp"

namespace aces::test {

class FieldResolutionPriorityTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
        rng_.seed(42);
    }

    std::mt19937 rng_;
    std::vector<std::shared_ptr<std::vector<double>>> cdeps_data_storage_;
};

/**
 * @brief HasCDEPSField returns false for a field that was never added.
 *
 * **Validates: Requirements 3.3**
 */
TEST_F(FieldResolutionPriorityTest, FieldNotFound) {
    AcesDataIngestor ingestor;
    EXPECT_FALSE(ingestor.HasDataIngesterField("nonexistent_field"));
    EXPECT_FALSE(ingestor.HasCachedField("nonexistent_field"));
}

/**
 * @brief HasCachedField is an alias for HasDataIngesterField.
 *
 * **Validates: Requirements 3.3**
 */
TEST_F(FieldResolutionPriorityTest, HasCachedFieldMatchesHasDataIngesterField) {
    AcesDataIngestor ingestor;
    const std::string name = "some_field";
    EXPECT_EQ(ingestor.HasDataIngesterField(name), ingestor.HasCachedField(name));
}

/**
 * @brief ResolveField returns an empty view when the field is absent.
 *
 * **Validates: Requirements 3.3**
 */
TEST_F(FieldResolutionPriorityTest, ResolveFieldReturnsEmptyWhenAbsent) {
    AcesDataIngestor ingestor;
    auto view = ingestor.ResolveField("missing", 4, 4, 4);
    EXPECT_EQ(view.data(), nullptr);
}

/**
 * @brief ResolveField return type carries the Unmanaged memory trait.
 *
 * **Validates: Requirements 3.7**
 */
TEST_F(FieldResolutionPriorityTest, ResolveFieldReturnTypeIsUnmanaged) {
    using ExpectedViewType =
        Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                     Kokkos::MemoryTraits<Kokkos::Unmanaged>>;

    static_assert(
        std::is_same_v<decltype(std::declval<AcesDataIngestor>().ResolveField("", 0, 0, 0)),
                       ExpectedViewType>,
        "ResolveField must return a view with Kokkos::MemoryTraits<Kokkos::Unmanaged>");

    SUCCEED();
}

/**
 * @brief Various field name patterns are handled without crashing.
 *
 * **Validates: Requirements 3.3**
 */
TEST_F(FieldResolutionPriorityTest, FieldNameVariations) {
    AcesDataIngestor ingestor;
    const std::vector<std::string> names = {
        "CO",
        "carbon_monoxide",
        "NOx_emissions_total",
        "CEDS_CO_anthro_2020",
        "field123",
        "a",
        "very_long_field_name_with_many_components_for_testing"};

    for (const auto& name : names) {
        EXPECT_FALSE(ingestor.HasDataIngesterField(name))
            << "Field '" << name << "' should not be in cache before any ingestion";
        auto view = ingestor.ResolveField(name, 5, 5, 5);
        EXPECT_EQ(view.data(), nullptr) << "ResolveField should return empty view for '" << name
                                        << "' when not in cache";
    }
}

}  // namespace aces::test

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
