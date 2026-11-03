"""Explicit nearest-point selection for column forcing."""

import cece
import pytest
import xarray as xr
from cece.preparation import sample_point


def test_nearest_point_wraps_longitude_and_preserves_field_metadata():
    data = xr.DataArray(
        [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]],
        dims=("latitude", "longitude"),
        coords={"latitude": [-10.0, 10.0], "longitude": [0.0, 120.0, 240.0]},
        attrs={"units": "K"},
        name="temperature",
    )
    result = sample_point(data, longitude=-119.0, latitude=9.0, max_distance_km=500.0)
    assert result.dims == ("column",)
    assert result.item() == pytest.approx(6.0)
    assert result.attrs["units"] == "K"
    assert result.longitude.item() == pytest.approx(-119.0)
    assert result.latitude.item() == pytest.approx(9.0)
    with cece.Model(["native_example"], grid=cece.Grid.point(-119.0, 9.0)) as model:
        model.set_inputs({"base_anthropogenic_nox": result})
        assert model.step(
            time="2020-01-01T00:00:00Z", dt=60
        ).nox.item() == pytest.approx(12.0)


def test_point_sampling_rejects_missing_field_and_unsupported_shape():
    field = xr.DataArray(
        [[float("nan"), 2.0], [3.0, 4.0]],
        dims=("latitude", "longitude"),
        coords={"latitude": [-10.0, 10.0], "longitude": [0.0, 120.0]},
    )
    with pytest.raises(ValueError, match="non-finite"):
        sample_point(field, longitude=0.0, latitude=-10.0, max_distance_km=500.0)
    with pytest.raises(ValueError, match="distance"):
        sample_point(field, longitude=60.0, latitude=0.0, max_distance_km=1.0)
