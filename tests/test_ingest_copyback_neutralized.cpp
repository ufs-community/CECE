#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <string>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_data_ingestor.hpp"
#include "cece/cece_state.hpp"

/**
 * @file test_ingest_copyback_neutralized.cpp
 * @brief Structural unit test that the ingestor copy-back is neutralized for
 *        AMIO variables on the consolidated ingest path.
 *
 * Feature: ingest-copy-consolidation (Tier 1, task 7.2)
 *
 * On the consolidated AMIO-driven path the driver facade writes
 * `import_state.fields[var_name]` directly and no longer routes through the
 * ingestor. Because `AdvanceTime` no longer calls `cece_ingestor_set_field`
 * (-> `CeceDataIngestor::SetField`), `field_cache_` is never populated for AMIO
 * variables. `IngestEmissionsInline` guards its copy-back with
 * `HasCachedField(model_name)`; with no cached entry that guard returns false
 * and the copy-back is skipped entirely — a no-op for AMIO variables.
 *
 * These tests assert that copy-back neutralization at the ingestor API level:
 *  - `HasCachedField` is false for an un-cached AMIO variable (the guard that
 *    makes the copy-back a no-op);
 *  - `IngestEmissionsInline` writes nothing into `import_state` for an un-cached
 *    AMIO variable (no field created, no value copied);
 *  - the copy-back leaves a value the driver facade already wrote to
 *    `import_state` untouched — it does not overwrite the authoritative write;
 *  - it stays a no-op across repeated steps (no stale/duplicate write appears).
 *
 * _Requirements: 3.2_
 */

namespace cece::test {

namespace {

// Builds a single-stream config listing the given AMIO-driven variables.
CeceDataConfig MakeAmioConfig(const std::vector<std::string>& model_names) {
    CeceDataConfig config;
    CeceDataStreamConfig stream;
    for (const auto& model_name : model_names) {
        CeceDataVariableConfig variable;
        variable.name_in_file = model_name + "_FILE";
        variable.name_in_model = model_name;
        stream.variables.push_back(variable);
    }
    config.streams.push_back(stream);
    return config;
}

}  // namespace

class CopyBackNeutralizedTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }
};

// The copy-back guard (HasCachedField) is false for AMIO variables that the
// consolidated path never routed through SetField. This is the condition that
// neutralizes the copy-back.
TEST_F(CopyBackNeutralizedTest, HasCachedFieldFalseForAmioVariable) {
    CeceDataIngestor ingestor;

    EXPECT_FALSE(ingestor.HasCachedField("amio_emission_field"));
    EXPECT_FALSE(ingestor.HasDataIngesterField("amio_emission_field"));
}

// IngestEmissionsInline performs no copy-back for an un-cached AMIO variable:
// it neither creates the import field nor writes any value.
TEST_F(CopyBackNeutralizedTest, InlineDoesNotWriteImportStateForUncachedAmioVariable) {
    constexpr int nx = 4;
    constexpr int ny = 3;
    constexpr int nz = 1;

    CeceDataIngestor ingestor;
    const CeceDataConfig config = MakeAmioConfig({"amio_emission_field"});

    // Precondition: the consolidated path did not SetField this AMIO variable.
    ASSERT_FALSE(ingestor.HasCachedField("amio_emission_field"));

    CeceImportState import_state;
    ingestor.IngestEmissionsInline(config, import_state, nx, ny, nz);

    // The guard skipped the un-cached AMIO variable: nothing written.
    EXPECT_TRUE(import_state.fields.empty());
    EXPECT_FALSE(import_state.fields.contains("amio_emission_field"));
}

// The neutralized copy-back must not disturb the authoritative facade write:
// a value already present in import_state is left byte-for-byte unchanged.
TEST_F(CopyBackNeutralizedTest, InlineDoesNotOverwriteFacadeWrittenField) {
    constexpr int nx = 4;
    constexpr int ny = 3;
    constexpr int nz = 1;

    CeceDataIngestor ingestor;
    const CeceDataConfig config = MakeAmioConfig({"amio_emission_field"});

    // Simulate the driver facade being the sole authoritative writer: it has
    // already populated import_state for this AMIO variable.
    CeceImportState import_state;
    DualView3D facade_field("amio_emission_field", nx, ny, nz);
    auto device_view = facade_field.view_device();
    auto host_seed = Kokkos::create_mirror_view(device_view);
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            host_seed(i, j, 0) = 10.0 * i + j + 0.25;
        }
    }
    Kokkos::deep_copy(device_view, host_seed);
    facade_field.modify_device();
    Kokkos::fence();
    import_state.fields.emplace("amio_emission_field", facade_field);

    // Precondition: variable is not in the ingestor cache (AMIO bypass).
    ASSERT_FALSE(ingestor.HasCachedField("amio_emission_field"));

    // The neutralized copy-back must be a no-op and not touch the facade write.
    ingestor.IngestEmissionsInline(config, import_state, nx, ny, nz);

    ASSERT_TRUE(import_state.fields.contains("amio_emission_field"));
    const auto after = import_state.fields.at("amio_emission_field").view_device();
    const auto host_after = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), after);
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            const double expected = 10.0 * i + j + 0.25;
            EXPECT_DOUBLE_EQ(host_after(i, j, 0), expected) << "i=" << i << " j=" << j;
        }
    }
}

// Repeated steps keep the copy-back neutralized for AMIO variables: no field is
// ever created by the ingestor across multiple invocations.
TEST_F(CopyBackNeutralizedTest, InlineStaysNoOpAcrossRepeatedSteps) {
    constexpr int nx = 3;
    constexpr int ny = 2;
    constexpr int nz = 1;

    CeceDataIngestor ingestor;
    const CeceDataConfig config = MakeAmioConfig({"amio_a", "amio_b"});

    CeceImportState import_state;
    for (int step = 0; step < 5; ++step) {
        ingestor.IngestEmissionsInline(config, import_state, nx, ny, nz);
        EXPECT_TRUE(import_state.fields.empty()) << "step=" << step;
    }
}

}  // namespace cece::test

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
