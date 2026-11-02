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
