"""Python access to the shared standalone/NUOPC simulation lifecycle."""
from pathlib import Path
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
