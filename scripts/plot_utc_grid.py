"""Plot the UTC-offset grid (data/utc_grid_720r.rle) on an Equal Earth map.

Usage:
    python scripts/plot_utc_grid.py [GRID] [-o utc_offset_map.png] [--no-merge]

Requires numpy, matplotlib and cartopy (Natural Earth data is downloaded on first
run). Equal-offset runs within a row are merged into one polygon by default;
--no-merge draws every grid cell.
"""

import argparse
import math
import struct
import time
from itertools import pairwise
from pathlib import Path

import numpy as np

MAGIC = b"UTC1"
DEFAULT_GRID = Path(__file__).resolve().parents[1] / "data" / "utc_grid_720r.rle"
UTC_OFFSET_MIN_HOURS = -12
UTC_OFFSET_MAX_HOURS = 14


def decode_grid(path: Path) -> tuple[np.ndarray, np.ndarray]:
    blob = path.read_bytes()
    if blob[:4] != MAGIC:
        raise ValueError(f"{path} does not have the UTC1 grid header")
    if len(blob) < 6:
        raise ValueError(f"{path} has a truncated UTC1 header")

    nrows = struct.unpack_from("<H", blob, 4)[0]
    header_size = 6 + 2 * nrows
    if nrows == 0 or len(blob) < header_size:
        raise ValueError(f"{path} has an invalid or truncated row-count table")

    ncols = np.frombuffer(blob, dtype="<u2", count=nrows, offset=6).copy()
    if np.any(ncols == 0):
        raise ValueError(f"{path} contains an empty grid row")
    token_bytes = len(blob) - header_size
    if token_bytes % 3:
        raise ValueError(f"{path} has a truncated RLE token")

    tokens = np.frombuffer(
        blob,
        dtype=np.dtype([("run", "<u2"), ("offset", "i1")]),
        offset=header_size,
    )
    expected_cells = int(ncols.sum())
    if np.any(tokens["run"] == 0):
        raise ValueError(f"{path} contains a zero-length RLE run")
    if int(tokens["run"].sum()) != expected_cells:
        raise ValueError(
            f"{path} expands to {int(tokens['run'].sum())} cells; "
            f"expected {expected_cells}"
        )

    cells = np.repeat(tokens["offset"], tokens["run"])
    return ncols, cells


def densified_run(west: float, east: float, south: float, north: float) -> np.ndarray:
    segments = max(1, math.ceil((east - west) / 2.0))
    longitudes = np.linspace(west, east, segments + 1)
    return np.column_stack((
        np.concatenate((longitudes, longitudes[::-1])),
        np.concatenate((np.full(segments + 1, north), np.full(segments + 1, south))),
    ))


def build_merged_polygons(
    ncols: np.ndarray, cells: np.ndarray
) -> tuple[list[np.ndarray], np.ndarray]:
    polygons = []
    offsets = []
    lat_step = 180.0 / len(ncols)
    row_start = 0
    for row, ncol in enumerate(ncols):
        count = int(ncol)
        row_values = cells[row_start : row_start + count]
        changes = np.flatnonzero(row_values[1:] != row_values[:-1]) + 1
        boundaries = np.concatenate(([0], changes, [count]))
        lat_north = 90.0 - row * lat_step
        lat_south = lat_north - lat_step
        for start, end in pairwise(boundaries):
            west = -180.0 + start * (360.0 / count)
            east = -180.0 + end * (360.0 / count)
            polygons.append(densified_run(west, east, lat_south, lat_north))
            offsets.append(row_values[start])
        row_start += count
    return polygons, np.asarray(offsets, dtype=np.float32) / 4.0


def project_polygons(
    polygons: list[np.ndarray], source_crs, target_crs
) -> list[np.ndarray]:
    lengths = np.fromiter((len(polygon) for polygon in polygons), dtype=np.int64)
    coordinates = np.concatenate(polygons)
    projected = target_crs.transform_points(
        source_crs, coordinates[:, 0], coordinates[:, 1]
    )[:, :2]
    boundaries = np.concatenate(([0], np.cumsum(lengths)))
    return [projected[start:end] for start, end in pairwise(boundaries)]


def add_polygons(ax, PolyCollection, polygons, values, cmap, norm):
    collection = PolyCollection(
        polygons,
        array=values,
        cmap=cmap,
        norm=norm,
        edgecolors="none",
        linewidths=0,
        antialiased=False,
        transform=ax.transData,
        zorder=1,
    )
    ax.add_collection(collection)
    return collection


