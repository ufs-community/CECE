"""Unit tests for NASA EarthAccess lightning inputs and derivations."""

from __future__ import annotations

import importlib.util
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import xarray as xr
import yaml

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "cece_earthaccess_standalone_ingest",
    ROOT / "scripts" / "cece_earthaccess_standalone_ingest.py",
)
INGEST = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = INGEST
SPEC.loader.exec_module(INGEST)


def test_grid_cell_area_uses_spherical_row_areas():
    lon = np.array([-1.5, -0.5, 0.5, 1.5])
    lat = np.array([-0.5, 0.5])

    areas = INGEST._grid_cell_area(lon, lat)

    assert areas.shape == (2, 4)
    assert np.isfinite(areas).all()
    assert (areas > 0.0).all()
    np.testing.assert_allclose(areas[0], areas[1])


def test_grid_cell_area_preserves_descending_latitude_order():
    lon = np.array([-1.5, -0.5, 0.5, 1.5])
    lat = np.array([0.5, -0.5])

    areas = INGEST._grid_cell_area(lon, lat)

    assert (areas > 0.0).all()
    np.testing.assert_allclose(areas[0], areas[1])


def test_hypsometric_cloud_top_height_and_clear_sky_fallback():
    pressure = np.array([[50_000.0, 100_000.0]])
    temperature = np.array([[280.0, 0.0]])
    surface_pressure = np.array([[100_000.0, 100_000.0]])

    height = INGEST._hypsometric_cloud_top_height(
        pressure, temperature, surface_pressure
    )

    expected = 287.05 * 280.0 / 9.80665 * np.log(2.0)
    np.testing.assert_allclose(height, [[expected, 0.0]])


def test_swath_interpolation_keeps_paired_geolocation():
    data = xr.DataArray(
        [100.0, 200.0, 300.0],
        dims=("sample",),
        coords={
            "LAT": ("sample", [10.0, 0.0, 0.0]),
            "LON": ("sample", [1.0, 2.0, 3.0]),
        },
        name="cloud_top_height",
    )

    values = INGEST._interp_to_target(
        data, target_lons=np.array([1.0, 3.0]), target_lats=np.array([0.0, 10.0])
    )

    np.testing.assert_array_equal(values, [[200.0, 300.0], [100.0, 100.0]])


def test_read_stream_ingests_swath_cloud_top_height(monkeypatch, tmp_path):
    dataset = xr.Dataset({
        "LAT": ("sample", [10.0, 0.0, 0.0]),
        "LON": ("sample", [1.0, 2.0, 3.0]),
        "cloud_top_height": ("sample", [1000.0, 2000.0, 3000.0]),
    })
    monkeypatch.setattr(INGEST, "_open_dataset", lambda *_: dataset)

    manifest = INGEST._read_stream(
        {
            "name": "satellite_cloud_top",
            "variables": {"cloud_top_height": "CLOUD_TOP_HEIGHT"},
        },
        None,
        np.datetime64("2017-12-05T00:00:00"),
        datetime(2017, 12, 5),
        np.array([1.0, 3.0]),
        np.array([0.0, 10.0]),
        tmp_path,
    )

    values = np.fromfile(tmp_path / "CLOUD_TOP_HEIGHT.f64").reshape(2, 2)
    np.testing.assert_array_equal(values, [[2000.0, 3000.0], [1000.0, 1000.0]])
    assert manifest[0].startswith("CLOUD_TOP_HEIGHT 2 2 1 ")


def test_read_stream_derives_merra_cloud_height_and_cell_area(monkeypatch, tmp_path):
    dataset = xr.Dataset(
        {
            "CLDPRS": (
                ("time", "lat", "lon"),
                np.full((1, 2, 4), 50_000.0),
            ),
            "CLDTMP": (
                ("time", "lat", "lon"),
                np.full((1, 2, 4), 280.0),
            ),
            "PS": (
                ("time", "lat", "lon"),
                np.full((1, 2, 4), 100_000.0),
            ),
        },
        coords={
            "time": [np.datetime64("2022-07-01T00:30:00")],
            "lat": [-0.5, 0.5],
            "lon": [-1.5, -0.5, 0.5, 1.5],
        },
    )
    monkeypatch.setattr(INGEST, "_open_dataset", lambda *_: dataset)

    manifest = INGEST._read_stream(
        {
            "name": "merra2_cloud_top",
            "derived_variables": {
                "cloud_top_height": {
                    "method": "hypsometric_cloud_top_height",
                    "cloud_top_pressure": "CLDPRS",
                    "cloud_top_temperature": "CLDTMP",
                    "surface_pressure": "PS",
                },
                "cell_area": "grid_cell_area",
            },
        },
        None,
        np.datetime64("2022-07-01T00:00:00"),
        datetime(2022, 7, 1),
        np.array([-1.5, -0.5, 0.5, 1.5]),
        np.array([-0.5, 0.5]),
        tmp_path,
    )

    expected_height = 287.05 * 280.0 / 9.80665 * np.log(2.0)
    heights = np.fromfile(tmp_path / "cloud_top_height.f64").reshape(2, 4)
    areas = np.fromfile(tmp_path / "cell_area.f64").reshape(2, 4)
    np.testing.assert_allclose(heights, expected_height)
    assert (areas > 0.0).all()
    assert len(manifest) == 2


def test_lightning_earthaccess_configs_have_required_inputs():
    satellite_path = (
        ROOT / "examples" / "cece_config_lightning_earthaccess_satellite.yaml"
    )
    merra_path = ROOT / "examples" / "cece_config_lightning_earthaccess_merra2.yaml"
    satellite = yaml.safe_load(satellite_path.read_text())
    merra = yaml.safe_load(merra_path.read_text())

    satellite_streams = satellite["cece_data"]["streams"]
    assert satellite_streams[0]["short_name"] == "CALIOP_CLOUDSAT_MODIS_25KM"
    assert "cloud_top_height" in satellite_streams[0]["variables"]
    assert satellite_streams[1]["variables"]["FRLAND"]["model"] == "LAND_MASK"
    assert satellite_streams[1]["derived_variables"]["cell_area"] == "grid_cell_area"

    merra_streams = merra["cece_data"]["streams"]
    assert {"CLDPRS", "CLDTMP", "PS"} <= set(merra_streams[0]["variables"])
    assert (
        merra_streams[0]["derived_variables"]["cloud_top_height"]["method"]
        == "hypsometric_cloud_top_height"
    )
    assert merra_streams[1]["derived_variables"]["cell_area"] == "grid_cell_area"
