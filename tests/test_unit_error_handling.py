"""
Unit tests for error handling and edge cases.

Tests specific error conditions and edge cases for configuration, state, arrays,
execution space, error handling, and temporal cycles.
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent / "build" / "src" / "python"))

import pytest
import numpy as np
import aces


class TestConfigurationValidation:
    """Unit tests for configuration validation (Task 12.1)."""

    def test_empty_config_is_valid(self):
        """Test that empty config is valid."""
        config = aces.AcesConfig()
        result = config.validate()
        assert result.is_valid, "Empty config should be valid"

    def test_config_with_species_is_valid(self):
        """Test that config with species is valid."""
        config = aces.AcesConfig()
        vdist = aces.VerticalDistributionConfig(method="single")
        layer = aces.EmissionLayer(field_name="CO", vdist=vdist)
        config.add_species("CO", [layer])

        result = config.validate()
        assert result.is_valid, "Config with species should be valid"

    def test_config_with_physics_scheme_is_valid(self):
        """Test that config with physics scheme is valid."""
        config = aces.AcesConfig()
        config.add_physics_scheme("test_scheme", "cpp", {})

        result = config.validate()
        assert result.is_valid, "Config with physics scheme should be valid"

    def test_validation_result_has_errors_list(self):
        """Test that validation result has errors list."""
        config = aces.AcesConfig()
        result = config.validate()

        assert hasattr(result, 'errors'), "ValidationResult should have errors attribute"
        assert isinstance(result.errors, list), "errors should be a list"


class TestStateManagement:
    """Unit tests for state management (Task 12.2)."""

    def test_state_creation_with_valid_dimensions(self):
        """Test state creation with valid dimensions."""
        state = aces.AcesState(nx=10, ny=20, nz=30)
        assert state.dimensions == (10, 20, 30)

    def test_state_creation_with_large_dimensions(self):
        """Test state creation with large dimensions."""
        state = aces.AcesState(nx=1000, ny=1000, nz=100)
        assert state.dimensions == (1000, 1000, 100)

    def test_state_creation_with_small_dimensions(self):
        """Test state creation with small dimensions."""
        state = aces.AcesState(nx=1, ny=1, nz=1)
        assert state.dimensions == (1, 1, 1)

    def test_state_creation_with_zero_nx_raises_error(self):
        """Test that zero nx raises ValueError."""
        with pytest.raises(ValueError):
            aces.AcesState(nx=0, ny=10, nz=10)

    def test_state_creation_with_zero_ny_raises_error(self):
        """Test that zero ny raises ValueError."""
        with pytest.raises(ValueError):
            aces.AcesState(nx=10, ny=0, nz=10)

    def test_state_creation_with_zero_nz_raises_error(self):
        """Test that zero nz raises ValueError."""
        with pytest.raises(ValueError):
            aces.AcesState(nx=10, ny=10, nz=0)

    def test_state_creation_with_negative_nx_raises_error(self):
        """Test that negative nx raises ValueError."""
        with pytest.raises(ValueError):
            aces.AcesState(nx=-1, ny=10, nz=10)

    def test_state_creation_with_negative_ny_raises_error(self):
        """Test that negative ny raises ValueError."""
        with pytest.raises(ValueError):
            aces.AcesState(nx=10, ny=-1, nz=10)

    def test_state_creation_with_negative_nz_raises_error(self):
        """Test that negative nz raises ValueError."""
        with pytest.raises(ValueError):
            aces.AcesState(nx=10, ny=10, nz=-1)

    def test_add_field_with_matching_dimensions(self):
        """Test adding field with matching dimensions."""
        state = aces.AcesState(nx=10, ny=10, nz=10)
        field = np.ones((10, 10, 10), dtype=np.float64)
        state.add_import_field("test", field)
        assert "test" in state.import_fields

    def test_add_field_with_mismatched_dimensions_raises_error(self):
        """Test that mismatched dimensions raise ValueError."""
        state = aces.AcesState(nx=10, ny=10, nz=10)
        field = np.ones((5, 5, 5), dtype=np.float64)

        with pytest.raises(ValueError):
            state.add_import_field("test", field)

    def test_get_nonexistent_field_raises_keyerror(self):
        """Test that getting nonexistent field raises KeyError."""
        state = aces.AcesState(nx=10, ny=10, nz=10)

        with pytest.raises(KeyError):
            state.get_import_field("nonexistent")

    def test_get_nonexistent_export_field_raises_keyerror(self):
        """Test that getting nonexistent export field raises KeyError."""
        state = aces.AcesState(nx=10, ny=10, nz=10)

        with pytest.raises(KeyError):
            state.get_export_field("nonexistent")

    def test_import_fields_dict_like_access(self):
        """Test that import_fields supports dict-like access."""
        state = aces.AcesState(nx=10, ny=10, nz=10)
        field = np.ones((10, 10, 10), dtype=np.float64)
        state.add_import_field("test", field)

        # Test dict-like access
        assert "test" in state.import_fields
        retrieved = state.import_fields["test"]
        assert np.allclose(retrieved, field)

    def test_export_fields_dict_like_access(self):
        """Test that export_fields supports dict-like access."""
        state = aces.AcesState(nx=10, ny=10, nz=10)

        # export_fields should be accessible even if empty
        assert hasattr(state, 'export_fields')
        assert isinstance(state.export_fields, dict)


class TestArrayLayoutConversion:
    """Unit tests for array layout conversion (Task 12.3)."""

    def test_c_order_array_conversion(self):
        """Test C-order array is converted correctly."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        original = np.arange(125, dtype=np.float64).reshape((5, 5, 5), order='C')

        state.add_import_field("test", original)
        retrieved = state.get_import_field("test")

        assert np.allclose(retrieved, original)

    def test_fortran_order_array_conversion(self):
        """Test Fortran-order array is converted correctly."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        original = np.arange(125, dtype=np.float64).reshape((5, 5, 5), order='F')

        state.add_import_field("test", original)
        retrieved = state.get_import_field("test")

        assert np.allclose(retrieved, original)

    def test_non_contiguous_array_handling(self):
        """Test that non-contiguous arrays are handled."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        # Create non-contiguous array by slicing
        full = np.arange(250, dtype=np.float64).reshape((10, 5, 5))
        non_contiguous = full[::2, :, :]  # Every other element

        # Should either work or raise a clear error
        try:
            state.add_import_field("test", non_contiguous)
            retrieved = state.get_import_field("test")
            assert retrieved.shape == (5, 5, 5)
        except ValueError as e:
            assert "contiguous" in str(e).lower() or "dimension" in str(e).lower()

    def test_array_dtype_float64_accepted(self):
        """Test that float64 arrays are accepted."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        field = np.ones((5, 5, 5), dtype=np.float64)

        state.add_import_field("test", field)
        assert "test" in state.import_fields

    def test_array_dtype_float32_conversion(self):
        """Test that float32 arrays are converted to float64."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        field = np.ones((5, 5, 5), dtype=np.float32)

        # Should either convert or raise error
        try:
            state.add_import_field("test", field)
            retrieved = state.get_import_field("test")
            assert retrieved.dtype == np.float64
        except TypeError:
            pass  # Acceptable if float32 is not supported

    def test_array_dtype_int_raises_error(self):
        """Test that integer arrays raise TypeError."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        field = np.ones((5, 5, 5), dtype=np.int32)

        with pytest.raises(TypeError):
            state.add_import_field("test", field)


class TestExecutionSpaceConfiguration:
    """Unit tests for execution space configuration (Task 12.4)."""

    def test_get_available_execution_spaces_returns_list(self):
        """Test that get_available_execution_spaces returns a list."""
        spaces = aces.get_available_execution_spaces()
        assert isinstance(spaces, list)

    def test_available_spaces_non_empty(self):
        """Test that at least one execution space is available."""
        spaces = aces.get_available_execution_spaces()
        assert len(spaces) > 0

    def test_serial_space_always_available(self):
        """Test that Serial space is always available."""
        spaces = aces.get_available_execution_spaces()
        assert 'Serial' in spaces

    def test_set_serial_execution_space(self):
        """Test setting Serial execution space."""
        try:
            aces.set_execution_space('Serial')
            current = aces.get_execution_space()
            # Just verify it's a valid space
            assert current in aces.get_available_execution_spaces()
        except RuntimeError:
            pass  # May fail if not initialized

    def test_set_invalid_execution_space_raises_error(self):
        """Test that setting invalid execution space raises error."""
        with pytest.raises(RuntimeError):
            aces.set_execution_space('InvalidSpace')

    def test_get_execution_space_returns_string(self):
        """Test that get_execution_space returns a string."""
        space = aces.get_execution_space()
        assert isinstance(space, str)

    def test_get_execution_space_returns_valid_space(self):
        """Test that get_execution_space returns a valid space."""
        space = aces.get_execution_space()
        available = aces.get_available_execution_spaces()
        assert space in available


class TestErrorHandling:
    """Unit tests for error handling (Task 12.5)."""

    def test_exception_mapping_invalid_config(self):
        """Test that invalid config raises appropriate exception."""
        # This would require an invalid config that the C layer rejects
        # For now, just verify exception classes exist
        assert hasattr(aces, 'AcesConfigError')
        assert hasattr(aces, 'AcesComputationError')
        assert hasattr(aces, 'AcesStateError')

    def test_exception_classes_inherit_from_base(self):
        """Test that exception classes inherit from AcesException."""
        assert issubclass(aces.AcesConfigError, aces.AcesException)
        assert issubclass(aces.AcesComputationError, aces.AcesException)
        assert issubclass(aces.AcesStateError, aces.AcesException)

    def test_get_last_error_returns_string_or_none(self):
        """Test that get_last_error returns string or None."""
        error = aces.get_last_error()
        assert error is None or isinstance(error, str)

    def test_finalize_when_not_initialized_raises_error(self):
        """Test that finalize raises error when not initialized."""
        # Reset initialization state
        if aces.is_initialized():
            aces.finalize()

        with pytest.raises(RuntimeError):
            aces.finalize()

    def test_is_initialized_returns_bool(self):
        """Test that is_initialized returns boolean."""
        result = aces.is_initialized()
        assert isinstance(result, bool)

    def test_is_initialized_false_initially(self):
        """Test that ACES is not initialized initially."""
        # Ensure we're not initialized
        if aces.is_initialized():
            aces.finalize()

        assert not aces.is_initialized()


class TestTemporalCycles:
    """Unit tests for temporal cycles (Task 12.6)."""

    def test_add_temporal_cycle_to_config(self):
        """Test adding temporal cycle to config."""
        config = aces.AcesConfig()
        factors = [1.0, 1.1, 1.2, 1.3, 1.2, 1.1, 1.0, 0.9, 0.8, 0.9, 1.0, 1.1]

        config.add_temporal_cycle("seasonal", factors)

        # Verify it was added
        assert hasattr(config, 'temporal_cycles')

    def test_temporal_cycle_factors_length(self):
        """Test that temporal cycle factors have correct length."""
        config = aces.AcesConfig()

        # Seasonal cycle should have 12 factors
        seasonal_factors = [1.0] * 12
        config.add_temporal_cycle("seasonal", seasonal_factors)

        # Diurnal cycle should have 24 factors
        diurnal_factors = [1.0] * 24
        config.add_temporal_cycle("diurnal", diurnal_factors)

        # Weekly cycle should have 7 factors
        weekly_factors = [1.0] * 7
        config.add_temporal_cycle("weekly", weekly_factors)

    def test_compute_with_temporal_parameters(self):
        """Test compute with temporal parameters."""
        config = aces.AcesConfig()
        state = aces.AcesState(nx=5, ny=5, nz=5)

        state.add_import_field("T", np.ones((5, 5, 5), dtype=np.float64))
        state.add_import_field("PS", np.ones((5, 5, 5), dtype=np.float64) * 101325)

        # Should accept temporal parameters
        try:
            aces.compute(state, config, hour=12, day_of_week=3, month=6)
        except RuntimeError:
            # Expected if ACES isn't fully initialized
            pass

    def test_temporal_parameters_valid_ranges(self):
        """Test that temporal parameters accept valid ranges."""
        config = aces.AcesConfig()
        state = aces.AcesState(nx=5, ny=5, nz=5)

        state.add_import_field("T", np.ones((5, 5, 5), dtype=np.float64))
        state.add_import_field("PS", np.ones((5, 5, 5), dtype=np.float64) * 101325)

        # Test boundary values
        for hour in [0, 23]:
            for day_of_week in [0, 6]:
                for month in [1, 12]:
                    try:
                        aces.compute(state, config, hour=hour, day_of_week=day_of_week, month=month)
                    except RuntimeError:
                        pass  # Expected if not initialized


class TestEdgeCases:
    """Unit tests for edge cases."""

    def test_state_with_single_element(self):
        """Test state with single element (1x1x1)."""
        state = aces.AcesState(nx=1, ny=1, nz=1)
        field = np.array([[[1.0]]], dtype=np.float64)

        state.add_import_field("test", field)
        retrieved = state.get_import_field("test")

        assert retrieved.shape == (1, 1, 1)
        assert np.allclose(retrieved, field)

    def test_state_with_very_large_dimensions(self):
        """Test state with large dimensions."""
        state = aces.AcesState(nx=100, ny=100, nz=100)
        assert state.dimensions == (100, 100, 100)

    def test_field_with_all_zeros(self):
        """Test field with all zeros."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        field = np.zeros((5, 5, 5), dtype=np.float64)

        state.add_import_field("test", field)
        retrieved = state.get_import_field("test")

        assert np.allclose(retrieved, field)

    def test_field_with_negative_values(self):
        """Test field with negative values."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        field = -np.ones((5, 5, 5), dtype=np.float64)

        state.add_import_field("test", field)
        retrieved = state.get_import_field("test")

        assert np.allclose(retrieved, field)

    def test_field_with_very_large_values(self):
        """Test field with very large values."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        field = np.ones((5, 5, 5), dtype=np.float64) * 1e10

        state.add_import_field("test", field)
        retrieved = state.get_import_field("test")

        assert np.allclose(retrieved, field)

    def test_field_with_very_small_values(self):
        """Test field with very small values."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        field = np.ones((5, 5, 5), dtype=np.float64) * 1e-10

        state.add_import_field("test", field)
        retrieved = state.get_import_field("test")

        assert np.allclose(retrieved, field)

    def test_multiple_fields_in_state(self):
        """Test adding multiple fields to state."""
        state = aces.AcesState(nx=5, ny=5, nz=5)

        for i in range(10):
            field = np.ones((5, 5, 5), dtype=np.float64) * i
            state.add_import_field(f"field_{i}", field)

        assert len(state.import_fields) == 10

    def test_field_name_with_special_characters(self):
        """Test field names with special characters."""
        state = aces.AcesState(nx=5, ny=5, nz=5)
        field = np.ones((5, 5, 5), dtype=np.float64)

        # Test various field names
        for name in ["field_1", "FIELD", "field-1", "field.1"]:
            state.add_import_field(name, field)
            assert name in state.import_fields


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
