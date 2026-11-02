"""Explicit nearest-point selection for column forcing."""
import numpy as np
import pytest
import xarray as xr
import cece

from cece.preparation import sample_point


def test_nearest_point_wraps_longitude_and_preserves_field_metadata():
    data = xr.DataArray([[1., 2., 3.], [4., 5., 6.]], dims=('latitude', 'longitude'),
                        coords={'latitude': [-10., 10.], 'longitude': [0., 120., 240.]},
                        attrs={'units': 'K'}, name='temperature')
    result = sample_point(data, longitude=-119., latitude=9., max_distance_km=500.)
    assert result.dims == ('column',)
    assert result.item() == 6.
    assert result.attrs['units'] == 'K'
    assert result.longitude.item() == -119.
    assert result.latitude.item() == 9.
    with cece.Model(['native_example'], grid=cece.Grid.point(-119., 9.)) as model:
        model.set_inputs({'base_anthropogenic_nox': result})
        assert model.step(time='2020-01-01T00:00:00Z', dt=60).nox.item() == 12.


def test_point_sampling_rejects_missing_field_and_unsupported_shape():
    field = xr.DataArray([[float('nan'), 2.], [3., 4.]], dims=('latitude', 'longitude'),
                         coords={'latitude': [-10., 10.], 'longitude': [0., 120.]})
    with pytest.raises(ValueError, match='non-finite'):
        sample_point(field, longitude=0., latitude=-10., max_distance_km=500.)
    with pytest.raises(ValueError, match='distance'):
        sample_point(field, longitude=60., latitude=0., max_distance_km=1.)
