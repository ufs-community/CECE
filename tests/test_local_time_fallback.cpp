// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Feature 001 — T022/T024/T025 [US3]: safe opt-in + graceful UTC fallback.
//
// Drives the real initialization entry point (cece_core_local_time_init) with a
// directly-constructed CeceInternalData (no file parser involved) and asserts
// the deployment-safety contract:
//   * disabled  => no service, no file access, rc 0 (byte-identical baseline).
//   * missing / corrupt grid => EXACTLY ONE warning, a UTC-fallback service is
//     attached, every lookup yields UTC parts, rc stays 0 (never a silent zero
//     that masks a real offset, FR-008).
// Warning capture goes through helm/LOGS (CeceLogger::AddStreamSink), never by
// hijacking std::cout.

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_local_time.hpp"
#include "cece/cece_logger.hpp"
#include "cece/utc_grid.hpp"

#ifndef CECE_SOURCE_DIR
#define CECE_SOURCE_DIR "."
#endif

extern "C" void cece_core_local_time_init(void* data_ptr, int nx, int ny, int nz, const double* lon_coords, int lon_len, const double* lat_coords,
                                          int lat_len, int mpi_comm_f, int* rc);

namespace cece {
namespace {

constexpr std::string_view kFallbackMarker = "continuing in UTC mode";

class LocalTimeFallbackTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
        // Register a LOGS sink on a capture stream; count matching records.
        capture_ = std::make_unique<std::ostringstream>();
        CeceLogger::GetInstance().AddStreamSink(*capture_);
    }
    void TearDown() override {
        capture_.reset();
    }

    int CountFallbackWarnings() const {
        const std::string text = capture_->str();
        int n = 0;
        for (size_t pos = 0; (pos = text.find(kFallbackMarker, pos)) != std::string::npos; pos += kFallbackMarker.size()) ++n;
        return n;
    }

    // Coordinates: a 2x1 native grid (NY + Tokyo longitudes, one latitude).
    std::vector<double> lons_{-74.0, 139.7};
    std::vector<double> lats_{40.0};
    std::unique_ptr<std::ostringstream> capture_;
};

// ---------------------------------------------------------------------------
// Disabled short-circuit (T025): no service, no file access
// ---------------------------------------------------------------------------

TEST_F(LocalTimeFallbackTest, DisabledAttachesNoServiceEvenWithBogusPath) {
    CeceInternalData data;
    data.config.local_time.enabled = false;
    data.config.local_time.grid_file = "/definitely/not/here.rle";  // would throw if opened
    int rc = -12345;
    cece_core_local_time_init(&data, 2, 1, 1, lons_.data(), 2, lats_.data(), 1, 0, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(data.local_time, nullptr);  // never allocated — exact baseline path
    EXPECT_EQ(CountFallbackWarnings(), 0);
}

// ---------------------------------------------------------------------------
// Missing grid file => one warning + UTC fallback (T022, T024)
// ---------------------------------------------------------------------------

TEST_F(LocalTimeFallbackTest, MissingGridWarnsOnceAndFallsBackToUtc) {
    CeceInternalData data;
    data.config.local_time.enabled = true;
    data.config.local_time.grid_file = "/definitely/not/here_f720r.rle";
    int rc = -1;
    cece_core_local_time_init(&data, 2, 1, 1, lons_.data(), 2, lats_.data(), 1, 0, &rc);
    EXPECT_EQ(rc, 0);  // run must complete, not abort
    ASSERT_NE(data.local_time, nullptr);
    EXPECT_TRUE(data.local_time->UtcFallback());
    EXPECT_EQ(CountFallbackWarnings(), 1) << "exactly one startup warning required (FR-008)";

    // Every lookup yields UTC parts (offset 0 everywhere).
    const std::int64_t epoch = 1768456800;  // 06:00Z
    EXPECT_EQ(data.local_time->LocalHourAt(0, 0, epoch), 6);
    EXPECT_EQ(data.local_time->LocalHourAt(1, 0, epoch), 6);
    const auto p = data.local_time->Resolve(40.0, -74.0, epoch);
    EXPECT_EQ(p.hour, 6);
    EXPECT_EQ(p.day_of_week, 4);
    EXPECT_EQ(p.month, 0);
}

// ---------------------------------------------------------------------------
// Truncated grid file => one warning + UTC fallback (all-or-nothing, FR-003)
// ---------------------------------------------------------------------------

TEST_F(LocalTimeFallbackTest, TruncatedGridWarnsOnceAndFallsBackToUtc) {
    std::ifstream in(std::string(CECE_SOURCE_DIR) + "/data/utc_grid_f720r.rle", std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in));
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 30u);
    const std::string tmp = "/tmp/cece_localtime_truncated.rle";
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 2));
    }

    CeceInternalData data;
    data.config.local_time.enabled = true;
    data.config.local_time.grid_file = tmp;
    int rc = -1;
    cece_core_local_time_init(&data, 2, 1, 1, lons_.data(), 2, lats_.data(), 1, 0, &rc);
    std::remove(tmp.c_str());

    EXPECT_EQ(rc, 0);
    ASSERT_NE(data.local_time, nullptr);
    EXPECT_TRUE(data.local_time->UtcFallback());
    EXPECT_EQ(CountFallbackWarnings(), 1);
    // A partial grid must never mask real offsets: fallback is uniform UTC.
    auto host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), data.local_time->Offsets());
    EXPECT_EQ(host(0, 0), 0);
    EXPECT_EQ(host(1, 0), 0);
}

// ---------------------------------------------------------------------------
// Happy path: a valid grid loads with NO warning and non-zero offsets
// ---------------------------------------------------------------------------

TEST_F(LocalTimeFallbackTest, ValidGridLoadsWithoutWarning) {
    CeceInternalData data;
    data.config.local_time.enabled = true;
    data.config.local_time.grid_file = std::string(CECE_SOURCE_DIR) + "/data/utc_grid_f720r.rle";
    int rc = -1;
    cece_core_local_time_init(&data, 2, 1, 1, lons_.data(), 2, lats_.data(), 1, 0, &rc);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(data.local_time, nullptr);
    EXPECT_FALSE(data.local_time->UtcFallback());
    EXPECT_EQ(CountFallbackWarnings(), 0);
    auto host = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), data.local_time->Offsets());
    EXPECT_EQ(host(0, 0), -20 * 900);  // NY
    EXPECT_EQ(host(1, 0), 36 * 900);   // Tokyo
}

}  // namespace
}  // namespace cece
