/**
 * @file cece_utc_grid.cpp
 * @brief Decoder + native-grid mapper for the cosine-reduced UTC-offset grid.
 *
 * Implements the file-format contract in
 * specs/001-local-time-support/contracts/utc-grid-file.md: a self-describing
 * v2 file (magic + uint16 nrows + per-row uint16 column counts) followed by a
 * headerless run-length stream of little-endian 3-byte tokens
 * (uint16 run, int8 quarter-hour offset), expanded all-or-nothing to a dense
 * reduced lattice (1440 rows, ncol(row) = max(4, 4*round(720*cos lat))), plus
 * the exact inverse of the generator's cell-center floor mapping (research D1)
 * onto the native simulation grid.
 *
 * Deliberately free of Kokkos / config / MPI dependencies so the decoder is
 * unit-testable in isolation.
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "cece/utc_grid.hpp"

namespace cece {
namespace {

/// Read exactly n bytes into buf; returns false on short read.
bool ReadExact(std::FILE* f, void* buf, std::size_t n) {
    return std::fread(buf, 1, n, f) == n;
}

std::uint16_t ReadU16Le(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

}  // namespace

UtcGrid DecodeUtcGridRle(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("utc_grid: cannot open grid file '" + path + "'");
    }

    // --- header: magic (4) + nrows (2) + ncol[nrows] (2*nrows) ---
    unsigned char magic[4];
    std::uint16_t nrows_u16 = 0;
    bool ok = ReadExact(f, magic, 4) && ReadExact(f, reinterpret_cast<unsigned char*>(&nrows_u16), 2);
    if (ok) {
        const std::uint32_t got = ReadU16Le(magic) | (static_cast<std::uint32_t>(ReadU16Le(magic + 2)) << 16);
        if (got != kUtcGridMagic) {
            ok = false;
        }
    }
    if (!ok) {
        std::fclose(f);
        throw std::runtime_error("utc_grid: bad magic or short header in '" + path + "'");
    }
    if (nrows_u16 == 0) {
        std::fclose(f);
        throw std::runtime_error("utc_grid: zero-row grid in '" + path + "'");
    }

    UtcGrid grid;
    grid.nrows = static_cast<int>(nrows_u16);
    grid.ncol.resize(grid.nrows);
    for (int r = 0; r < grid.nrows; ++r) {
        unsigned char le[2];
        if (!ReadExact(f, le, 2)) {
            std::fclose(f);
            throw std::runtime_error("utc_grid: truncated ncol table in '" + path + "'");
        }
        grid.ncol[static_cast<std::size_t>(r)] = ReadU16Le(le);
    }

    // Row offsets (prefix sums) and the expected dense cell count.
    grid.row_offset.resize(static_cast<std::size_t>(grid.nrows) + 1, 0);
    for (int r = 0; r < grid.nrows; ++r) {
        grid.row_offset[static_cast<std::size_t>(r) + 1] = grid.row_offset[static_cast<std::size_t>(r)] + grid.ncol[static_cast<std::size_t>(r)];
    }
    const std::size_t total = grid.row_offset[static_cast<std::size_t>(grid.nrows)];
    grid.cells.reserve(total);

    // --- RLE token stream: (uint16 run, int8 offset), row-major N->S / W->E ---
    unsigned char token[3];
    while (grid.cells.size() < total) {
        if (std::fread(token, 1, 3, f) != 3) {
            std::fclose(f);
            throw std::runtime_error("utc_grid: RLE stream in '" + path + "' ended after " + std::to_string(grid.cells.size()) + " of " +
                                     std::to_string(total) + " cells (corrupt)");
        }
        const std::uint16_t run_length = ReadU16Le(token);
        const std::int8_t offset = static_cast<std::int8_t>(token[2]);
        if (run_length == 0) {
            std::fclose(f);
            throw std::runtime_error("utc_grid: zero-length run in '" + path + "' (corrupt)");
        }
        if (grid.cells.size() + run_length > total) {
            std::fclose(f);
            throw std::runtime_error("utc_grid: RLE stream in '" + path + "' expands beyond " + std::to_string(total) + " cells (corrupt)");
        }
        grid.cells.resize(grid.cells.size() + run_length, offset);
    }
    // Exactly filled; reject any trailing bytes (concatenated or corrupt tail).
    unsigned char extra = 0;
    const bool no_trailing = std::fread(&extra, 1, 1, f) == 0;
    std::fclose(f);
    if (!no_trailing) {
        throw std::runtime_error("utc_grid: trailing data after " + std::to_string(total) + " cells in '" + path + "' (corrupt)");
    }
    return grid;
}

int UtcGridRowForLat(double lat) {
    // Cell-center inverse of lat = 90 - (row + 0.5) * step (generator convention).
    double row = std::floor((90.0 - lat) / kUtcGridStepDeg);
    if (row < 0.0) row = 0.0;
    if (row > kUtcGridRows - 1) row = kUtcGridRows - 1;
    return static_cast<int>(row);
}

int UtcGridColForLon(double lon, int row_ncol) {
    // Wrap to [-180, 180) first so 0..360 conventions and exact 180 land correctly.
    double wrapped = std::fmod(lon + 180.0, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    const double step = 360.0 / static_cast<double>(row_ncol);
    double col = std::floor(wrapped / step);
    if (col < 0.0) col = 0.0;
    if (col > row_ncol - 1) col = row_ncol - 1;
    return static_cast<int>(col);
}

std::vector<std::int32_t> MapToNativeOffsetsSec(const UtcGrid& grid, const std::vector<double>& native_lons, const std::vector<double>& native_lats,
                                                int nx, int j0, int ny_local) {
    if (grid.cells.empty() || grid.nrows <= 0) {
        throw std::invalid_argument("utc_grid: MapToNativeOffsetsSec expects a decoded grid, got an empty one");
    }
    if (nx <= 0 || ny_local < 0 || native_lons.size() != static_cast<std::size_t>(nx)) {
        throw std::invalid_argument("utc_grid: MapToNativeOffsetsSec lon array size mismatch (nx=" + std::to_string(nx) +
                                    ", lons=" + std::to_string(native_lons.size()) + ")");
    }
    if (static_cast<std::size_t>(j0) + static_cast<std::size_t>(ny_local) > native_lats.size()) {
        throw std::invalid_argument("utc_grid: MapToNativeOffsetsSec band [" + std::to_string(j0) + ", " + std::to_string(j0 + ny_local) +
                                    ") exceeds native_lats size " + std::to_string(native_lats.size()));
    }

    std::vector<std::int32_t> out(static_cast<std::size_t>(nx) * ny_local, 0);
    for (int j = 0; j < ny_local; ++j) {
        const int row = UtcGridRowForLat(native_lats[static_cast<std::size_t>(j0) + static_cast<std::size_t>(j)]);
        const int row_ncol = grid.ncol[static_cast<std::size_t>(row)];
        for (int i = 0; i < nx; ++i) {
            const int col = UtcGridColForLon(native_lons[static_cast<std::size_t>(i)], row_ncol);
            out[static_cast<std::size_t>(j) * nx + i] = static_cast<std::int32_t>(grid.cells[grid.index(row, col)]) * kUtcOffsetQuarterHoursPerSecond;
        }
    }
    return out;
}

}  // namespace cece
