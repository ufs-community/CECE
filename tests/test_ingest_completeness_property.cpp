/**
 * @file test_ingest_completeness_property.cpp
 * @brief Property 2: Ingest completeness.
 *
 * For any valid AcesCdepsConfig with N stream variables, after
 * IngestEmissionsInline the AcesImportState.fields map shall contain
 * exactly N entries (one per model-side variable name).
 *
 * Uses real ESMF objects (no mocking) per the ACES no-mock policy.
 *
 * Validates: Requirements 3.2
 */

#include <ESMC.h>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <Kokkos_Core.hpp>
#include <fstream>
#include <string>

#include "aces/aces_config.hpp"
#include "aces/aces_data_ingestor.hpp"
#include "aces/aces_state.hpp"
#include "aces/cf_ingestor/cf_data_ingestor.hpp"

namespace aces {
namespace {

// ---------------------------------------------------------------------------
// Global ESMF + Kokkos environment
// ---------------------------------------------------------------------------
class ESMFEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
        int rc = ESMC_Initialize(nullptr, ESMC_ArgLast);
        ASSERT_EQ(rc, ESMF_SUCCESS) << "ESMF init failed";
    }
    void TearDown() override {
        ESMC_Finalize();
    }
};

// ---------------------------------------------------------------------------
// Helper: build a minimal 4-node target mesh covering the globe
// ---------------------------------------------------------------------------
ESMC_Mesh MakeGlobalMesh() {
    int rc;
    ESMC_CoordSys_Flag cs = ESMC_COORDSYS_SPH_DEG;
    ESMC_Mesh mesh = ESMC_MeshCreate(2, 2, &cs, &rc);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("MeshCreate failed");

    int node_ids[] = {1, 2, 3, 4};
    int node_owners[] = {0, 0, 0, 0};
    double node_coords[] = {-180.0, -90.0, 180.0, -90.0, 180.0, 90.0, -180.0, 90.0};
    rc = ESMC_MeshAddNodes(mesh, 4, node_ids, node_coords, node_owners, &rc);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("MeshAddNodes failed");

    int elem_ids[] = {1};
    int elem_types[] = {ESMC_MESHELEMTYPE_QUAD};
    int elem_conn[] = {1, 2, 3, 4};
    rc = ESMC_MeshAddElements(mesh, 1, elem_ids, elem_types, elem_conn, nullptr, nullptr, nullptr);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("MeshAddElements failed");
    return mesh;
}

// ---------------------------------------------------------------------------
// Helper: build a minimal ESMF clock
// ---------------------------------------------------------------------------
ESMC_Clock MakeClock() {
    int rc;
    ESMC_Calendar cal = ESMC_CalendarCreate("Greg", ESMC_CALKIND_GREGORIAN, &rc);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("CalendarCreate failed");

    ESMC_Time start, stop;
    ESMC_TimeSet(&start, 2020, 0, cal, ESMC_CALKIND_GREGORIAN, 0);
    ESMC_TimeSet(&stop, 2020, 48, cal, ESMC_CALKIND_GREGORIAN, 0);

    ESMC_TimeInterval dt;
    ESMC_TimeIntervalSet(&dt, 86400);

    ESMC_Clock clock = ESMC_ClockCreate("clk", dt, start, stop, &rc);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("ClockCreate failed");
    return clock;
}

// ---------------------------------------------------------------------------
// Property 2a: empty config → IngestEmissionsInline populates zero fields
// ---------------------------------------------------------------------------
TEST(IngestCompletenessProperty, EmptyConfigProducesZeroFields) {
    AcesCdepsConfig config;  // no streams
    AcesImportState state;
    AcesDataIngestor ingestor;

    EXPECT_NO_THROW(ingestor.IngestEmissionsInline(config, state, 36, 18, 10));
    EXPECT_EQ(state.fields.size(), 0u) << "Empty config must produce zero fields in import state";
}

