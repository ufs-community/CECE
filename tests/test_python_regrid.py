"""Explicit Python access to the standalone driver's AXIS regridding."""
import numpy as np
import pytest
import xarray as xr

from cece import Grid
from cece.preparation import regrid_rectilinear


def test_passthrough_retains_values_units_and_target_coordinates():
    source = xr.DataArray(np.arange(12.).reshape(3, 4), dims=('latitude', 'longitude'),
                          coords={'latitude': [-45., 0., 45.], 'longitude': [0., 90., 180., 270.]},
                          attrs={'units': 'K'}, name='temperature')
    grid = Grid.structured(longitude=[0., 90., 180., 270.], latitude=[-45., 0., 45.])
    result = regrid_rectilinear(source, grid, method='passthrough')
    xr.testing.assert_identical(result, source)


def test_regrid_uses_axis_for_nearest_neighbor():
    source = xr.DataArray(np.arange(12.).reshape(3, 4), dims=('latitude', 'longitude'),
                          coords={'latitude': [-45., 0., 45.], 'longitude': [0., 90., 180., 270.]},
                          attrs={'units': 'K'})
    grid = Grid.structured(longitude=[0., 90., 180., 270.], latitude=[-45., 0., 45.])
    result = regrid_rectilinear(source, grid, method='nearest')
    np.testing.assert_allclose(result.values, source.values)


def test_nearest_regrids_to_different_rectilinear_geometry():
    source = xr.DataArray(np.full((3, 4), 7.), dims=('latitude', 'longitude'),
                          coords={'latitude': [-45., 0., 45.], 'longitude': [0., 90., 180., 270.]})
    grid = Grid.structured(longitude=[20., 100., 200., 280.], latitude=[-30., 30.])
    result = regrid_rectilinear(source, grid, method='nearest')
    assert result.shape == (2, 4)
    np.testing.assert_allclose(result.values, 7.)
    np.testing.assert_array_equal(result.latitude, [-30., 30.])


def test_regrid_rejects_unsupported_geometry_and_method():
    source = xr.DataArray(np.ones((3, 4)), dims=('latitude', 'longitude'),
                          coords={'latitude': [-45., 0., 45.], 'longitude': [0., 90., 180., 270.]})
    with pytest.raises(ValueError, match='structured'):
        regrid_rectilinear(source, Grid.point(0, 0), method='nearest')
    with pytest.raises(ValueError, match='method'):
        regrid_rectilinear(source, Grid.structured(longitude=[0., 90.], latitude=[0., 45.]), method='conservative')


def test_regrid_rejects_missing_values():
    source = xr.DataArray(np.array([[1., np.nan], [3., 4.]]), dims=('latitude', 'longitude'),
                          coords={'latitude': [-45., 45.], 'longitude': [0., 180.]})
    grid = Grid.structured(longitude=[0., 180.], latitude=[-45., 45.])
    with pytest.raises(ValueError, match='non-finite'):
        regrid_rectilinear(source, grid, method='nearest')
