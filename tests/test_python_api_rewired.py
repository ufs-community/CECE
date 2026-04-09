"""
Integration tests for the rewired Python API layer (Task 9.2).

Tests that the public API surface works correctly after replacing _bindings
with _aces_core pybind11 module.
"""

import os
import sys
from pathlib import Path

import pytest
import numpy as np

# Add the build output directory so the aces package (with _aces_core.so) is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build", "src", "python"))

import aces
from aces import _aces_core


@pytest.fixture(scope="session", autouse=True)
def kokkos_runtime():
    """Initialize Kokkos once for the entire test session."""
    if not _aces_core.is_kokkos_initialized():
        _aces_core.initialize_kokkos()
    yield


@pytest.fixture(autouse=True)
def reset_aces_state():
    """Ensure ACES is finalized between tests to avoid state leakage."""
    yield
    if aces.is_initialized():
        aces.finalize()


class TestInitializeFinalize:
    """Tests for initialize() / finalize() lifecycle."""

    def test_initialize_with_python_config(self):
        """Test initialize with a Python AcesConfig object."""
        config = aces.AcesConfig()
        aces.initialize(config)
        assert aces.is_initialized()

    def test_finalize_after_initialize(self):
        """Test finalize cleans up properly."""
        config = aces.AcesConfig()
        aces.initialize(config)
        assert aces.is_initialized()
        aces.finalize()
        assert not aces.is_initialized()

    def test_double_initialize_raises(self):
        """Test that calling initialize twice raises RuntimeError."""
        config = aces.AcesConfig()
        aces.initialize(config)
        with pytest.raises(RuntimeError, match="already initialized"):
            aces.initialize(config)

    def test_finalize_when_not_initialized_raises(self):
        """Test that finalize raises when not initialized."""
        with pytest.raises(RuntimeError, match="not initialized"):
            aces.finalize()

    def test_is_initialized_false_initially(self):
        """Test that is_initialized returns False before initialize."""
        assert not aces.is_initialized()

    def test_initialize_with_dict_config(self):
        """Test initialize with a dict config."""
        config_dict = {
            "species": {},
            "physics_schemes": [],
            "aces_data": {"streams": []},
            "temporal_cycles": {},
        }
        aces.initialize(config_dict)
        assert aces.is_initialized()

    def test_reinitialize_after_finalize(self):
        """Test that initialize works again after finalize."""
        config = aces.AcesConfig()
        aces.initialize(config)
        aces.finalize()
        aces.initialize(config)
        assert aces.is_initialized()


class TestComputeEndToEnd:
    """Tests for compute() end-to-end with import fields and export field retrieval."""

    def test_compute_with_species(self):
        """Test compute with a species config and import fields."""
        config = aces.AcesConfig()
        vdist = aces.VerticalDistributionConfig(method="single")
        layer = aces.EmissionLayer(field_name="test_field", vdist=vdist)
        config.add_species("TEST", [layer])

        aces.initialize(config)

        state = aces.AcesState(nx=4, ny=4, nz=1)
        state.add_import_field(
            "test_field",
            np.ones((4, 4, 1), dtype=np.float64),
        )

        # compute should not crash
        aces.compute(state, config)

        # Export fields should be populated for the species
        assert "TEST" in state.export_fields

    def test_compute_not_initialized_raises(self):
        """Test that compute raises when not initialized."""
        state = aces.AcesState(nx=4, ny=4, nz=1)
        with pytest.raises(RuntimeError, match="not initialized"):
            aces.compute(state)

    def test_compute_invalid_state_type_raises(self):
        """Test that compute raises for non-AcesState input."""
        config = aces.AcesConfig()
        aces.initialize(config)
        with pytest.raises(aces.AcesStateError):
            aces.compute("not a state")

    def test_compute_with_explicit_config(self):
        """Test compute with an explicit config argument."""
        config = aces.AcesConfig()
        aces.initialize(config)

        # Pass a different config with a species
        config2 = aces.AcesConfig()
        vdist = aces.VerticalDistributionConfig(method="single")
        layer = aces.EmissionLayer(field_name="co_field", vdist=vdist)
        config2.add_species("CO", [layer])

        state = aces.AcesState(nx=4, ny=4, nz=1)
        state.add_import_field(
            "co_field",
            np.ones((4, 4, 1), dtype=np.float64) * 2.0,
        )

        aces.compute(state, config2)
        assert "CO" in state.export_fields


