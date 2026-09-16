#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <string>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_data_ingestor.hpp"
#include "cece/cece_state.hpp"

/**
 * @file test_ingest_setfield_bypass.cpp
 * @brief Structural unit test for the consolidated AMIO ingest path.
 *
 * Feature: ingest-copy-consolidation (Tier 1, task 7.1)
 *
 * On the consolidated AMIO-driven path the driver facade populates
 * `import_state.fields[var_name]` directly and no longer routes through the
 * ingestor: `AdvanceTime` no longer calls `cece_ingestor_set_field`
 * (-> `CeceDataIngestor::SetField`). As a result, `field_cache_` is never
 * populated for AMIO variables, so `HasCachedField(var)` returns false, and the
 * `IngestEmissionsInline` copy-back becomes a no-op for those variables (its
 * `HasCachedField` guard skips them).
 *
 * These tests assert that structural contract at the ingestor API level:
 *  - a fresh ingestor has no cached AMIO variable (SetField not called);
 *  - the copy-back does not write `import_state` for an un-cached AMIO variable;
 *  - the preserved API (SetField) still works when a non-AMIO consumer calls it
 *    directly (Req 3.3).
 *
 * _Requirements: 3.1 (with 3.3 preservation guard)_
 */

namespace cece::test {

namespace {

// Builds a single-stream, single-variable config for an AMIO-driven variable.
CeceDataConfig MakeAmioConfig(const std::string& file_name, const std::string& model_name) {
    CeceDataConfig config;
    CeceDataStreamConfig stream;
    CeceDataVariableConfig variable;
    variable.name_in_file = file_name;
    variable.name_in_model = model_name;
    stream.variables.push_back(variable);
    config.streams.push_back(stream);
    return config;
}

}  // namespace

class SetFieldBypassTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

// On the consolidated path the driver never calls SetField for AMIO variables,
// so field_cache_ is never populated: HasCachedField must be false.
TEST_F(SetFieldBypassTest, FieldCacheNotPopulatedForAmioVariable) {
    CeceDataIngestor ingestor;

    // Consolidated AMIO path: SetField is intentionally NOT invoked for this
    // AMIO variable. The cache must therefore have no entry for it.
    EXPECT_FALSE(ingestor.HasCachedField("amio_emission_field"));
    EXPECT_FALSE(ingestor.HasDataIngesterField("amio_emission_field"));
}

// Because field_cache_ is not populated for AMIO variables, the copy-back
// (IngestEmissionsInline) is a no-op for them: it must not write import_state.
TEST_F(SetFieldBypassTest, CopyBackDoesNotWriteImportStateWhenNotCached) {
    constexpr int nx = 3;
    constexpr int ny = 2;
    constexpr int nz = 1;

    CeceDataIngestor ingestor;
    const CeceDataConfig config = MakeAmioConfig("AMIO_FILE", "amio_emission_field");

    // Precondition: consolidated path did not SetField this AMIO variable.
    ASSERT_FALSE(ingestor.HasCachedField("amio_emission_field"));

    CeceImportState import_state;
    ingestor.IngestEmissionsInline(config, import_state, nx, ny, nz);

    // The copy-back must skip the un-cached AMIO variable, leaving import_state
    // untouched by the ingestor (the driver facade is the sole writer).
    EXPECT_FALSE(import_state.fields.contains("amio_emission_field"));
}

// Req 3.3 preservation guard: the SetField API remains functional for a
// non-AMIO consumer that calls it directly. Bypassing on the AMIO path must not
// break the standalone ingestor contract.
TEST_F(SetFieldBypassTest, SetFieldStillWorksWhenCalledDirectly) {
    constexpr int nx = 3;
    constexpr int ny = 2;
    constexpr int levels = 1;
    constexpr int horizontal_size = nx * ny;

    std::vector<double> values(static_cast<size_t>(levels) * horizontal_size);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            values[static_cast<size_t>(j) * nx + i] = 100.0 * j + i + 0.5;
        }
    }

    CeceDataIngestor ingestor;

    // Before a direct SetField call, the field is not cached.
    EXPECT_FALSE(ingestor.HasCachedField("non_amio_consumer_field"));

    int rc = -1;
    ingestor.SetField("non_amio_consumer_field", values.data(), levels, horizontal_size, nx, ny,
                      /*nz=*/1, &rc);
    ASSERT_EQ(rc, 0);

    // A direct SetField call still populates the cache (API preserved).
    EXPECT_TRUE(ingestor.HasCachedField("non_amio_consumer_field"));

    // And the copy-back writes import_state for the directly-set field.
    const CeceDataConfig config = MakeAmioConfig("NON_AMIO_FILE", "non_amio_consumer_field");
    CeceImportState import_state;
    ingestor.IngestEmissionsInline(config, import_state, nx, ny, /*nz=*/1);
    ASSERT_TRUE(import_state.fields.contains("non_amio_consumer_field"));

    const auto imported = import_state.fields.at("non_amio_consumer_field").view_device();
    const auto host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), imported);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const double expected = values[static_cast<size_t>(j) * nx + i];
            EXPECT_DOUBLE_EQ(host(i, j, 0), expected) << "i=" << i << " j=" << j;
        }
    }
}

}  // namespace cece::test

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
