#!/usr/bin/env python3
"""Generate the cosine-reduced UTC-offset grid consumed by CECE local-time support.

Output format (v2, self-describing), little-endian:

    offset  size  field
    0       4     magic  b"UTC1"
    4       2     nrows  uint16 (= 1440)
    6       2*N   ncol[row] uint16 * N       (per-row column counts)
    ...     ...   run-length tokens: { uint16 run_length; int8 offset_15min }

The grid keeps 1440 uniform latitude rows (0.125 deg, centers
``lat = 90 - (row + 0.5) * 0.125``) and tapers columns toward the poles so each
row holds ``ncol(row) = max(4, 4 * round(720 * cos(lat)))`` points. That yields
~2.64M cells (36% fewer than the old 1440x2880 regular raster) at a constant
~14 km ground resolution -- timezone-offset data does not need 0.125 deg
resolution near the poles.

Two modes:

  --rebin SOURCE.rle     Resample an existing grid (v1 headerless 1440x2880 or
                         v2 self-describing) onto the reduced lattice by
                         nearest-cell lookup. Pure stdlib, deterministic, and
                         byte-consistent with the source data. This produces the
                         shipped file.

  --from-timezones       Rasterize the globe with ``timezonefinder`` at
                         --ref-date, then emit the reduced layout. Needs the
                         third-party dep; imported lazily so --rebin never does.

Both write the same header + RLE stream. See
specs/001-local-time-support/design-reduced-grid.md for the design rationale.
"""

import argparse
import math
import struct
from datetime import datetime, timezone

# ---------------------------------------------------------------------------
# Grid geometry (shared by both modes and by the C++ decoder contract)
# ---------------------------------------------------------------------------
NROWS = 1440  # 180 / 0.125
STEP = 0.125  # degrees; ~14 km at the equator
FULL_NCOL = 2880  # equatorial / v1 column count
MAGIC = b"UTC1"


def row_center_lat(row: int) -> float:
    """Cell-center latitude of a row (North -> South)."""
    return 90.0 - (row + 0.5) * STEP


