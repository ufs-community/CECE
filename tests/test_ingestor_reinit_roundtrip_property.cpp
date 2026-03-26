/**
 * @file test_ingestor_reinit_roundtrip_property.cpp
 * @brief Property 9: Ingestor round-trip re-initialization.
 *
 * The sequence Init → Ingest → Finalize → Init → Ingest shall produce field
 * values identical to the first Ingest call at the same model time.
 *
 * Uses real ESMF objects and real data files (data/MACCity_4x5.nc).
 * No mocking per the ACES no-mock policy.
 *
 * Validates: Requirements 9.4
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
// Helpers
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
// Property 9: Init → Ingest → Finalize → Init → Ingest produces identical values
// ---------------------------------------------------------------------------
TEST(IngestorReinitRoundtripProperty, ReinitProducesIdenticalValues) {
    if (!std::ifstream("data/MACCity_4x5.nc")) {
        GTEST_SKIP() << "data/MACCity_4x5.nc not present";
    }

    ESMC_Mesh  mesh;
    ESMC_Clock clock;
    try { mesh = MakeGlobalMesh(); clock = MakeClock(); }
    catch (const std::exception& e) { GTEST_SKIP() << "ESMF setup: " << e.what(); }

    auto config = MakeMACCityConfig();
    const int nx = 36, ny = 18, nz = 1;

    // --- First cycle: Init → Ingest → Finalize ---
    AcesDataIngestor ingestor1;
    ASSERT_NO_THROW(ingestor1.InitializeDataIngester(nullptr, clock.ptr, mesh.ptr, config));

    AcesImportState state1;
    ASSERT_NO_THROW(ingestor1.IngestEmissionsInline(config, state1, nx, ny, nz));
    ASSERT_TRUE(state1.fields.count("MACCITY"));
    auto v1 = Flatten(state1.fields["MACCITY"]);

    ASSERT_NO_THROW(ingestor1.FinalizeDataIngester());

    // --- Second cycle: fresh ingestor, same clock, same config ---
    AcesDataIngestor ingestor2;
    ASSERT_NO_THROW(ingestor2.InitializeDataIngester(nullptr, clock.ptr, mesh.ptr, config));

    AcesImportState state2;
    ASSERT_NO_THROW(ingestor2.IngestEmissionsInline(config, state2, nx, ny, nz));
    ASSERT_TRUE(state2.fields.count("MACCITY"));
    auto v2 = Flatten(state2.fields["MACCITY"]);

    ASSERT_NO_THROW(ingestor2.FinalizeDataIngester());

    // Property 9: values must be identical
    ASSERT_EQ(v1.size(), v2.size())
        << "Property 9: reinit must produce same-size field";

    for (size_t i = 0; i < v1.size(); ++i) {
        EXPECT_DOUBLE_EQ(v1[i], v2[i])
            << "Property 9: value mismatch at index " << i
            << " after reinit roundtrip";
    }

    ESMC_MeshDestroy(&mesh);
    ESMC_ClockDestroy(&clock);
}

// ---------------------------------------------------------------------------
// Property 9b: Finalize on an uninitialised ingestor must not throw
// ---------------------------------------------------------------------------
TEST(IngestorReinitRoundtripProperty, FinalizeOnUninitializedIngestorIsNoop) {
    AcesDataIngestor ingestor;
    EXPECT_NO_THROW(ingestor.FinalizeDataIngester())
        << "FinalizeDataIngester on an uninitialised ingestor must be a no-op";
}

}  // namespace
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new aces::ESMFEnvironment);
    return RUN_ALL_TESTS();
}
