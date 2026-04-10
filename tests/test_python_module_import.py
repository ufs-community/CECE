"""
Tests for Python module import and basic functionality.

Tests that the cece module can be imported and basic classes are available.
"""

import pytest
import sys
from pathlib import Path

# Add src/python to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "src" / "python"))


class TestModuleImport:
    """Tests for module import."""

    def test_import_cece_module(self):
        """Test that cece module can be imported."""
        import cece
        assert cece is not None

    def test_import_config_class(self):
        """Test that CeceConfig can be imported."""
        from cece import CeceConfig
        assert CeceConfig is not None

    def test_import_state_class(self):
        """Test that CeceState can be imported."""
        from cece import CeceState
        assert CeceState is not None

    def test_import_field_class(self):
        """Test that CeceField can be imported."""
        from cece import CeceField
        assert CeceField is not None

    def test_import_emission_layer_class(self):
        """Test that EmissionLayer can be imported."""
        from cece import EmissionLayer
        assert EmissionLayer is not None

    def test_import_exceptions(self):
        """Test that exception classes can be imported."""
        from cece import (
            CeceException,
            CeceConfigError,
            CeceComputationError,
            CeceStateError,
            CeceExecutionSpaceError,
        )
        assert CeceException is not None
        assert CeceConfigError is not None
        assert CeceComputationError is not None
        assert CeceStateError is not None
        assert CeceExecutionSpaceError is not None

    def test_import_functions(self):
        """Test that module-level functions can be imported."""
        from cece import (
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
    """Tests for CeceConfig class."""

    def test_create_empty_config(self):
        """Test creating an empty CeceConfig."""
        from cece import CeceConfig
        config = CeceConfig()
        assert config is not None
        assert config.species == {}
        assert config.physics_schemes == []

    def test_config_add_species(self):
        """Test adding a species to config."""
        from cece import CeceConfig, EmissionLayer, VerticalDistributionConfig
        config = CeceConfig()
        vdist = VerticalDistributionConfig(method="single")
        layer = EmissionLayer(field_name="CO", vdist=vdist)
        config.add_species("CO", [layer])
        assert "CO" in config.species
        assert len(config.species["CO"]) == 1

    def test_config_validate(self):
        """Test config validation."""
        from cece import CeceConfig
        config = CeceConfig()
        result = config.validate()
        assert result.is_valid

    def test_config_to_dict(self):
        """Test config serialization to dict."""
        from cece import CeceConfig
        config = CeceConfig()
        config_dict = config.to_dict()
        assert isinstance(config_dict, dict)
        assert "species" in config_dict
        assert "physics_schemes" in config_dict

    def test_config_from_dict(self):
        """Test config deserialization from dict."""
        from cece import CeceConfig
        config_dict = {
            "species": {},
            "physics_schemes": [],
            "cece_data": {"streams": []},
            "temporal_cycles": {},
        }
        config = CeceConfig.from_dict(config_dict)
        assert config is not None
        assert config.species == {}


class TestStateClass:
    """Tests for CeceState class."""

    def test_create_state(self):
        """Test creating an CeceState."""
        from cece import CeceState
        state = CeceState(nx=10, ny=10, nz=10)
        assert state is not None
        assert state.dimensions == (10, 10, 10)

    def test_state_invalid_dimensions(self):
        """Test that invalid dimensions raise ValueError."""
        from cece import CeceState
        with pytest.raises(ValueError):
            CeceState(nx=0, ny=10, nz=10)

    def test_state_add_import_field(self):
        """Test adding an import field to state."""
        import numpy as np
        from cece import CeceState
        state = CeceState(nx=10, ny=10, nz=10)
        field = np.ones((10, 10, 10), dtype=np.float64)
        state.add_import_field("temperature", field)
        assert "temperature" in state.import_fields

    def test_state_get_import_field(self):
        """Test retrieving an import field from state."""
        import numpy as np
        from cece import CeceState
        state = CeceState(nx=10, ny=10, nz=10)
        field = np.ones((10, 10, 10), dtype=np.float64)
        state.add_import_field("temperature", field)
        retrieved = state.get_import_field("temperature")
        assert np.allclose(retrieved, field)

    def test_state_field_not_found(self):
        """Test that accessing non-existent field raises KeyError."""
        from cece import CeceState
        state = CeceState(nx=10, ny=10, nz=10)
        with pytest.raises(KeyError):
            state.get_import_field("nonexistent")

    def test_state_dimension_mismatch(self):
        """Test that mismatched dimensions raise ValueError."""
        import numpy as np
        from cece import CeceState
        state = CeceState(nx=10, ny=10, nz=10)
        field = np.ones((5, 5, 5), dtype=np.float64)
        with pytest.raises(ValueError):
            state.add_import_field("temperature", field)


class TestEmissionLayer:
    """Tests for EmissionLayer class."""

    def test_create_emission_layer(self):
        """Test creating an EmissionLayer."""
        from cece import EmissionLayer
        layer = EmissionLayer(field_name="CO")
        assert layer is not None
        assert layer.field_name == "CO"

    def test_emission_layer_validate(self):
        """Test emission layer validation."""
        from cece import EmissionLayer
        layer = EmissionLayer(field_name="CO")
        layer.validate()  # Should not raise

    def test_emission_layer_invalid_field_name(self):
        """Test that empty field name raises ValueError."""
        from cece import EmissionLayer
        layer = EmissionLayer(field_name="")
        with pytest.raises(ValueError):
            layer.validate()


class TestModuleState:
    """Tests for module-level state management."""

    def test_is_initialized_false_initially(self):
        """Test that CECE is not initialized initially."""
        from cece import is_initialized
        assert not is_initialized()

    def test_finalize_when_not_initialized(self):
        """Test that finalize raises error when not initialized."""
        from cece import finalize
        with pytest.raises(RuntimeError):
            finalize()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
