"""
Tests for Python module import and basic functionality.

Tests that the aces module can be imported and basic classes are available.
"""

import pytest
import sys
from pathlib import Path

# Add src/python to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "src" / "python"))


class TestModuleImport:
    """Tests for module import."""

    def test_import_aces_module(self):
        """Test that aces module can be imported."""
        import aces
        assert aces is not None

    def test_import_config_class(self):
        """Test that AcesConfig can be imported."""
        from aces import AcesConfig
        assert AcesConfig is not None

    def test_import_state_class(self):
        """Test that AcesState can be imported."""
        from aces import AcesState
        assert AcesState is not None

    def test_import_field_class(self):
        """Test that AcesField can be imported."""
        from aces import AcesField
        assert AcesField is not None

    def test_import_emission_layer_class(self):
        """Test that EmissionLayer can be imported."""
        from aces import EmissionLayer
        assert EmissionLayer is not None

    def test_import_exceptions(self):
        """Test that exception classes can be imported."""
        from aces import (
            AcesException,
            AcesConfigError,
            AcesComputationError,
            AcesStateError,
            AcesExecutionSpaceError,
        )
        assert AcesException is not None
        assert AcesConfigError is not None
        assert AcesComputationError is not None
        assert AcesStateError is not None
        assert AcesExecutionSpaceError is not None

    def test_import_functions(self):
        """Test that module-level functions can be imported."""
        from aces import (
            initialize,
            finalize,
            is_initialized,
            compute,
            load_config,
            set_execution_space,
            get_execution_space,
            get_available_execution_spaces,
            set_log_level,
            get_diagnostics,
            reset_diagnostics,
            get_last_error,
        )
        assert initialize is not None
        assert finalize is not None
        assert is_initialized is not None
        assert compute is not None
        assert load_config is not None
        assert set_execution_space is not None
        assert get_execution_space is not None
        assert get_available_execution_spaces is not None
        assert set_log_level is not None
        assert get_diagnostics is not None
        assert reset_diagnostics is not None
        assert get_last_error is not None


class TestConfigClass:
    """Tests for AcesConfig class."""

    def test_create_empty_config(self):
        """Test creating an empty AcesConfig."""
        from aces import AcesConfig
        config = AcesConfig()
        assert config is not None
        assert config.species == {}
        assert config.physics_schemes == []

    def test_config_add_species(self):
        """Test adding a species to config."""
        from aces import AcesConfig, EmissionLayer, VerticalDistributionConfig
        config = AcesConfig()
        vdist = VerticalDistributionConfig(method="single")
        layer = EmissionLayer(field_name="CO", vdist=vdist)
        config.add_species("CO", [layer])
        assert "CO" in config.species
        assert len(config.species["CO"]) == 1

    def test_config_validate(self):
        """Test config validation."""
        from aces import AcesConfig
        config = AcesConfig()
        result = config.validate()
        assert result.is_valid

    def test_config_to_dict(self):
        """Test config serialization to dict."""
        from aces import AcesConfig
        config = AcesConfig()
        config_dict = config.to_dict()
        assert isinstance(config_dict, dict)
        assert "species" in config_dict
        assert "physics_schemes" in config_dict

    def test_config_from_dict(self):
        """Test config deserialization from dict."""
        from aces import AcesConfig
        config_dict = {
            "species": {},
            "physics_schemes": [],
            "aces_data": {"streams": []},
            "temporal_cycles": {},
        }
        config = AcesConfig.from_dict(config_dict)
        assert config is not None
        assert config.species == {}


class TestStateClass:
    """Tests for AcesState class."""

    def test_create_state(self):
        """Test creating an AcesState."""
        from aces import AcesState
        state = AcesState(nx=10, ny=10, nz=10)
        assert state is not None
        assert state.dimensions == (10, 10, 10)

    def test_state_invalid_dimensions(self):
        """Test that invalid dimensions raise ValueError."""
        from aces import AcesState
        with pytest.raises(ValueError):
            AcesState(nx=0, ny=10, nz=10)

    def test_state_add_import_field(self):
        """Test adding an import field to state."""
        import numpy as np
        from aces import AcesState
        state = AcesState(nx=10, ny=10, nz=10)
        field = np.ones((10, 10, 10), dtype=np.float64)
        state.add_import_field("temperature", field)
        assert "temperature" in state.import_fields

    def test_state_get_import_field(self):
        """Test retrieving an import field from state."""
        import numpy as np
        from aces import AcesState
        state = AcesState(nx=10, ny=10, nz=10)
        field = np.ones((10, 10, 10), dtype=np.float64)
        state.add_import_field("temperature", field)
        retrieved = state.get_import_field("temperature")
        assert np.allclose(retrieved, field)

    def test_state_field_not_found(self):
        """Test that accessing non-existent field raises KeyError."""
        from aces import AcesState
        state = AcesState(nx=10, ny=10, nz=10)
        with pytest.raises(KeyError):
            state.get_import_field("nonexistent")

    def test_state_dimension_mismatch(self):
        """Test that mismatched dimensions raise ValueError."""
        import numpy as np
        from aces import AcesState
        state = AcesState(nx=10, ny=10, nz=10)
        field = np.ones((5, 5, 5), dtype=np.float64)
        with pytest.raises(ValueError):
            state.add_import_field("temperature", field)


class TestEmissionLayer:
    """Tests for EmissionLayer class."""

    def test_create_emission_layer(self):
        """Test creating an EmissionLayer."""
        from aces import EmissionLayer
        layer = EmissionLayer(field_name="CO")
        assert layer is not None
        assert layer.field_name == "CO"

    def test_emission_layer_validate(self):
        """Test emission layer validation."""
        from aces import EmissionLayer
        layer = EmissionLayer(field_name="CO")
        layer.validate()  # Should not raise

    def test_emission_layer_invalid_field_name(self):
        """Test that empty field name raises ValueError."""
        from aces import EmissionLayer
        layer = EmissionLayer(field_name="")
        with pytest.raises(ValueError):
            layer.validate()


class TestModuleState:
    """Tests for module-level state management."""

    def test_is_initialized_false_initially(self):
        """Test that ACES is not initialized initially."""
        from aces import is_initialized
        assert not is_initialized()

    def test_finalize_when_not_initialized(self):
        """Test that finalize raises error when not initialized."""
        from aces import finalize
        with pytest.raises(RuntimeError):
            finalize()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