class TestLogLevelAndExecutionSpace:
    """Tests for set_log_level / get_execution_space / get_available_execution_spaces."""

    def test_set_log_level_valid(self):
        """Test setting valid log levels."""
        for level in ["DEBUG", "INFO", "WARNING", "ERROR"]:
            aces.set_log_level(level)

    def test_set_log_level_invalid_raises(self):
        """Test that invalid log level raises ValueError."""
        with pytest.raises(ValueError, match="Invalid log level"):
            aces.set_log_level("INVALID")

    def test_set_log_level_case_insensitive(self):
        """Test that log level is case-insensitive."""
        aces.set_log_level("debug")
        aces.set_log_level("Info")
        aces.set_log_level("WARNING")

    def test_get_execution_space_returns_string(self):
        """Test that get_execution_space returns a non-empty string."""
        space = aces.get_execution_space()
        assert isinstance(space, str)
        assert len(space) > 0

    def test_get_available_execution_spaces_returns_list(self):
        """Test that get_available_execution_spaces returns a non-empty list."""
        spaces = aces.get_available_execution_spaces()
        assert isinstance(spaces, list)
        assert len(spaces) > 0

    def test_current_space_in_available(self):
        """Test that the current execution space is in the available list."""
        current = aces.get_execution_space()
        available = aces.get_available_execution_spaces()
        assert current in available

    def test_set_execution_space_invalid_raises(self):
        """Test that setting an unavailable execution space raises."""
        with pytest.raises(aces.AcesExecutionSpaceError):
            aces.set_execution_space("NonExistentSpace")


class TestPublicAPISurface:
    """Tests that the public API surface matches __all__."""

    def test_all_exports_exist(self):
        """Test that every name in __all__ is actually defined in the module."""
        for name in aces.__all__:
            assert hasattr(aces, name), f"'{name}' is in __all__ but not defined in aces module"

    def test_all_functions_callable(self):
        """Test that all function exports are callable."""
        function_names = [
            "initialize",
            "finalize",
            "is_initialized",
            "compute",
            "load_config",
            "set_execution_space",
            "get_execution_space",
            "get_available_execution_spaces",
            "set_log_level",
            "get_diagnostics",
            "reset_diagnostics",
            "get_last_error",
        ]
        for name in function_names:
            obj = getattr(aces, name)
            assert callable(obj), f"'{name}' should be callable"

    def test_all_classes_exist(self):
        """Test that all class exports are classes."""
        class_names = [
            "AcesConfig",
            "AcesState",
            "AcesField",
            "EmissionLayer",
            "VerticalDistributionConfig",
            "AcesException",
            "AcesConfigError",
            "AcesComputationError",
            "AcesStateError",
            "AcesExecutionSpaceError",
        ]
        for name in class_names:
            obj = getattr(aces, name)
            assert isinstance(obj, type), f"'{name}' should be a class"

    def test_no_bindings_import(self):
        """Test that _bindings is not imported anywhere in the module."""
        assert not hasattr(aces, '_bindings'), "_bindings should not be imported"
        assert not hasattr(aces, '_c_bindings'), "_c_bindings should not be imported"

    def test_aces_core_available(self):
        """Test that _aces_core pybind11 module is available."""
        assert hasattr(aces, '_aces_core')


class TestDiagnosticsAndErrors:
    """Tests for get_diagnostics, reset_diagnostics, get_last_error."""

    def test_get_diagnostics_returns_dict(self):
        """Test that get_diagnostics returns a dict."""
        diag = aces.get_diagnostics()
        assert isinstance(diag, dict)

    def test_reset_diagnostics_no_error(self):
        """Test that reset_diagnostics doesn't raise."""
        aces.reset_diagnostics()

    def test_get_last_error_initially_none(self):
        """Test that get_last_error returns None initially."""
        assert aces.get_last_error() is None


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
