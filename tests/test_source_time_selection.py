import numpy as np
import pytest
import xarray as xr
from cece.preparation import select_time


def test_nearest_time_requires_tolerance_and_rejects_stale_data():
    data = xr.Dataset({'t': ('time', [1., 2.])}, coords={'time': np.array(['2020-01-01T00', '2020-01-01T06'], dtype='datetime64[h]')})
    assert select_time(data, '2020-01-01T05', tolerance='2h').t.item() == 2
    with pytest.raises(ValueError, match='tolerance'):
        select_time(data, '2020-01-02', tolerance='2h')


def test_static_data_is_unchanged():
    data = xr.Dataset({'x': ('column', [1., 2.])})
    assert select_time(data, '2020-01-01', tolerance='1h').identical(data)


def test_forecast_reference_time_is_shifted_by_explicit_scalar_lead():
    from cece.preparation import forecast_valid_time
    dataset = xr.Dataset({'value': ('time', [1., 2.])}, coords={
        'time': np.array(['2020-01-01T00', '2020-01-01T06'], dtype='datetime64[h]'),
        'step': np.timedelta64(6, 'h')})
    result = forecast_valid_time(dataset)
    np.testing.assert_array_equal(result.time.values, np.array(['2020-01-01T06', '2020-01-01T12'], dtype='datetime64[h]'))
    assert 'step' not in result.coords
    assert select_time(result, '2020-01-01T12', tolerance='0h').value.item() == 2.
