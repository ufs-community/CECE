"""EarthAccess fields retain their horizontal meaning in native state."""
from datetime import datetime
from types import SimpleNamespace

import numpy as np
import pytest
import xarray as xr
from cece import _cece_core
from cece.stream_bridge import EarthAccessStreamBridge


def make_bridge(tolerance='1h'):
    bridge = EarthAccessStreamBridge.__new__(EarthAccessStreamBridge)
    bridge._datasets = [xr.Dataset({'input': (('time', 'lat', 'lon'), [[[1., 2., 3.], [4., 5., 6.]]])},
                       coords={'time': np.array(['2020-01-01T00'], dtype='datetime64[h]'),
                               'lat': [10., 20.], 'lon': [30., 40., 50.]})]
    bridge._configs = [SimpleNamespace(variable_map={'input': 'temperature'})]
    bridge._time_tolerance = tolerance
    return bridge


def test_bridge_prepares_xy_surface_array_and_populates_native_state():
    class Capture:
        def set_field(self, name, values):
            self.values = values
    bridge = make_bridge()
    captured = Capture()
    bridge.inject_at_time(captured, datetime(2020, 1, 1))
    assert captured.values.shape == (3, 2, 1)
    np.testing.assert_array_equal(captured.values[:, :, 0], [[1., 4.], [2., 5.], [3., 6.]])
    assert captured.values.flags.f_contiguous
    _cece_core.initialize_kokkos()
    state = _cece_core.CeceImportState()
    bridge.inject_at_time(state, datetime(2020, 1, 1))
    assert 'temperature' in state.get_field_names()


def test_bridge_rejects_stale_data_with_explicit_tolerance():
    with pytest.raises(ValueError, match='tolerance'):
        make_bridge().inject_at_time(object(), datetime(2020, 1, 2))


def test_bridge_context_closes_all_datasets():
    bridge = make_bridge()
    events = []
    bridge._datasets[0].set_close(lambda: events.append('closed'))
    with bridge:
        assert not events
    assert events == ['closed']
    bridge.close()
    assert events == ['closed']