def reduced_ncol(row: int) -> int:
    """Cosine-tapered column count for a row: constant ground resolution.

    ncol = 4 * round(720 * cos(lat)); 2880 at the equator (matching v1),
    tapering to 4 at the poles. 720 = FULL_NCOL / 4 keeps the equatorial row
    at the full 2880 columns.
    """
    return max(
        4, 4 * round((FULL_NCOL // 4) * math.cos(math.radians(row_center_lat(row))))
    )


def reduced_ncols() -> list:
    return [reduced_ncol(r) for r in range(NROWS)]


# ---------------------------------------------------------------------------
# Source decoding: accept v1 (headerless full raster) or v2 (self-describing)
# ---------------------------------------------------------------------------
def _expand_rle(blob: bytes, offset: int, total_cells: int) -> list:
    """Expand RLE tokens starting at ``offset`` into ``total_cells`` int8 values."""
    out = bytearray()
    n = len(blob)
    while offset + 3 <= n and len(out) < total_cells:
        run = struct.unpack_from("<H", blob, offset)[0]
        off = struct.unpack_from("<b", blob, offset + 2)[0]
        offset += 3
        out.extend(bytes([off & 0xFF]) * run)
    if len(out) != total_cells:
        raise ValueError(
            f"RLE stream expanded to {len(out)} cells, expected {total_cells}"
        )
    return [struct.unpack("<b", bytes([b]))[0] for b in out]


def decode_grid(path: str):
    """Return (ncols_per_row, flat int8 cells) for a v1 or v2 file."""
    blob = open(path, "rb").read()
    if blob[:4] == MAGIC:
        nrows = struct.unpack_from("<H", blob, 4)[0]
        ncols = list(struct.unpack_from(f"<{nrows}H", blob, 6))
        cells = _expand_rle(blob, 6 + 2 * nrows, sum(ncols))
        return ncols, cells
    # v1: headerless full 1440x2880 regular raster.
    if len(blob) % 3 != 0:
        raise ValueError(f"v1 RLE file size {len(blob)} is not a multiple of 3")
    cells = _expand_rle(blob, 0, NROWS * FULL_NCOL)
    return [FULL_NCOL] * NROWS, cells


# ---------------------------------------------------------------------------
# Rebin: nearest-cell resample of a decoded source onto the reduced lattice
# ---------------------------------------------------------------------------
def rebin(src_ncols, src_cells) -> bytearray:
    """For each reduced cell, take the source cell containing its center.

    Rows are unchanged (both grids have NROWS uniform rows with identical
    centers), so the source row is exact; only the column index is re-derived.
    This matches the C++ lookup rule, so file and lookup agree by construction.
    """
    out = bytearray()
    src_row_off = 0
    for row in range(NROWS):
        snc = src_ncols[row]
        dnc = reduced_ncol(row)
        for col in range(dnc):
            # Reduced-cell center longitude.
            lon = -180.0 + (col + 0.5) * (360.0 / dnc)
            # Source column containing that center (floor, clamped).
            scol = int(math.floor((lon + 180.0) / 360.0 * snc))
            scol = max(0, min(snc - 1, scol))
            out.append(src_cells[src_row_off + scol] & 0xFF)
        src_row_off += snc
    return out


# ---------------------------------------------------------------------------
# Timezone rasterization (lazy import; only --from-timezones needs it)
# ---------------------------------------------------------------------------
def rasterize_from_timezones(ref_date: datetime) -> bytearray:
    from timezonefinder import TimezoneFinder  # lazy: --rebin needs no third-party deps
    import zoneinfo

    tf = TimezoneFinder(in_memory=True)

    def offset_units(lat: float, lon: float) -> int:
        tz_name = tf.timezone_at(lat=lat, lng=lon)
        if not tz_name:
            return 0  # ocean / unresolved -> UTC
        try:
            tz = zoneinfo.ZoneInfo(tz_name)
            secs = ref_date.astimezone(tz).utcoffset().total_seconds()
            return max(-128, min(127, int(round(secs / 900.0))))
        except Exception:
            return 0

    out = bytearray()
    for row in range(NROWS):
        lat = row_center_lat(row)
        dnc = reduced_ncol(row)
        for col in range(dnc):
            lon = -180.0 + (col + 0.5) * (360.0 / dnc)
            out.append(offset_units(lat, lon) & 0xFF)
        if (row + 1) % 360 == 0:
            print(f"Progress: {((row + 1) / NROWS) * 100:.1f}%")
    return out


# ---------------------------------------------------------------------------
# Writer: header + RLE (shared by both modes)
# ---------------------------------------------------------------------------
def write_v2(cells, path: str) -> int:
    ncols = reduced_ncols()
    compressed = bytearray()
    compressed += MAGIC
    compressed += struct.pack("<H", NROWS)
    compressed += struct.pack(f"<{NROWS}H", *ncols)

    cur = struct.unpack("<b", bytes([cells[0]]))[0]
    run = 0
    for b in cells:
        v = struct.unpack("<b", bytes([b]))[0]
        if v == cur and run < 65535:
            run += 1
        else:
            compressed += struct.pack("<Hb", run, cur)
            cur = v
            run = 1
    compressed += struct.pack("<Hb", run, cur)

    with open(path, "wb") as f:
        f.write(compressed)
    return len(compressed)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--rebin", metavar="SOURCE", help="resample an existing v1/v2 grid file"
    )
    mode.add_argument(
        "--from-timezones", action="store_true", help="rasterize with timezonefinder"
    )
    ap.add_argument("-o", "--output", default="utc_grid_f720r.rle", help="output path")
    ap.add_argument(
        "--ref-date",
        default="2026-01-15T12:00:00",
        help="timezone snapshot instant (UTC)",
    )
    args = ap.parse_args()

    ncols = reduced_ncols()
    total = sum(ncols)
    print(
        f"Reduced lattice: {NROWS} rows, {total:,} cells "
        f"(equator {ncols[NROWS // 2 - 1]} cols, pole {ncols[0]} cols)"
    )

    if args.rebin:
        src_ncols, src_cells = decode_grid(args.rebin)
        print(
            f"Rebinning {args.rebin} ({sum(src_ncols):,} source cells) -> reduced lattice"
        )
        cells = rebin(src_ncols, src_cells)
    else:
        ref = datetime.fromisoformat(args.ref_date).replace(tzinfo=timezone.utc)
        print(f"Rasterizing globe from timezones at {ref.isoformat()} ...")
        cells = rasterize_from_timezones(ref)

    size = write_v2(cells, args.output)
    print(f"Success! Saved to {args.output} ({size / 1024:.2f} KB, {total:,} cells)")


if __name__ == "__main__":
    main()
