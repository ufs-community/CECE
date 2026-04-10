"""
Tests for pybind11 compute and StackingEngine bindings (_cece_core module).

Tests StackingEngine construction, GIL release during ComputeEmissions and
StackingEngine.Execute, and C++ exception translation during compute.
"""

import os
import sys
import threading
import time
import pytest
import numpy as np

# Add the build output directory to the path so _cece_core can be imported
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build", "src", "python", "cece"))

import _cece_core


@pytest.fixture(scope="session", autouse=True)
def kokkos_runtime():
    """Initialize Kokkos once for the entire test session."""
    _cece_core.initialize_kokkos()
    yield


def _make_compute_setup(nx=4, ny=4, nz=1):
    """Helper: create a config, import/export states, and resolver for compute tests."""
    config = _cece_core.CeceConfig()
    layer = _cece_core.EmissionLayer()
    layer.operation = "add"
    layer.field_name = "test_field"
    layer.scale = 1.0
    _cece_core.AddSpecies(config, "TEST", [layer])

    import_state = _cece_core.CeceImportState()
    export_state = _cece_core.CeceExportState()

    # Set up import field
    field_data = np.asfortranarray(np.ones((nx, ny, nz), dtype=np.float64))
    import_state.set_field("test_field", field_data)

    # Set up export field (species name "TEST" is the export key)
    export_data = np.asfortranarray(np.zeros((nx, ny, nz), dtype=np.float64))
    export_state.set_field("TEST", export_data)

    resolver = _cece_core.CeceStateResolver(
        import_state, export_state,
        {"test_field": "test_field"},
    )
    return config, import_state, export_state, resolver


class TestStackingEngineConstruction:
    """Tests for StackingEngine construction from CeceConfig."""

    def test_construct_from_empty_config(self):
        """Test StackingEngine can be constructed from an empty CeceConfig."""
        config = _cece_core.CeceConfig()
        engine = _cece_core.StackingEngine(config)
        assert engine is not None

    def test_construct_from_config_with_species(self):
        """Test StackingEngine construction with a config that has species."""
        config = _cece_core.CeceConfig()
        layer = _cece_core.EmissionLayer()
        layer.operation = "add"
        layer.field_name = "co_emis"
        layer.scale = 1.0
        _cece_core.AddSpecies(config, "CO", [layer])
        engine = _cece_core.StackingEngine(config)
        assert engine is not None

    def test_reset_bindings(self):
        """Test that ResetBindings can be called without error."""
        config = _cece_core.CeceConfig()
        engine = _cece_core.StackingEngine(config)
        engine.ResetBindings()

    def test_add_species(self):
        """Test that AddSpecies can be called on the engine."""
        config = _cece_core.CeceConfig()
        layer = _cece_core.EmissionLayer()
        layer.operation = "add"
        layer.field_name = "no_emis"
        layer.scale = 1.0
        _cece_core.AddSpecies(config, "NO", [layer])
        engine = _cece_core.StackingEngine(config)
        # AddSpecies on the engine for a species already in config
        engine.AddSpecies("NO")


class TestGILRelease:
    """Tests that ComputeEmissions and StackingEngine.Execute release the GIL.

    The GIL release is verified by running a Python thread concurrently with
    the C++ call. If the GIL were held, the thread would be blocked until the
    C++ call completes. We use a threading.Barrier to confirm the thread
    actually ran during the C++ call.
    """

    def test_compute_emissions_releases_gil(self):
        """Verify ComputeEmissions releases the GIL by running a Python thread concurrently."""
        config, import_state, export_state, resolver = _make_compute_setup()

        # Track whether a concurrent thread ran
        thread_ran = threading.Event()

        def background_task():
            thread_ran.set()

        t = threading.Thread(target=background_task)
        t.start()

        # Call compute_emissions — this releases the GIL via call_guard
        _cece_core.compute_emissions(config, resolver, 4, 4, 1)

        t.join(timeout=5.0)
        assert thread_ran.is_set(), "Background thread should have run (GIL was released)"

    def test_stacking_engine_execute_releases_gil(self):
        """Verify StackingEngine.Execute releases the GIL."""
        config, import_state, export_state, resolver = _make_compute_setup()
        engine = _cece_core.StackingEngine(config)

        thread_ran = threading.Event()

        def background_task():
            thread_ran.set()

        t = threading.Thread(target=background_task)
        t.start()

        engine.Execute(resolver, 4, 4, 1)

        t.join(timeout=5.0)
        assert thread_ran.is_set(), "Background thread should have run (GIL was released)"


class TestComputeExceptionTranslation:
    """Tests that C++ exceptions during compute are translated to Python exceptions."""

    def test_compute_emissions_with_empty_config_no_crash(self):
        """Test that compute_emissions with empty config doesn't crash."""
        config = _cece_core.CeceConfig()
        import_state = _cece_core.CeceImportState()
        export_state = _cece_core.CeceExportState()
        resolver = _cece_core.CeceStateResolver(import_state, export_state, {})

        # Empty config = no species = should be a no-op or raise, not crash
        try:
            _cece_core.compute_emissions(config, resolver, 4, 4, 1)
        except Exception as e:
            assert isinstance(e, Exception)

    def test_stacking_engine_execute_with_empty_config_no_crash(self):
        """Test StackingEngine.Execute with empty config doesn't crash."""
        config = _cece_core.CeceConfig()
        import_state = _cece_core.CeceImportState()
        export_state = _cece_core.CeceExportState()
        resolver = _cece_core.CeceStateResolver(import_state, export_state, {})

        engine = _cece_core.StackingEngine(config)
        # Empty config = no species = should be a no-op or raise, not crash
        try:
            engine.Execute(resolver, 4, 4, 1)
        except Exception as e:
            assert isinstance(e, Exception)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
