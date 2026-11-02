"""Python access to the shared standalone/NUOPC simulation lifecycle."""
from pathlib import Path
import importlib.util
import subprocess
import sys
import textwrap
import yaml
import pytest
import cece


def test_from_config_rejects_unknown_execution_and_missing_file(tmp_path):
    with pytest.raises(ValueError, match='simulation'):
        cece.Model.from_config(tmp_path / 'missing.yaml', execution='other')
    with pytest.raises(FileNotFoundError):
        cece.Model.from_config(tmp_path / 'missing.yaml')


def test_configured_simulation_runs_to_completion(tmp_path):
    original = Path(__file__).parent / 'data' / 'test_driver_mesh_config.yaml'
    data = yaml.safe_load(original.read_text())
    data['output'] = {'enabled': False, 'fields': ['CO']}
    config = tmp_path / 'simulation.yaml'
    config.write_text(yaml.safe_dump(data))
    script = textwrap.dedent('''
        import cece
        import numpy as np
        with cece.Model.from_config(CONFIG, execution='simulation') as model:
            assert model.grid.ncol == 16
            try:
                cece.Model.from_config(CONFIG, execution='simulation')
                raise AssertionError('Concurrent simulations accepted')
            except cece.CeceException:
                pass
            try:
                model.step(time='2020-01-01T01:00:00Z', dt=3600)
                raise AssertionError('Out-of-order simulation step accepted')
            except ValueError:
                pass
            result = model.step(time='2020-01-01T00:00:00Z', dt=3600)
            assert model.complete
            assert result['CO'].shape == (4, 4, 1)
            assert np.isfinite(result['CO']).all()
            snapshot = result['CO'].values.copy()
        np.testing.assert_array_equal(result['CO'].values, snapshot)
        with cece.Model.from_config(CONFIG, execution='simulation') as again:
            assert again.step(time='2020-01-01T00:00:00Z', dt=3600)['CO'].shape == (4, 4, 1)
        with cece.Model(['native_example'], grid=cece.Grid.point(0, 0)) as column:
            column.set_inputs({'base_anthropogenic_nox': 2.})
            assert column.step(time='2020-01-01T00:00:00Z', dt=60).nox.item() == 4.
    ''').replace('CONFIG', repr(str(config)))
    result = subprocess.run([sys.executable, '-c', script], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr


def test_simulation_accepts_external_surface_inputs(tmp_path):
    original = Path(__file__).parent / 'data' / 'test_driver_mesh_config.yaml'
    data = yaml.safe_load(original.read_text())
    data['output'] = {'enabled': False, 'fields': ['CO']}
    config = tmp_path / 'simulation-inputs.yaml'
    config.write_text(yaml.safe_dump(data))
    script = textwrap.dedent('''
        import cece
        import numpy as np
        field = np.arange(16., dtype=float).reshape(4, 4) + 1.
        with cece.Model.from_config(CONFIG) as model:
            try:
                model.set_inputs({'CO_anthro': np.zeros((4, 4, 2))})
                raise AssertionError('Profile input was accepted by surface-only API')
            except ValueError:
                pass
            try:
                model.set_inputs({'a': field, 'b': field}, mapping={'a': 'CO_anthro', 'b': 'CO_anthro'})
                raise AssertionError('Duplicate input producers were accepted')
            except ValueError:
                pass
            import xarray as xr
            unlocated = xr.Dataset(
                {'source': (('latitude', 'longitude'), field.T, {'units': 'kg m-2 s-1'})}
            )
            try:
                model.set_inputs(unlocated, mapping={'source': 'CO_anthro'})
            except ValueError as error:
                assert 'coordinates are required' in str(error)
            else:
                raise AssertionError('Labeled simulation input without grid coordinates was accepted')
            labeled = xr.Dataset(
                {'source': (('latitude', 'longitude'), field.T)},
                coords={'longitude': list(model.grid.longitude), 'latitude': list(model.grid.latitude)},
            )
            try:
                model.set_inputs(labeled, mapping={'source': 'CO_anthro'})
            except ValueError as error:
                assert 'units are required' in str(error)
            else:
                raise AssertionError('Labeled simulation input without units was accepted')
            wrong_units = xr.Dataset(
                {'source': (('latitude', 'longitude'), field.T, {'units': 'm s-1'})},
                coords={'longitude': list(model.grid.longitude), 'latitude': list(model.grid.latitude)},
            )
            try:
                model.set_inputs(wrong_units, mapping={'source': 'multiplier_input'})
            except ValueError as error:
                assert 'expected' in str(error)
            else:
                raise AssertionError('Labeled input with incompatible scheme units was accepted')
            labeled.source.attrs['units'] = 'kg m-2 s-1'
            model.set_inputs(labeled, mapping={'source': 'CO_anthro'})
            result = model.step(time='2020-01-01T00:00:00Z', dt=3600)
            np.testing.assert_allclose(result.CO.values[:, :, 0], field)
    ''').replace('CONFIG', repr(str(config)))
    result = subprocess.run([sys.executable, '-c', script], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr


def test_simulation_rejects_unconfigured_external_fields_atomically(tmp_path):
    original = Path(__file__).parent / 'data' / 'test_driver_mesh_config.yaml'
    data = yaml.safe_load(original.read_text())
    data['output'] = {'enabled': False, 'fields': ['CO']}
    config = tmp_path / 'simulation-unknown-input.yaml'
    config.write_text(yaml.safe_dump(data))
    script = textwrap.dedent('''
        import cece
        import numpy as np
        forcing = np.full((4, 4), 2.0)
        with cece.Model.from_config(CONFIG) as model:
            try:
                model.set_inputs({'CO_anthro': forcing, 'misspelled_field': forcing})
            except ValueError as error:
                assert 'unknown configured input' in str(error)
            else:
                raise AssertionError('An unconfigured import field was accepted')
            actual = model.step(time='2020-01-01T00:00:00Z', dt=3600).CO.values
        with cece.Model.from_config(CONFIG) as baseline:
            expected = baseline.step(time='2020-01-01T00:00:00Z', dt=3600).CO.values
        np.testing.assert_array_equal(actual, expected)
    ''').replace('CONFIG', repr(str(config)))
    result = subprocess.run([sys.executable, '-c', script], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr


def test_simulation_accepts_profile_inputs(tmp_path):
    original = Path(__file__).parent / 'data' / 'test_driver_mesh_config.yaml'
    data = yaml.safe_load(original.read_text())
    data['driver']['grid']['nz'] = 2
    data['output'] = {'enabled': False, 'fields': ['CO']}
    config = tmp_path / 'simulation-profiles.yaml'
    config.write_text(yaml.safe_dump(data))
    script = textwrap.dedent('''
        import cece
        import numpy as np
        field = np.arange(32., dtype=float).reshape(4, 4, 2) + 1.
        with cece.Model.from_config(CONFIG) as model:
            model.set_inputs({'CO_anthro': field})
            result = model.step(time='2020-01-01T00:00:00Z', dt=3600)
            np.testing.assert_allclose(result.CO.values, field)
    ''').replace('CONFIG', repr(str(config)))
    result = subprocess.run([sys.executable, '-c', script], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr


def test_simulation_mixed_sources_and_duplicate_ownership(tmp_path):
    xr = pytest.importorskip('xarray')
    import numpy as np

    original = Path(__file__).parent / 'data' / 'test_driver_mesh_config.yaml'
    base = yaml.safe_load(original.read_text())
    base['species']['CO'].append(
        {'operation': 'add', 'field': 'stream_emis', 'hierarchy': 0, 'scale': 1.0}
    )
    source = tmp_path / 'forcing.nc'
    dataset = xr.Dataset(
        {'emissions': (('time', 'lat', 'lon'), np.full((1, 4, 4), 3.0))},
        coords={
            'time': np.array(['2020-01-01T00:00:00'], dtype='datetime64[s]'),
            'lon': [-135.0, -45.0, 45.0, 135.0],
            'lat': [-67.5, -22.5, 22.5, 67.5],
        },
    )
    dataset.to_netcdf(source)
    base['cece_data'] = {
        'streams': [{
            'name': 'forcing', 'file': str(source), 'mapalgo': 'passthrough',
            'variables': [{'file': 'emissions', 'model': 'stream_emis'}],
        }],
    }
    base['output'] = {'enabled': False, 'fields': ['CO']}
    mixed_config = tmp_path / 'simulation-mixed.yaml'
    mixed_config.write_text(yaml.safe_dump(base))
    overlap = yaml.safe_load(yaml.safe_dump(base))
    overlap['cece_data']['streams'][0]['variables'] = [{'file': 'emissions', 'model': 'CO_anthro'}]
    overlap_config = tmp_path / 'simulation-overlap.yaml'
    overlap_config.write_text(yaml.safe_dump(overlap))
    script = textwrap.dedent('''
        import cece
        import numpy as np
        forcing = np.full((4, 4), 2.0)
        with cece.Model.from_config(MIXED) as model:
            model.set_inputs({'CO_anthro': forcing})
            result = model.step(time='2020-01-01T00:00:00Z', dt=3600)
            np.testing.assert_allclose(result.CO.values[:, :, 0], 5.0)
        with cece.Model.from_config(OVERLAP) as model:
            try:
                model.set_inputs({'CO_anthro': forcing})
            except ValueError as error:
                assert 'configured file streams' in str(error)
            else:
                raise AssertionError('Duplicate source ownership was accepted')
    ''').replace('MIXED', repr(str(mixed_config))).replace('OVERLAP', repr(str(overlap_config)))
    result = subprocess.run([sys.executable, '-c', script], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('axis_first', [True, False])
def test_axis_and_cece_share_kokkos_runtime(axis_first, tmp_path):
    try:
        axis_available = importlib.util.find_spec('axis.axis_py') is not None
    except ModuleNotFoundError:
        axis_available = False
    if not axis_available or importlib.util.find_spec('cece._cece_simulation') is None:
        pytest.skip('AXIS and full simulation extensions are not built')
    original = Path(__file__).parent / 'data' / 'test_driver_mesh_config.yaml'
    data = yaml.safe_load(original.read_text())
    data['output'] = {'enabled': False, 'fields': ['CO']}
    config = tmp_path / 'simulation-kokkos.yaml'
    config.write_text(yaml.safe_dump(data))
    axis_import = "from axis import axis_py; axis_py.make_regular_mesh(4, 4, 0., -90., 90., 45.)"
    cece_import = textwrap.dedent('''
        import cece
        with cece.Model(['native_example'], grid=cece.Grid.point(0., 0.)) as column:
            column.set_inputs({'base_anthropogenic_nox': 2.})
            column.step(time='2020-01-01T00:00:00Z', dt=60.)
        with cece.Model.from_config(CONFIG) as simulation:
            simulation.step(time='2020-01-01T00:00:00Z', dt=3600.)
    ''').replace('CONFIG', repr(str(config)))
    script = axis_import + '\n' + cece_import if axis_first else cece_import + '\n' + axis_import
    result = subprocess.run([sys.executable, '-c', script], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
