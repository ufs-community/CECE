"""Tests for the synthetic native-lightning diagnostic workflow."""

import sys
from pathlib import Path

import numpy as np
import pytest
import xarray as xr

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from lightning_diagnostics_example import (
    UNITS,
    check_outputs,
    generate_input,
)


def write_reference_outputs(input_path, output_dir):
    output_dir.mkdir()
    with xr.open_dataset(input_path, engine="h5netcdf", decode_times=False) as source:
        for step in range(2):
            height = source.cloud_top_height.values[step]
            rate = 3.44e-5 * (height / 1000.0) ** 4.9 / 60.0
            yield_mol = np.where(
                source.land_mask.values[step] > 0.5, 500.0, 1.566e26 / 6.022e23
            )
            production = rate * yield_mol * 0.030
            column_values = {
                "FLASH_RATE": rate,
                "FLASH_DENSITY": rate
                * 3600.0
                / (source.cell_area.values[step] / 1.0e6),
                "NO_PER_FLASH": np.where(rate > 0.0, yield_mol, np.nan),
                "NO_PRODUCTION": production,
            }
            variables = {}
            for name, units in UNITS.items():
                if name == "LIGHTNING_NO":
                    values = np.broadcast_to(production / 3.0, (3, 2, 4)).copy()
                else:
                    values = np.full(
                        (3, 2, 4), np.nan if name == "NO_PER_FLASH" else 0.0
                    )
                    values[0] = column_values[name]
                variables[name] = (("lev", "lat", "lon"), values, {"units": units})
            output = xr.Dataset(
                variables,
                coords={
                    "lon": source.lon.values,
                    "lat": source.lat.values,
                    "lev": [0, 1, 2],
                    "time": (step + 1) * 3600.0,
                },
            )
            output.to_netcdf(output_dir / f"lightning_{step}.nc", engine="h5netcdf")


def test_input_and_reference_outputs(tmp_path):
    input_path = tmp_path / "input.nc"
    output_dir = tmp_path / "output"
    generate_input(input_path)
    with xr.open_dataset(input_path, engine="h5netcdf") as source:
        assert dict(source.sizes) == {"time": 2, "lat": 2, "lon": 4}
        assert (source.cell_area.values > 0).all()
        assert (source.cloud_top_height.values[1] == 0).all()
    write_reference_outputs(input_path, output_dir)
    check_outputs(input_path, output_dir)


@pytest.mark.parametrize(
    "field", ["FLASH_RATE", "FLASH_DENSITY", "NO_PER_FLASH", "LIGHTNING_NO"]
)
def test_checker_rejects_incorrect_diagnostics(tmp_path, field):
    input_path = tmp_path / "input.nc"
    output_dir = tmp_path / "output"
    generate_input(input_path)
    write_reference_outputs(input_path, output_dir)
    path = output_dir / "lightning_0.nc"
    with xr.open_dataset(path, engine="h5netcdf", decode_times=False) as source:
        output = source.load()
    output[field].values[0, 0, 1] *= 60.0
    output.to_netcdf(path, engine="h5netcdf", mode="w")
    with pytest.raises(AssertionError):
        check_outputs(input_path, output_dir)


def test_checker_rejects_stale_clear_sky_emissions(tmp_path):
    input_path = tmp_path / "input.nc"
    output_dir = tmp_path / "output"
    generate_input(input_path)
    write_reference_outputs(input_path, output_dir)
    path = output_dir / "lightning_1.nc"
    with xr.open_dataset(path, engine="h5netcdf", decode_times=False) as source:
        output = source.load()
    output["LIGHTNING_NO"].values[:] = 1.0
    output.to_netcdf(path, engine="h5netcdf", mode="w")
    with pytest.raises(AssertionError):
        check_outputs(input_path, output_dir)
