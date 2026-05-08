#!/usr/bin/env python3
"""
cece_make_gridspec.py — Generate an ESMF GRIDSPEC NetCDF file from a CECE YAML config.

Usage:
    cece_make_gridspec.py <cece_config.yaml> [output.nc]

If no output filename is given, one is constructed from the grid parameters:
    cece_grid_nx{nx}_ny{ny}_nz{nz}_lon{lon_min}_{lon_max}_lat{lat_min}_{lat_max}.nc

The resulting file can be used as driver.gridspec_file in the YAML to skip
grid construction at runtime:

    driver:
      gridspec_file: cece_grid_nx3600_ny1801_nz1_lonN180_180_latN90_90.nc
      grid:          # still used as fallback and for TIDE mesh
        nx: 3600
        ...
"""

import sys
from pathlib import Path

import numpy as np
import yaml

try:
    from netCDF4 import Dataset
except ImportError:
    sys.exit("ERROR: netCDF4 is required. Install with: pip install netCDF4")


def _encode_coord(val: float) -> str:
    """Encode a float as a compact filename-safe string (no dot, 'N' prefix for negative)."""
    prefix = "N" if val < 0 else ""
    s = f"{abs(val):g}"  # e.g. "180", "67.5", "177.5"
    s = s.replace(".", "p")  # "67p5"
    return prefix + s


def build_output_name(nx, ny, nz, lon_min, lon_max, lat_min, lat_max) -> str:
    return (
        f"cece_grid"
        f"_nx{nx}_ny{ny}_nz{nz}"
        f"_lon{_encode_coord(lon_min)}_{_encode_coord(lon_max)}"
        f"_lat{_encode_coord(lat_min)}_{_encode_coord(lat_max)}"
        f".nc"
    )


def read_grid_params(yaml_path: str) -> dict:
    with open(yaml_path) as f:
        cfg = yaml.safe_load(f)

    driver = cfg.get("driver", {})
    grid = driver.get("grid", {})

    return {
        "nx": int(grid.get("nx", 4)),
        "ny": int(grid.get("ny", 4)),
        "nz": int(grid.get("nz", 1)),
        "lon_min": float(grid.get("lon_min", -135.0)),
        "lon_max": float(grid.get("lon_max", 135.0)),
        "lat_min": float(grid.get("lat_min", -67.5)),
        "lat_max": float(grid.get("lat_max", 67.5)),
    }


def write_gridspec(params: dict, output_path: str) -> None:
    nx = params["nx"]
    ny = params["ny"]
    lon_min = params["lon_min"]
    lon_max = params["lon_max"]
    lat_min = params["lat_min"]
    lat_max = params["lat_max"]

    # Cell-center coordinates
    lon = np.linspace(lon_min, lon_max, nx, endpoint=False) + (lon_max - lon_min) / (
        2 * nx
    )
    lat = np.linspace(lat_min, lat_max, ny, endpoint=False) + (lat_max - lat_min) / (
        2 * ny
    )

    # Cell bounds (corners)
    dlon = (lon_max - lon_min) / nx
    dlat = (lat_max - lat_min) / ny
    lon_bnds = np.column_stack([lon - dlon / 2, lon + dlon / 2])
    lat_bnds = np.column_stack([lat - dlat / 2, lat + dlat / 2])

    with Dataset(output_path, "w", format="NETCDF4") as ds:
        # Global attributes required by ESMF GRIDSPEC reader
        ds.title = "CECE structured lat-lon grid"
        ds.Conventions = "CF-1.8"
        ds.grid_type = "regular"

        # Dimensions
        ds.createDimension("lon", nx)
        ds.createDimension("lat", ny)
        ds.createDimension("nv", 2)  # bounds vertices

        # Longitude
        v_lon = ds.createVariable("lon", "f8", ("lon",))
        v_lon[:] = lon
        v_lon.units = "degrees_east"
        v_lon.long_name = "longitude"
        v_lon.axis = "X"
        v_lon.bounds = "lon_bnds"

        v_lon_bnds = ds.createVariable("lon_bnds", "f8", ("lon", "nv"))
        v_lon_bnds[:] = lon_bnds

        # Latitude
        v_lat = ds.createVariable("lat", "f8", ("lat",))
        v_lat[:] = lat
        v_lat.units = "degrees_north"
        v_lat.long_name = "latitude"
        v_lat.axis = "Y"
        v_lat.bounds = "lat_bnds"

        v_lat_bnds = ds.createVariable("lat_bnds", "f8", ("lat", "nv"))
        v_lat_bnds[:] = lat_bnds

    print("INFO: Grid parameters:")
    print(f"  nx={nx}, ny={ny}, nz={params['nz']}")
    print(f"  lon: {lon_min} to {lon_max}  (cell width {dlon:.4f} deg)")
    print(f"  lat: {lat_min} to {lat_max}  (cell width {dlat:.4f} deg)")
    print(f"SUCCESS: GRIDSPEC file written to {output_path}")
    print("  Add to your YAML:")
    print("    driver:")
    print(f"      gridspec_file: {output_path}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    yaml_path = sys.argv[1]
    if not Path(yaml_path).exists():
        sys.exit(f"ERROR: File not found: {yaml_path}")

    params = read_grid_params(yaml_path)

    output_path = sys.argv[2] if len(sys.argv) >= 3 else build_output_name(**params)

    write_gridspec(params, output_path)


if __name__ == "__main__":
    main()
