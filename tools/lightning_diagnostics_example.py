"""Generate a synthetic lightning stream and check native CECE diagnostics."""

from __future__ import annotations

import argparse
import logging
from pathlib import Path

import numpy as np
import xarray as xr

LOGGER = logging.getLogger(__name__)
DEFAULT_INPUT = Path("data/lightning/lightning_diagnostics_input.nc")
UNITS = {
    "LIGHTNING_NO": "kg s-1 cell-1",
    "FLASH_RATE": "flashes s-1 cell-1",
    "FLASH_DENSITY": "flashes km-2 h-1",
    "NO_PER_FLASH": "mol flash-1",
    "NO_PRODUCTION": "kg s-1 cell-1",
}


def generate_input(path: Path) -> None:
    longitude = np.array([-1.5, -0.5, 0.5, 1.5])
    latitude = np.array([-0.5, 0.5])
    cloud_height = np.zeros((2, 2, 4))
    cloud_height[0] = np.array([0.0, 5000.0, 10000.0, 15000.0])
    land_mask = np.broadcast_to(np.array([1.0, 0.0])[None, :, None], (2, 2, 4)).copy()
    latitude_edges = np.deg2rad(np.array([-1.0, 0.0, 1.0]))
    row_areas = 6371000.0**2 * np.deg2rad(1.0) * np.diff(np.sin(latitude_edges))
    cell_area = np.broadcast_to(row_areas[None, :, None], (2, 2, 4)).copy()
    dataset = xr.Dataset(
        {
            "cloud_top_height": (("time", "lat", "lon"), cloud_height, {"units": "m"}),
            "land_mask": (("time", "lat", "lon"), land_mask, {"units": "1"}),
            "cell_area": (("time", "lat", "lon"), cell_area, {"units": "m2"}),
        },
        coords={
            "time": (
                "time",
                np.array([0.0, 3600.0]),
                {"units": "seconds since 2020-06-20 00:00:00", "calendar": "standard"},
            ),
            "lat": ("lat", latitude, {"units": "degrees_north"}),
            "lon": ("lon", longitude, {"units": "degrees_east"}),
        },
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    dataset.to_netcdf(path, engine="h5netcdf")
    LOGGER.info("Wrote %s (cloudy then clear; land and ocean rows)", path)


def check_outputs(input_path: Path, output_dir: Path) -> None:
    paths = sorted(output_dir.glob("lightning_*.nc"))
    if len(paths) != 2:
        raise ValueError(
            f"Expected exactly two lightning output files in {output_dir}, found {len(paths)}"
        )
    with xr.open_dataset(input_path, engine="h5netcdf", decode_times=False) as source:
        for step, path in enumerate(paths):
            height = source.cloud_top_height.isel(time=step).values
            area = source.cell_area.isel(time=step).values
            mask = source.land_mask.isel(time=step).values
            expected_rate = 3.44e-5 * np.maximum(height / 1000.0, 0.0) ** 4.9 / 60.0
            expected_yield = np.where(mask > 0.5, 3.011e26, 1.566e26) / 6.022e23
            expected_production = expected_rate * expected_yield * 0.030
            expected_efficiency = np.where(expected_rate > 0.0, expected_yield, np.nan)
            with xr.open_dataset(path, engine="h5netcdf", decode_times=False) as output:
                np.testing.assert_allclose(output.lon.values, source.lon.values)
                np.testing.assert_allclose(output.lat.values, source.lat.values)
                np.testing.assert_allclose(output.time.values, (step + 1) * 3600.0)
                values = {}
                for name, units in UNITS.items():
                    if output[name].attrs.get("units") != units:
                        raise ValueError(f"{path}: {name} must have units {units!r}")
                    values[name] = output[name].values.reshape(3, 2, 4)
                expected = {
                    "FLASH_RATE": expected_rate,
                    "FLASH_DENSITY": expected_rate * 3600.0 * 1.0e6 / area,
                    "NO_PER_FLASH": expected_efficiency,
                    "NO_PRODUCTION": expected_production,
                }
                for name, reference in expected.items():
                    np.testing.assert_allclose(
                        values[name][0],
                        reference,
                        rtol=1.0e-10,
                        atol=1.0e-15,
                        equal_nan=True,
                    )
                    if name == "NO_PER_FLASH":
                        if not np.isnan(values[name][1:]).all():
                            raise ValueError(
                                f"{path}: efficiency must be NaN above level 0"
                            )
                    else:
                        np.testing.assert_array_equal(values[name][1:], 0.0)
                np.testing.assert_allclose(
                    values["LIGHTNING_NO"],
                    np.broadcast_to(expected_production / 3.0, (3, 2, 4)),
                    rtol=1.0e-10,
                    atol=1.0e-15,
                )
                np.testing.assert_allclose(
                    values["LIGHTNING_NO"].sum(axis=0),
                    expected_production,
                    rtol=1.0e-10,
                    atol=1.0e-15,
                )
                active = expected_rate > 0.0
                np.testing.assert_allclose(
                    values["LIGHTNING_NO"].sum(axis=0)[active]
                    / (0.030 * values["FLASH_RATE"][0][active]),
                    values["NO_PER_FLASH"][0][active],
                    rtol=1.0e-10,
                )
                LOGGER.info(
                    "PASS %s: rates, density, mol/flash, vertical conservation, and units",
                    path,
                )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("generate", "check"))
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument(
        "--output-dir", type=Path, default=Path("cece_output/lightning")
    )
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    if args.action == "generate":
        generate_input(args.input)
    else:
        check_outputs(args.input, args.output_dir)


if __name__ == "__main__":
    main()
