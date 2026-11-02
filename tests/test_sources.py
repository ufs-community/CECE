"""Provider lifecycle, offline data selection, and model integration."""
import sys
import types
import numpy as np
import pytest
import xarray as xr
import cece
from cece.sources import DatasetSource, MonetioSource


def dataset():
    return xr.Dataset({'base': (('time', 'member', 'column'), [[[1.], [2.]], [[3.], [4.]]], {'units': '1'})},
                      coords={'time': np.array(['2020-01-01T00', '2020-01-01T06'], dtype='datetime64[h]'), 'member': [0, 1], 'column': [0]})


def test_dataset_source_requires_explicit_ensemble_and_bounded_time():
    source = DatasetSource(dataset(), variables=['base'], tolerance='2h')
    with pytest.raises(ValueError, match='member'):
        source.read('2020-01-01T05')
    selected = source.read('2020-01-01T05', selection={'member': 1})
    assert selected.base.item() == 4
    with pytest.raises(ValueError, match='tolerance'):
        source.read('2020-01-02', selection={'member': 1})


def test_forecast_cycle_requires_explicit_selection():
    data = dataset().expand_dims(cycle=[0, 1])
    source = DatasetSource(data, variables=['base'], tolerance='2h')
    with pytest.raises(ValueError, match='cycle'):
        source.read('2020-01-01T05', selection={'member': 0})
    assert source.read('2020-01-01T05', selection={'member': 0, 'cycle': 1}).base.item() == 3


def test_forecast_lead_coordinate_requires_explicit_valid_time_preparation():
    data = dataset().assign_coords(step=np.timedelta64(6, 'h'))
    source = DatasetSource(data, variables=['base'], tolerance='2h')
    with pytest.raises(ValueError, match='valid time'):
        source.read('2020-01-01T05', selection={'member': 0})


def test_ncep_lead_without_valid_date_is_not_static():
    data = xr.Dataset({'base': ('column', [1.])}, coords={'leadTime': np.timedelta64(6, 'h')})
    source = DatasetSource(data, variables=['base'], tolerance='1h')
    with pytest.raises(ValueError, match='valid time'):
        source.read('2020-01-01')


def test_monetio_adapter_uses_public_load_and_closes(monkeypatch):
    calls = []
    monkeypatch.setitem(sys.modules, 'monetio', types.SimpleNamespace(load=lambda source, files=None, **kwargs: (calls.append((source, files, kwargs)) or dataset())))
    with MonetioSource('gefs', files=['forecast.grib2'], variables=['base'], tolerance='2h', engine='grib2io') as source:
        assert source.read('2020-01-01T05', selection={'member': 0}).base.item() == 3
    assert calls == [('gefs', ['forecast.grib2'], {'engine': 'grib2io'})]
    with pytest.raises(RuntimeError, match='closed'):
        source.read('2020-01-01T05', selection={'member': 0})


def test_source_updates_model_and_preserves_units():
    source = DatasetSource(dataset(), variables=['base'], tolerance='2h')
    with cece.Model(['native_example'], grid=cece.Grid.point(0, 0)) as model:
        model.add_source(source, mapping={'base': 'base_anthropogenic_nox'}, selection={'member': 1})
        result = model.step(time='2020-01-01T05:00:00Z', dt=3600)
        assert result.nox.item() == 8
    with pytest.raises(RuntimeError, match='closed'):
        source.read('2020-01-01T05', selection={'member': 1})


def test_source_refresh_validates_all_inputs_before_mutating_model():
    first = DatasetSource(dataset(), variables=['base'], tolerance='2h')
    second = DatasetSource(xr.Dataset({'mult': ('column', [float('nan')], {'units':'1'})}), variables=['mult'], tolerance='2h')
    with cece.Model(['native_example'], grid=cece.Grid.point(0, 0)) as model:
        model.add_source(first, mapping={'base':'base_anthropogenic_nox'}, selection={'member':0})
        model.add_source(second, mapping={'mult':'multiplier_input'})
        with pytest.raises(ValueError, match='non-finite'):
            model.step(time='2020-01-01T05:00:00Z',dt=3600)
        assert not model._inputs


def test_missing_monetio_dependency_is_actionable(monkeypatch):
    monkeypatch.setitem(sys.modules, 'monetio', None)
    source = MonetioSource('gfs', variables=['base'], tolerance='1h')
    with pytest.raises(ImportError, match='monetio v1'):
        source.open()


def test_conflicting_field_ownership_is_rejected():
    with cece.Model(['native_example'], grid=cece.Grid.point(0, 0)) as model:
        source = DatasetSource(dataset(), variables=['base'], tolerance='2h')
        model.add_source(source, mapping={'base':'base_anthropogenic_nox'}, selection={'member':0})
        with pytest.raises(ValueError, match='source-owned'):
            model.set_inputs({'base_anthropogenic_nox':10.})
        with pytest.raises(ValueError, match='Restart'):
            model.state_dict()


def test_source_cannot_take_over_existing_input():
    with cece.Model(['native_example'], grid=cece.Grid.point(0, 0)) as model:
        model.set_inputs({'base_anthropogenic_nox': 10.})
        with pytest.raises(ValueError, match='explicit inputs'):
            model.add_source(DatasetSource(dataset(), variables=['base'], tolerance='2h'),
                             mapping={'base':'base_anthropogenic_nox'})


def test_custom_provider_needs_no_optional_reader():
    class Provider:
        closed = False

        def open(self):
            return self

        def read(self, time, selection=None):
            assert time == np.datetime64('2020-01-01T00:00:00')
            return {'forcing': np.array([3.])}

        def close(self):
            self.closed = True

    source = Provider()
    with cece.Model(['native_example'], grid=cece.Grid.point(0, 0)) as model:
        model.add_source(source, mapping={'forcing': 'base_anthropogenic_nox'})
        assert model.step(time='2020-01-01T00:00:00Z', dt=3600).nox.item() == 6
    assert source.closed


def test_prepared_forecast_drives_model_at_valid_time():
    from cece.preparation import forecast_valid_time

    data = dataset().assign_coords(step=np.timedelta64(6, 'h'))
    prepared = forecast_valid_time(data)
    source = DatasetSource(prepared, variables=['base'], tolerance='0h')
    with cece.Model(['native_example'], grid=cece.Grid.point(0, 0)) as model:
        model.add_source(source, mapping={'base': 'base_anthropogenic_nox'}, selection={'member': 1})
        result = model.step(time='2020-01-01T06:00:00Z', dt=3600)
        assert result.nox.item() == 4.
