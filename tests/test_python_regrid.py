"""Explicit Python access to the standalone driver's AXIS regridding."""

import numpy as np
import pytest
import xarray as xr
from cece import Grid
from cece.preparation import regrid_rectilinear


def test_passthrough_retains_values_units_and_target_coordinates():
    source = xr.DataArray(
        np.arange(12.0).reshape(3, 4),
        dims=("latitude", "longitude"),
        coords={"latitude": [-45.0, 0.0, 45.0], "longitude": [0.0, 90.0, 180.0, 270.0]},
        attrs={"units": "K"},
        name="temperature",
    )
    grid = Grid.structured(
        longitude=[0.0, 90.0, 180.0, 270.0], latitude=[-45.0, 0.0, 45.0]
    )
    result = regrid_rectilinear(source, grid, method="passthrough")
    xr.testing.assert_identical(result, source)


def test_regrid_uses_axis_for_nearest_neighbor():
    pytest.importorskip("axis")
    source = xr.DataArray(
        np.arange(12.0).reshape(3, 4),
        dims=("latitude", "longitude"),
        coords={"latitude": [-45.0, 0.0, 45.0], "longitude": [0.0, 90.0, 180.0, 270.0]},
        attrs={"units": "K"},
    )
    grid = Grid.structured(
        longitude=[0.0, 90.0, 180.0, 270.0], latitude=[-45.0, 0.0, 45.0]
    )
    result = regrid_rectilinear(source, grid, method="nearest")
    np.testing.assert_allclose(result.values, source.values)


def test_nearest_regrids_to_different_rectilinear_geometry():
    pytest.importorskip("axis")
    source = xr.DataArray(
        np.arange(12.0).reshape(3, 4),
        dims=("latitude", "longitude"),
        coords={"latitude": [-45.0, 0.0, 45.0], "longitude": [0.0, 90.0, 180.0, 270.0]},
    )
    grid = Grid.structured(
        longitude=[20.0, 100.0, 200.0, 280.0], latitude=[-30.0, 30.0]
    )
    result = regrid_rectilinear(source, grid, method="nearest")
    assert result.shape == (2, 4)
    np.testing.assert_allclose(
        result.values, [[0.0, 1.0, 2.0, 3.0], [8.0, 9.0, 10.0, 11.0]]
    )
    np.testing.assert_array_equal(result.latitude, [-30.0, 30.0])


def test_regrid_rejects_unsupported_geometry_and_method():
    source = xr.DataArray(
        np.ones((3, 4)),
        dims=("latitude", "longitude"),
        coords={"latitude": [-45.0, 0.0, 45.0], "longitude": [0.0, 90.0, 180.0, 270.0]},
    )
    with pytest.raises(ValueError, match="structured"):
        regrid_rectilinear(source, Grid.point(0, 0), method="nearest")
    with pytest.raises(ValueError, match="method"):
        regrid_rectilinear(
            source,
            Grid.structured(longitude=[0.0, 90.0], latitude=[0.0, 45.0]),
            method="conservative",
        )


def test_regrid_rejects_missing_values():
    source = xr.DataArray(
        np.array([[1.0, np.nan], [3.0, 4.0]]),
        dims=("latitude", "longitude"),
        coords={"latitude": [-45.0, 45.0], "longitude": [0.0, 180.0]},
    )
    grid = Grid.structured(longitude=[0.0, 180.0], latitude=[-45.0, 45.0])
    with pytest.raises(ValueError, match="non-finite"):
        regrid_rectilinear(source, grid, method="nearest")


@pytest.mark.parametrize("axis_first", [True, False])
def test_axis_and_model_share_runtime_across_initialization_orders(axis_first):
    pytest.importorskip("axis")
    import os
    import subprocess
    import sys
    import textwrap

    script = textwrap.dedent("""
        import numpy as np
        import xarray as xr
        import cece
        from cece.preparation import regrid_rectilinear
        def regrid():
            data = xr.DataArray(np.arange(12.).reshape(3, 4), dims=('latitude', 'longitude'),
                coords={'latitude': [-45., 0., 45.], 'longitude': [0., 90., 180., 270.]})
            grid = cece.Grid.structured(longitude=[0., 90., 180., 270.], latitude=[-45., 0., 45.])
            np.testing.assert_allclose(regrid_rectilinear(data, grid, method='nearest'), data)
        if FIRST:
            regrid()
        with cece.Model(['native_example'], grid=cece.Grid.point(0, 0)) as model:
            model.set_inputs({'base_anthropogenic_nox': 2.})
            assert model.step(time='2020-01-01T00:00:00Z', dt=60).nox.item() == 4.
            if not FIRST:
                regrid()
        regrid()
    """).replace("FIRST", repr(axis_first))
    result = subprocess.run(
        [sys.executable, "-c", script],
        env=os.environ.copy(),
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
