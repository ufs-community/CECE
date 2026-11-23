# SPDX-License-Identifier: Apache-2.0
"""Fixture builder and checker for test_stream_local_weekday.sh.

make-input DIR      write DIR/weekly.nc, a 7-record weekly file where record k (0=Monday) is k + 1.
check OUT_DIR MODE  verify the driver output; MODE is "local" (local-weekday records) or "utc".
"""

import glob
import sys

import numpy as np
from netCDF4 import Dataset

NX, NY = 72, 36

# (name, lat, lon, standard-time UTC offset in hours)
POINTS = [
    ("US Pacific (-8)", 37.0, -120.0, -8.0),
    ("London (0)", 51.0, 0.0, 0.0),
    ("Moscow (+3)", 55.0, 37.5, 3.0),
    ("India (+5:30)", 27.5, 77.5, 5.5),
    ("Tokyo (+9)", 35.0, 140.0, 9.0),
    ("Auckland (+12)", -37.5, 175.0, 12.0),
]


def make_input(directory):
    with Dataset(f"{directory}/weekly.nc", "w", format="NETCDF4_CLASSIC") as ds:
        ds.createDimension("time", 7)
        ds.createDimension("lat", NY)
        ds.createDimension("lon", NX)
        lat = ds.createVariable("lat", "f8", ("lat",))
        lon = ds.createVariable("lon", "f8", ("lon",))
        lat.units = "degrees_north"
        lon.units = "degrees_east"
        lat[:] = -90.0 + (np.arange(NY) + 0.5) * 180.0 / NY
        lon[:] = -180.0 + (np.arange(NX) + 0.5) * 360.0 / NX
        fw = ds.createVariable("FW", "f8", ("time", "lat", "lon"))
        for k in range(7):
            fw[k, :, :] = k + 1.0


def check(out_dir, mode):
    files = sorted(glob.glob(f"{out_dir}/wk_*.nc"))
    if not files:
        print(f"no output files in {out_dir}")
        return 1
    failures = 0
    for path in files:
        # File labels are the end of the step, so the data was computed one hour earlier.
        hour = int(path.split("_")[-1][:2]) - 1
        with Dataset(path) as ds:
            lat, lon = ds["lat"][:], ds["lon"][:]
            field = np.squeeze(ds["wk"][:])
        for name, plat, plon, offset in POINTS:
            value = field[
                int(np.argmin(np.abs(lat - plat))), int(np.argmin(np.abs(lon - plon)))
            ]
            # Monday record 0 -> 1.0; Tuesday record 1 -> 2.0 once the local clock passes midnight.
            want = (2.0 if hour + offset >= 24 else 1.0) if mode == "local" else 1.0
            # NaN and masked fill values compare False against any tolerance, so reject them first.
            if (
                np.ma.is_masked(value)
                or not np.isfinite(value)
                or abs(value - want) > 1e-6
            ):
                failures += 1
                print(f"MISMATCH {path} {name}: got {value}, expected {want}")
    return 1 if failures else 0


if __name__ == "__main__":
    if sys.argv[1] == "make-input":
        make_input(sys.argv[2])
    else:
        sys.exit(check(sys.argv[2], sys.argv[3]))
