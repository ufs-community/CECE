// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Feature 001 (local-time support) — decoder tests for the cosine-reduced UTC-offset grid.
//
// Decodes the real data/utc_grid_f720r.rle and checks: the reduced-lattice
// invariants (row widths, total cell count, taper monotonicity), the (lat,lon)
// probe offsets against the generator's own expansion, the all-or-nothing
// guarantee (FR-003, FR-008), and the nearest-cell mapping (FR-004).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "cece/utc_grid.hpp"

#ifndef CECE_SOURCE_DIR
#define CECE_SOURCE_DIR "."
#endif

namespace cece {
namespace {

std::string GridPath() {
    return std::string(CECE_SOURCE_DIR) + "/data/utc_grid_f720r.rle";
}

/// Reference offsets (quarter-hours) at named probe points, verified against
/// the rebinned file produced by scripts/python/utcoffset_generator.py.
struct Probe {
    double lat;
    double lon;
    int quarter_hours;
};

TEST(UtcGridDecode, ReducedLatticeInvariants) {
    const auto grid = DecodeUtcGridRle(GridPath());
    EXPECT_EQ(grid.nrows, kUtcGridRows);
    ASSERT_EQ(static_cast<int>(grid.ncol.size()), kUtcGridRows);
    ASSERT_EQ(grid.row_offset.size(), static_cast<std::size_t>(kUtcGridRows) + 1);
    // Equator keeps the full 2880 columns; the polar rows hit the floor of 4.
    EXPECT_EQ(grid.ncol[kUtcGridRows / 2 - 1], kUtcGridEquatorCols);
    EXPECT_EQ(grid.ncol[0], kUtcGridMinCols);
    EXPECT_EQ(grid.ncol[kUtcGridRows - 1], kUtcGridMinCols);
    // Total cells = 2,640,216 (the design target, ~36% below the old 4,147,200).
    EXPECT_EQ(grid.cells.size(), 2640216u);
    EXPECT_EQ(grid.row_offset[kUtcGridRows], grid.cells.size());
    // Taper is monotone non-increasing from the equator toward each pole.
    for (int r = kUtcGridRows / 2; r > 0; --r) {
        EXPECT_LE(grid.ncol[r - 1], grid.ncol[r]) << "row " << r;
    }
    for (int r = kUtcGridRows / 2; r < kUtcGridRows - 1; ++r) {
        EXPECT_GE(grid.ncol[r], grid.ncol[r + 1]) << "row " << r;
    }
    // Observed range of the shipped snapshot: -12h .. +14h in quarter-hours.
    int lo = 127, hi = -128;
    for (const auto v : grid.cells) {
        lo = std::min(lo, static_cast<int>(v));
        hi = std::max(hi, static_cast<int>(v));
    }
    EXPECT_EQ(lo, -48);
    EXPECT_EQ(hi, 56);
}

TEST(UtcGridDecode, ProbeCitiesMatchReference) {
    const auto grid = DecodeUtcGridRle(GridPath());
    // City probes keep their v1 values: each sits far from a timezone border
    // relative to even the widened cell. The two pole/date-line corner probes
    // change under the taper (row 0 has 4 columns of 90 deg each) and are
    // pinned to the rebinned file's values.
    const std::vector<Probe> probes = {
        {40.0, -74.0, -20},   // New York  (UTC-5)
        {51.5, -0.125, 0},    // London    (UTC+0)
        {28.6, 77.2, 22},     // Delhi     (UTC+5.5)
        {35.68, 139.7, 36},   // Tokyo     (UTC+9)
        {0.0, 0.0, 0},        // Atlantic (0,0)
        {-33.86, 151.2, 44},  // Sydney    (UTC+11)
        {52.5, 13.4, 4},      // Berlin    (UTC+1)
        {90.0, -180.0, -36},  // N-pole / date-line corner (tapered: was -48)
        {-45.0, 179.99, 48},  // far south, near date line
    };
    for (const auto& p : probes) {
        const int row = UtcGridRowForLat(p.lat);
        const int col = UtcGridColForLon(p.lon, static_cast<int>(grid.ncol[row]));
        const int got = static_cast<int>(grid.cells[grid.index(row, col)]);
        EXPECT_EQ(got, p.quarter_hours) << "probe lat=" << p.lat << " lon=" << p.lon;
    }
}

TEST(UtcGridMapping, RowColFloorAndClamp) {
    // Cell-center floor mapping: row = floor((90 - lat)/0.125), clamped.
    EXPECT_EQ(UtcGridRowForLat(90.0), 0);
    EXPECT_EQ(UtcGridRowForLat(89.9375), 0);  // center of row 0
    EXPECT_EQ(UtcGridRowForLat(89.9), 0);
    EXPECT_EQ(UtcGridRowForLat(-90.0), kUtcGridRows - 1);
    EXPECT_EQ(UtcGridRowForLat(0.0), kUtcGridRows / 2);  // 720
    // Column depends on the row width: at the equator (2880 cols) it matches the
    // old regular grid; on a tapered row the same longitude lands in a coarser bin.
    EXPECT_EQ(UtcGridColForLon(-180.0, kUtcGridEquatorCols), 0);
    EXPECT_EQ(UtcGridColForLon(180.0, kUtcGridEquatorCols), 0);                      // wraps to -180
    EXPECT_EQ(UtcGridColForLon(0.0, kUtcGridEquatorCols), kUtcGridEquatorCols / 2);  // 1440
    EXPECT_EQ(UtcGridColForLon(179.99, kUtcGridEquatorCols), kUtcGridEquatorCols - 1);
    EXPECT_EQ(UtcGridColForLon(190.0, 100), UtcGridColForLon(-170.0, 100));  // modulo wrap
    // A 4-column polar row: each column spans 90 deg.
    EXPECT_EQ(UtcGridColForLon(-180.0, 4), 0);
    EXPECT_EQ(UtcGridColForLon(-90.0, 4), 1);
    EXPECT_EQ(UtcGridColForLon(0.0, 4), 2);
    EXPECT_EQ(UtcGridColForLon(90.0, 4), 3);
    EXPECT_EQ(UtcGridColForLon(179.99, 4), 3);
}

TEST(UtcGridMapping, MapToNativeProducesSecondsBandLocal) {
    const auto grid = DecodeUtcGridRle(GridPath());
    // Two columns at NY and Tokyo longitudes, single equator row, whole band.
    const std::vector<double> lons = {-74.0, 139.7};
    const std::vector<double> lats = {40.0};
    const auto secs = MapToNativeOffsetsSec(grid, lons, lats, /*nx=*/2, /*j0=*/0, /*ny_local=*/1);
    ASSERT_EQ(secs.size(), 2u);
    EXPECT_EQ(secs[0], -20 * kUtcOffsetQuarterHoursPerSecond);  // NY -5h
    EXPECT_EQ(secs[1], 36 * kUtcOffsetQuarterHoursPerSecond);   // Tokyo +9h
    // Output layout is out[i + j*nx]; band-local j offset by j0.
    const std::vector<double> lats6 = {89.0, 88.0, 87.0, 86.0, 85.0, 40.0};
    const auto band = MapToNativeOffsetsSec(grid, lons, lats6, /*nx=*/2, /*j0=*/5, /*ny_local=*/1);
    ASSERT_EQ(band.size(), 2u);
    EXPECT_EQ(band[0], secs[0]);
    EXPECT_EQ(band[1], secs[1]);
}

TEST(UtcGridDecode, TruncatedFileThrows) {
    // Keep the header but drop most of the token stream: the expanded cell count
    // then falls short of sum(ncol) and must be rejected wholesale (never a
    // partial grid masking 0s, FR-003).
    std::ifstream in(GridPath(), std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in));
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 4000u);  // header (2886 B) + tokens
    const std::string tmp = "/tmp/cece_utc_grid_truncated.rle";
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 2));
    }
    EXPECT_THROW(DecodeUtcGridRle(tmp), std::runtime_error);
    std::remove(tmp.c_str());
}

TEST(UtcGridDecode, BadMagicThrows) {
    std::ifstream in(GridPath(), std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in));
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 10u);
    bytes[0] = static_cast<char>('X');  // corrupt the magic
    const std::string tmp = "/tmp/cece_utc_grid_badmagic.rle";
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    EXPECT_THROW(DecodeUtcGridRle(tmp), std::runtime_error);
    std::remove(tmp.c_str());
}

TEST(UtcGridDecode, MissingFileThrows) {
    EXPECT_THROW(DecodeUtcGridRle("/tmp/cece_does_not_exist_" + std::to_string(__LINE__) + ".rle"), std::runtime_error);
}

}  // namespace
}  // namespace cece