def add_cell_polygons(ax, ccrs, target_crs, PolyCollection, ncols, cells, cmap, norm):
    max_cells_per_collection = 50_000
    vertices = []
    offsets = []
    collections = []
    lat_step = 180.0 / len(ncols)
    row_start = 0
    buffered_cells = 0

    for row, ncol in enumerate(ncols):
        count = int(ncol)
        row_values = cells[row_start : row_start + count]
        west = -180.0 + np.arange(count) * (360.0 / count)
        east = -180.0 + (np.arange(count) + 1) * (360.0 / count)
        north = 90.0 - row * lat_step
        south = north - lat_step
        row_vertices = np.empty((count, 4, 2), dtype=np.float32)
        row_vertices[:, 0, :] = np.column_stack((west, np.full(count, north)))
        row_vertices[:, 1, :] = np.column_stack((east, np.full(count, north)))
        row_vertices[:, 2, :] = np.column_stack((east, np.full(count, south)))
        row_vertices[:, 3, :] = np.column_stack((west, np.full(count, south)))

        vertices.append(row_vertices)
        offsets.append(row_values)
        buffered_cells += count
        row_start += count

        if buffered_cells >= max_cells_per_collection or row == len(ncols) - 1:
            polygons = np.concatenate(vertices)
            projected = target_crs.transform_points(
                ccrs.PlateCarree(), polygons[:, :, 0].ravel(), polygons[:, :, 1].ravel()
            )[:, :2].reshape(polygons.shape)
            values = np.concatenate(offsets).astype(np.float32) / 4.0
            collections.append(
                add_polygons(ax, PolyCollection, projected, values, cmap, norm)
            )
            vertices.clear()
            offsets.clear()
            buffered_cells = 0

    if not collections:
        raise ValueError("Cannot plot an empty UTC grid")
    return collections[-1], len(collections)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot CECE's reduced UTC-offset grid.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "grid",
        metavar="GRID",
        nargs="?",
        type=Path,
        default=DEFAULT_GRID,
        help="Path to the UTC grid file",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("utc_offset_map.png"),
        help="Output file path",
    )
    parser.add_argument("--dpi", type=int, default=180, help="DPI")
    parser.add_argument("--cmap", default="turbo", help="UTC offset colormap")
    parser.add_argument(
        "--no-merge", action="store_true", help="Plot every grid cell separately"
    )
    parser.add_argument(
        "--show", action="store_true", help="Display the plot after creation"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    import cartopy.crs as ccrs
    import cartopy.feature as cfeature
    from matplotlib import pyplot as plt
    from matplotlib.collections import PolyCollection
    from matplotlib.colors import Normalize
    from matplotlib.ticker import FuncFormatter

    started = time.perf_counter()
    ncols, cells = decode_grid(args.grid)
    print(f"Decoded {cells.size:,} cells in {time.perf_counter() - started:.2f}s")
    offset_min = int(cells.min()) / 4.0
    offset_max = int(cells.max()) / 4.0
    print(f"Data UTC offset range: {offset_min:+g} to {offset_max:+g} hours")
    if offset_min < UTC_OFFSET_MIN_HOURS or offset_max > UTC_OFFSET_MAX_HOURS:
        print(
            f"{args.grid} has UTC offsets outside the plotted range "
            f"[{UTC_OFFSET_MIN_HOURS:+g}, {UTC_OFFSET_MAX_HOURS:+g}] hours: "
            f"{offset_min:+g} to {offset_max:+g} hours"
        )
        return 1

    norm = Normalize(vmin=UTC_OFFSET_MIN_HOURS, vmax=UTC_OFFSET_MAX_HOURS, clip=True)
    cmap = plt.get_cmap(args.cmap)
    figure = plt.figure(figsize=(14, 7.5))
    target_crs = ccrs.EqualEarth()
    ax = figure.add_subplot(1, 1, 1, projection=target_crs)
    ax.set_global()

    geometry_started = time.perf_counter()
    if args.no_merge:
        mappable, collection_count = add_cell_polygons(
            ax, ccrs, target_crs, PolyCollection, ncols, cells, cmap, norm
        )
        print(
            f"Built {cells.size:,} cell polygons in {collection_count} collections "
            f"in {time.perf_counter() - geometry_started:.2f}s"
        )
    else:
        polygons, values = build_merged_polygons(ncols, cells)
        polygons = project_polygons(polygons, ccrs.PlateCarree(), target_crs)
        mappable = add_polygons(ax, PolyCollection, polygons, values, cmap, norm)
        print(
            f"Built {len(polygons):,} merged polygons in "
            f"{time.perf_counter() - geometry_started:.2f}s"
        )

    ax.coastlines(resolution="50m", linewidth=0.9, color="#242b31", zorder=3)
    borders = cfeature.BORDERS.with_scale("50m")
    ax.add_feature(borders, linewidth=0.35, edgecolor="#38434a", zorder=3)

    colorbar = figure.colorbar(
        mappable,
        ax=ax,
        orientation="horizontal",
        pad=0.03,
        shrink=0.6,
        ticks=np.arange(UTC_OFFSET_MIN_HOURS, UTC_OFFSET_MAX_HOURS + 1, 2),
    )
    colorbar.set_label("UTC offset (hours)")
    colorbar.ax.xaxis.set_major_formatter(
        FuncFormatter(lambda value, _: "0" if value == 0 else f"{value:+.0f}")
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    draw_started = time.perf_counter()
    figure.savefig(args.output, dpi=args.dpi, bbox_inches="tight")
    print(
        f"Rendered and saved {args.output} in {time.perf_counter() - draw_started:.2f}s"
    )
    if args.show:
        plt.show()
    else:
        plt.close(figure)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