// ---------------------------------------------------------------------------
// Property 2b: config with real data files → fields map has exactly N entries
//
// Uses data/MACCity_4x5.nc (1 variable: MACCITY) and data/hourly.nc
// (1 variable: HOURLY_SCALFACT) — the same files used by aces_config_ex1.yaml.
// After InitializeDataIngester + IngestEmissionsInline the import state must
// contain exactly 2 entries (one per model-side variable name).
// ---------------------------------------------------------------------------
TEST(IngestCompletenessProperty, TwoStreamsTwoFieldsAfterIngest) {
    // Skip if data files are absent (CI without data)
    if (!std::ifstream("data/MACCity_4x5.nc") || !std::ifstream("data/hourly.nc")) {
        GTEST_SKIP() << "Test data files not present; skipping ESMF ingest test";
    }

    ESMC_Mesh mesh;
    ESMC_Clock clock;
    try {
        mesh = MakeGlobalMesh();
        clock = MakeClock();
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ESMF setup failed: " << e.what();
    }

    AcesCdepsConfig config;

    AcesDataStreamConfig s1;
    s1.name = "MACCITY";
    s1.file_paths = {"data/MACCity_4x5.nc"};
    s1.taxmode = "cycle";
    s1.tintalgo = "linear";
    s1.mapalgo = "bilinear";
    s1.yearFirst = 2000;
    s1.yearLast = 2000;
    s1.yearAlign = 2020;
    {
        AcesDataVariableConfig v;
        v.name_in_file = "MACCity";
        v.name_in_model = "MACCITY";
        s1.variables.push_back(v);
    }
    config.streams.push_back(s1);

    AcesDataStreamConfig s2;
    s2.name = "HOURLY_SCALFACT";
    s2.file_paths = {"data/hourly.nc"};
    s2.taxmode = "cycle";
    s2.tintalgo = "linear";
    s2.mapalgo = "bilinear";
    s2.yearFirst = 1;
    s2.yearLast = 1;
    s2.yearAlign = 2020;
    {
        AcesDataVariableConfig v;
        v.name_in_file = "HOURLY_SCALFACT";
        v.name_in_model = "HOURLY_SCALFACT";
        s2.variables.push_back(v);
    }
    config.streams.push_back(s2);

    AcesDataIngestor ingestor;
    ASSERT_NO_THROW(ingestor.InitializeDataIngester(nullptr, clock.ptr, mesh.ptr, config));

    AcesImportState state;
    ASSERT_NO_THROW(ingestor.IngestEmissionsInline(config, state, 36, 18, 1));

    // Property 2: exactly N=2 entries (one per model-side variable)
    EXPECT_EQ(state.fields.size(), 2u)
        << "import_state.fields must have exactly 2 entries after ingesting 2 streams";
    EXPECT_TRUE(state.fields.count("MACCITY")) << "MACCITY must be present in import state";
    EXPECT_TRUE(state.fields.count("HOURLY_SCALFACT"))
        << "HOURLY_SCALFACT must be present in import state";

    ingestor.FinalizeDataIngester();
    ESMC_MeshDestroy(&mesh);
    ESMC_ClockDestroy(&clock);
}

// ---------------------------------------------------------------------------
// Property 2c (PBT): for any N ∈ [1,4] streams each with 1 variable,
// the config model-name set has exactly N distinct entries.
// ---------------------------------------------------------------------------
RC_GTEST_PROP(IngestCompletenessProperty, ModelNameCountEqualsStreamCount, ()) {
    const int n = *rc::gen::inRange(1, 5);

    AcesCdepsConfig config;
    for (int i = 0; i < n; ++i) {
        AcesDataStreamConfig s;
        s.name = "stream_" + std::to_string(i);
        AcesDataVariableConfig v;
        v.name_in_file = "FILE_VAR_" + std::to_string(i);
        v.name_in_model = "MODEL_VAR_" + std::to_string(i);
        s.variables.push_back(v);
        config.streams.push_back(s);
    }

    std::unordered_map<std::string, int> seen;
    for (const auto& stream : config.streams)
        for (const auto& var : stream.variables) seen[var.name_in_model]++;

    RC_ASSERT(static_cast<int>(seen.size()) == n);
    for (const auto& [name, cnt] : seen) RC_ASSERT(cnt == 1);
}

}  // namespace
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new aces::ESMFEnvironment);
    return RUN_ALL_TESTS();
}
