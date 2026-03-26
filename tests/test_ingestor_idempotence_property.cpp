/**
 * @file test_ingestor_idempotence_property.cpp
 * @brief Property 8: Ingestor idempotence at same time step.
 *
 * For any model time T, two calls to IngestEmissionsInline at the same T
 * shall produce identical DualView3D values in AcesImportState.
 *
 * Uses real ESMF objects and real data files (data/MACCity_4x5.nc).
 * No mocking per the ACES no-mock policy.
 *
 * Validates: Requirements 9.2
 */

#include <ESMC.h>
#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <fstream>
#include <vector>

#include "aces/aces_config.hpp"
#include "aces/aces_data_ingestor.hpp"
#include "aces/aces_state.hpp"

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
    void TearDown() override { ESMC_Finalize(); }
};

// ---------------------------------------------------------------------------
// Helpers (same as other property tests)
// ---------------------------------------------------------------------------
ESMC_Mesh MakeGlobalMesh() {
    int rc;
    ESMC_CoordSys_Flag cs = ESMC_COORDSYS_SPH_DEG;
    ESMC_Mesh mesh = ESMC_MeshCreate(2, 2, &cs, &rc);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("MeshCreate failed");
    int node_ids[]    = {1, 2, 3, 4};
    int node_owners[] = {0, 0, 0, 0};
    double node_coords[] = {-180.0,-90.0, 180.0,-90.0, 180.0,90.0, -180.0,90.0};
    rc = ESMC_MeshAddNodes(mesh, 4, node_ids, node_coords, node_owners, &rc);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("MeshAddNodes failed");
    int elem_ids[]   = {1};
    int elem_types[] = {ESMC_MESHELEMTYPE_QUAD};
    int elem_conn[]  = {1, 2, 3, 4};
    rc = ESMC_MeshAddElements(mesh, 1, elem_ids, elem_types, elem_conn,
                              nullptr, nullptr, nullptr);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("MeshAddElements failed");
    return mesh;
}

ESMC_Clock MakeClock() {
    int rc;
    ESMC_Calendar cal = ESMC_CalendarCreate("Greg", ESMC_CALKIND_GREGORIAN, &rc);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("CalendarCreate failed");
    ESMC_Time start, stop;
    ESMC_TimeSet(&start, 2020, 0,  cal, ESMC_CALKIND_GREGORIAN, 0);
    ESMC_TimeSet(&stop,  2020, 48, cal, ESMC_CALKIND_GREGORIAN, 0);
    ESMC_TimeInterval dt;
    ESMC_TimeIntervalSet(&dt, 86400);
    ESMC_Clock clock = ESMC_ClockCreate("clk", dt, start, stop, &rc);
    if (rc != ESMF_SUCCESS) throw std::runtime_error("ClockCreate failed");
    return clock;
}

AcesCdepsConfig MakeMACCityConfig() {
    AcesCdepsConfig config;
    AcesDataStreamConfig s;
    s.name = "MACCITY";
    s.file_paths = {"data/MACCity_4x5.nc"};
    s.taxmode  = "cycle"; s.tintalgo = "linear"; s.mapalgo = "bilinear";
    s.yearFirst = 2000; s.yearLast = 2000; s.yearAlign = 2020;
    AcesDataVariableConfig v;
    v.name_in_file = "MACCity"; v.name_in_model = "MACCITY";
    s.variables.push_back(v);
    config.streams.push_back(s);
    return config;
}

// ---------------------------------------------------------------------------
// Helper: flatten a DualView3D host mirror into a std::vector<double>
// ---------------------------------------------------------------------------
std::vector<double> Flatten(DualView3D& dv) {
    dv.sync_host();
    auto h = dv.view_host();
    std::vector<double> out;
    out.reserve(h.extent(0) * h.extent(1) * h.extent(2));
    for (size_t i = 0; i < h.extent(0); ++i)
        for (size_t j = 0; j < h.extent(1); ++j)
            for (size_t k = 0; k < h.extent(2); ++k)
                out.push_back(h(i, j, k));
    return out;
}

// ---------------------------------------------------------------------------
// Property 8: two IngestEmissionsInline calls at the same clock time
// produce bit-identical results.
// ---------------------------------------------------------------------------
TEST(IngestorIdempotenceProperty, TwoCallsAtSameTimeProduceIdenticalValues) {
    if (!std::ifstream("data/MACCity_4x5.nc")) {
        GTEST_SKIP() << "data/MACCity_4x5.nc not present";
    }

    ESMC_Mesh  mesh;
    ESMC_Clock clock;
    try { mesh = MakeGlobalMesh(); clock = MakeClock(); }
    catch (const std::exception& e) { GTEST_SKIP() << "ESMF setup: " << e.what(); }

    auto config = MakeMACCityConfig();

    AcesDataIngestor ingestor;
    ASSERT_NO_THROW(ingestor.InitializeDataIngester(nullptr, clock.ptr, mesh.ptr, config));

    // First ingest
    AcesImportState state1;
    ASSERT_NO_THROW(ingestor.IngestEmissionsInline(config, state1, 36, 18, 1));

    // Second ingest — same clock, same ingestor
    AcesImportState state2;
    ASSERT_NO_THROW(ingestor.IngestEmissionsInline(config, state2, 36, 18, 1));

    ASSERT_TRUE(state1.fields.count("MACCITY"));
    ASSERT_TRUE(state2.fields.count("MACCITY"));

    auto v1 = Flatten(state1.fields["MACCITY"]);
    auto v2 = Flatten(state2.fields["MACCITY"]);

    ASSERT_EQ(v1.size(), v2.size())
        << "Property 8: both ingest calls must produce same-size fields";

    for (size_t i = 0; i < v1.size(); ++i) {
        EXPECT_DOUBLE_EQ(v1[i], v2[i])
            << "Property 8: values must be identical at index " << i;
    }

    ingestor.FinalizeDataIngester();
    ESMC_MeshDestroy(&mesh);
    ESMC_ClockDestroy(&clock);
}

}  // namespace
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new aces::ESMFEnvironment);
    return RUN_ALL_TESTS();
}
