"""State assignment must validate geometry through every public entry point."""
import importlib.util
from pathlib import Path
import numpy as np
import pytest

spec = importlib.util.spec_from_file_location("cece_state_under_test", Path(__file__).parents[1] / "src/python/state.py")
state_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(state_module)

def test_dictionary_assignment_rejects_wrong_geometry():
    state = state_module.CeceState(2, 3, 1)
    with pytest.raises(ValueError, match="dimensions"):
        state.import_fields["temperature"] = np.ones((3, 2, 1))

def test_dictionary_assignment_normalizes_float_and_layout():
    state = state_module.CeceState(2, 3, 1)
    state.import_fields["temperature"] = np.ones((2, 3, 1), dtype=np.float32)
    field = state.import_fields["temperature"]
    assert field.dtype == np.float64
    assert field.flags.f_contiguous

def test_assignment_accepts_noncontiguous_input_without_changing_axes():
    state = state_module.CeceState(2, 3, 1)
    source = np.arange(12.).reshape(4, 3, 1)[::2]
    state.add_import_field("temperature", source)
    np.testing.assert_array_equal(state.import_fields["temperature"], source)
