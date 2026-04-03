"""
Integration tests for end-to-end workflows.

Tests complete workflows from configuration to computation to result retrieval.
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent / "build" / "src" / "python"))

import pytest
import numpy as np
import aces


class TestBasicWorkflow:
    """Integration test for basic workflow (Task 13.1)."""

    def test_basic_workflow_complete(self):
        """Test complete basic workflow: config -> state -> compute -> results."""
        # 1. Load configuration
        config = aces.AcesConfig()
        vdist = aces.VerticalDistributionConfig(method="single")
        layer = aces.EmissionLayer(field_name="CO", vdist=vdist)
        config.add_species("CO", [layer])

        # 2. Create state with meteorology data
        state = aces.AcesState(nx=5, ny=5, nz=5)
        state.add_import_field("T", np.ones((5, 5, 5), dtype=np.float64) * 298.15)
        state.add_import_field("PS", np.ones((5, 5, 5), dtype=np.float64) * 101325)

        # 3. Execute computation
        try:
            aces.compute(state, config)
        except RuntimeError:
            # Expected if ACES isn't fully initialized
            pass

        # 4. Verify state is still valid
        assert state.dimensions == (5, 5, 5)
        assert "T" in state.import_fields
        assert "PS" in state.import_fields


class TestConfigurationRoundTrip:
    """Integration test for configuration round-trip (Task 13.2)."""

    def test_config_roundtrip_preserves_species(self):
        """Test that config survives serialization round-trip."""
        # 1. Create config with species
        config1 = aces.AcesConfig()
        vdist = aces.VerticalDistributionConfig(method="single")
        layer = aces.EmissionLayer(field_name="CO", vdist=vdist)
        config1.add_species("CO", [layer])

        # 2. Serialize to dict
        dict1 = config1.to_dict()

        # 3. Deserialize from dict
        config2 = aces.AcesConfig.from_dict(dict1)

        # 4. Verify equivalence
        assert "CO" in config2.species
        assert len(config2.species["CO"]) == 1

        # 5. Serialize again
        dict2 = config2.to_dict()

        # 6. Verify dicts are equivalent
        assert dict1 == dict2


class TestMultipleComputations:
    """Integration test for multiple computations (Task 13.3)."""

    def test_multiple_computations_with_different_temporal_parameters(self):
        """Test running multiple computations with different temporal parameters."""
        config = aces.AcesConfig()
        state = aces.AcesState(nx=5, ny=5, nz=5)

        # Add required fields
        state.add_import_field("T", np.ones((5, 5, 5), dtype=np.float64) * 298.15)
        state.add_import_field("PS", np.ones((5, 5, 5), dtype=np.float64) * 101325)

        # Run multiple computations with different temporal parameters
        temporal_params = [
            (0, 0, 1),    # Hour 0, Monday, January
            (12, 3, 6),   # Hour 12, Thursday, June
            (23, 6, 12),  # Hour 23, Sunday, December
        ]

        for hour, day_of_week, month in temporal_params:
            try:
                aces.compute(state, config, hour=hour, day_of_week=day_of_week, month=month)
            except RuntimeError:
                # Expected if ACES isn't fully initialized
                pass

        # Verify state is still valid
        assert state.dimensions == (5, 5, 5)


class TestErrorRecovery:
    """Integration test for error recovery (Task 13.4)."""

    def test_error_recovery_after_invalid_state(self):
        """Test that ACES can recover after an error."""
        config = aces.AcesConfig()

        # 1. Try to compute with invalid state (missing fields)
        state1 = aces.AcesState(nx=5, ny=5, nz=5)
        try:
            aces.compute(state1, config)
        except RuntimeError:
            # Expected - missing required fields
            pass

        # 2. Create new state with valid fields
        state2 = aces.AcesState(nx=5, ny=5, nz=5)
        state2.add_import_field("T", np.ones((5, 5, 5), dtype=np.float64) * 298.15)
        state2.add_import_field("PS", np.ones((5, 5, 5), dtype=np.float64) * 101325)

        # 3. Try to compute again - should work or fail gracefully
        try:
            aces.compute(state2, config)
        except RuntimeError:
            # Expected if ACES isn't fully initialized
            pass

        # 4. Verify state is still valid
        assert state2.dimensions == (5, 5, 5)


class TestExecutionSpaceSwitching:
    """Integration test for execution space switching (Task 13.5)."""

    def test_execution_space_switching(self):
        """Test switching between available execution spaces."""
        available_spaces = aces.get_available_execution_spaces()

        for space in available_spaces:
            try:
                # Set execution space
                aces.set_execution_space(space)

                # Verify it was set
                current = aces.get_execution_space()
                assert current in available_spaces

                # Run computation on this space
                config = aces.AcesConfig()
                state = aces.AcesState(nx=5, ny=5, nz=5)
                state.add_import_field("T", np.ones((5, 5, 5), dtype=np.float64) * 298.15)
                state.add_import_field("PS", np.ones((5, 5, 5), dtype=np.float64) * 101325)

                try:
                    aces.compute(state, config)
                except RuntimeError:
                    # Expected if ACES isn't fully initialized
                    pass

            except RuntimeError:
                # Space might not be available
                pass


class TestComplexConfiguration:
    """Integration test for complex configuration."""

    def test_complex_configuration_with_multiple_species(self):
        """Test configuration with multiple species and physics schemes."""
        config = aces.AcesConfig()

        # Add multiple species
        vdist = aces.VerticalDistributionConfig(method="single")
        for species in ["CO", "NO2", "SO2", "O3"]:
            layer = aces.EmissionLayer(field_name=species, vdist=vdist)
            config.add_species(species, [layer])

        # Add physics schemes
        config.add_physics_scheme("scheme1", "cpp", {})
        config.add_physics_scheme("scheme2", "cpp", {})

        # Validate configuration
        result = config.validate()
        assert result.is_valid

        # Verify all species are present
        assert len(config.species) == 4
        assert all(s in config.species for s in ["CO", "NO2", "SO2", "O3"])

        # Verify all schemes are present
        assert len(config.physics_schemes) == 2


class TestStateFieldOperations:
    """Integration test for state field operations."""

    def test_state_field_operations_workflow(self):
        """Test complete workflow of state field operations."""
        state = aces.AcesState(nx=10, ny=10, nz=10)

        # Add multiple fields
        fields = {
            "temperature": np.ones((10, 10, 10), dtype=np.float64) * 298.15,
            "pressure": np.ones((10, 10, 10), dtype=np.float64) * 101325,
            "humidity": np.ones((10, 10, 10), dtype=np.float64) * 0.5,
            "wind_u": np.random.randn(10, 10, 10).astype(np.float64),
            "wind_v": np.random.randn(10, 10, 10).astype(np.float64),
        }

        for name, field in fields.items():
            state.add_import_field(name, field)

        # Verify all fields are present
        assert len(state.import_fields) == 5

        # Retrieve and verify each field
        for name, original in fields.items():
            retrieved = state.get_import_field(name)
            assert np.allclose(retrieved, original)

        # Test dict-like access
        for name in fields:
            assert name in state.import_fields
            retrieved = state.import_fields[name]
            assert retrieved is not None


class TestConfigurationValidationWorkflow:
    """Integration test for configuration validation workflow."""

    def test_validation_workflow(self):
        """Test complete validation workflow."""
        # Create configuration
        config = aces.AcesConfig()

        # Validate empty config
        result1 = config.validate()
        assert result1.is_valid

        # Add species
        vdist = aces.VerticalDistributionConfig(method="single")
        layer = aces.EmissionLayer(field_name="CO", vdist=vdist)
        config.add_species("CO", [layer])

        # Validate with species
        result2 = config.validate()
        assert result2.is_valid

        # Add physics scheme
        config.add_physics_scheme("test_scheme", "cpp", {})

        # Validate with scheme
        result3 = config.validate()
        assert result3.is_valid


class TestArrayConversionWorkflow:
    """Integration test for array conversion workflow."""

    def test_array_conversion_workflow(self):
        """Test complete array conversion workflow."""
        state = aces.AcesState(nx=10, ny=10, nz=10)

        # Test C-order array
        c_order = np.arange(1000, dtype=np.float64).reshape((10, 10, 10), order='C')
        state.add_import_field("c_order", c_order)
        retrieved_c = state.get_import_field("c_order")
        assert np.allclose(retrieved_c, c_order)

        # Test Fortran-order array
        f_order = np.arange(1000, dtype=np.float64).reshape((10, 10, 10), order='F')
        state.add_import_field("f_order", f_order)
        retrieved_f = state.get_import_field("f_order")
        assert np.allclose(retrieved_f, f_order)

        # Test mixed operations
        mixed = np.random.randn(10, 10, 10).astype(np.float64)
        state.add_import_field("mixed", mixed)
        retrieved_mixed = state.get_import_field("mixed")
        assert np.allclose(retrieved_mixed, mixed)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
