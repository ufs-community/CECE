"""Offline integration with MONETIO's real TEMPO reader and grouped files."""

import numpy as np
import pytest
import xarray as xr
from cece import Grid
from cece.preparation import regrid_rectilinear
from cece.sources import MonetioSource


def test_tempo_quality_screening_and_swath_geometry(tmp_path):
    pytest.importorskip("monetio")
    pytest.importorskip("h5netcdf")
    path = tmp_path / "tempo.nc"
    xr.Dataset(
        {
            "vertical_column_troposphere": (
                ("time", "x", "y"),
                [[[1.0, 2.0], [3.0, 4.0]]],
                {"units": "mol m-2"},
            ),
            "main_data_quality_flag": (("time", "x", "y"), [[[0, 10], [0, 0]]]),
        },
        coords={"time": np.array(["2020-01-01T00"], dtype="datetime64[h]")},
    ).to_netcdf(path, engine="h5netcdf", group="product")
    xr.Dataset({
        "latitude": (("x", "y"), [[30.0, 31.0], [32.0, 33.0]]),
        "longitude": (("x", "y"), [[-100.0, -99.0], [-98.0, -97.0]]),
    }).to_netcdf(path, engine="h5netcdf", group="geolocation", mode="a")
    with MonetioSource(
        "tempo",
        files=str(path),
        variables=["vertical_column_troposphere"],
        tolerance="1h",
        group=["product", "geolocation"],
        variable_dict={"vertical_column_troposphere": {"quality_flag_max": 5}},
    ) as source:
        data = source.read("2020-01-01")
        field = data["vertical_column_troposphere"]
        assert np.isnan(field.values[0, 1])
        assert field.values[1, 1] == pytest.approx(4.0)
        assert field.latitude.shape == (2, 2)
        with pytest.raises(ValueError, match="tolerance"):
            source.read("2020-01-02")
        with pytest.raises(ValueError, match="latitude and longitude dimensions"):
            regrid_rectilinear(
                field,
                Grid.structured(longitude=[-100.0, -99.0], latitude=[30.0, 31.0]),
                method="nearest",
            )
