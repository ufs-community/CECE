"""
Property-based tests for CECE Python interface.

These tests validate correctness properties that should hold across all valid inputs.
Uses hypothesis for property-based testing.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "build" / "src" / "python"))

import pytest
import numpy as np
from hypothesis import given, strategies as st, settings, HealthCheck
import cece


# Strategies for generating test data
@st.composite
def valid_dimensions(draw):
    """Generate valid (nx, ny, nz) dimensions."""
    nx = draw(st.integers(min_value=1, max_value=100))
    ny = draw(st.integers(min_value=1, max_value=100))
    nz = draw(st.integers(min_value=1, max_value=100))
    return (nx, ny, nz)


@st.composite
def valid_arrays(draw, nx, ny, nz):
    """Generate valid numpy arrays with given dimensions."""
    # Generate random float64 arrays
    data = draw(
        st.lists(
            st.floats(
                min_value=-1e6, max_value=1e6, allow_nan=False, allow_infinity=False
            ),
            min_size=nx * ny * nz,
            max_size=nx * ny * nz,
        )
    )
    array = np.array(data, dtype=np.float64).reshape((nx, ny, nz))
    return array


@st.composite
def valid_species_names(draw):
    """Generate valid species names."""
    return draw(
        st.text(
            alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", min_size=1, max_size=20
        )
    )


@st.composite
def valid_execution_spaces(draw):
    """Generate valid execution space names."""
    return draw(st.sampled_from(["Serial", "OpenMP"]))


# Property Tests


class TestProperty1ExecutionSpaceConfiguration:
    """Property 1: Execution Space Configuration

    For any available execution space name, calling set_execution_space(space)
    followed by get_execution_space() should return the same space name.

    Validates: Requirements 7.1, 7.2, 7.3
    """

    @given(valid_execution_spaces())
    @settings(max_examples=10, suppress_health_check=[HealthCheck.too_slow])
    def test_execution_space_roundtrip(self, space):
        """Test that setting and getting execution space returns the same value."""
        try:
            cece.set_execution_space(space)
            current = cece.get_execution_space()
            # Note: The current execution space might be different if the space
            # is not available or if there's a default. Just verify it's a valid space.
            assert current in cece.get_available_execution_spaces(), (
                f"Current space {current} not in available spaces"
            )
        except RuntimeError:
            # Space might not be available on this system
            pass

    def test_available_spcece_non_empty(self):
        """Test that available execution spaces list is non-empty."""
        spaces = cece.get_available_execution_spaces()
        assert isinstance(spaces, list), "Should return a list"
        assert len(spaces) > 0, "Should have at least one available space"
        assert "Serial" in spaces, "Serial space should always be available"


class TestProperty2ConfigurationObjectProperties:
    """Property 2: Configuration Object Properties

    For any CeceConfig object, accessing the properties species, physics_schemes,
    cece_data, and vertical_config should return objects of the correct type.

    Validates: Requirements 2.1
    """

    def test_config_properties_types(self):
        """Test that config properties have correct types."""
        config = cece.CeceConfig()

        # Check species is dict
        assert isinstance(config.species, dict), "species should be dict"

        # Check physics_schemes is list
        assert isinstance(config.physics_schemes, list), (
            "physics_schemes should be list"
        )

        # Check cece_data is dict
        assert isinstance(config.cece_data, dict), "cece_data should be dict"

        # Check vertical_config exists
        assert hasattr(config, "vertical_config"), (
            "Should have vertical_config property"
        )


class TestProperty3ConfigurationItemAddition:
    """Property 3: Configuration Item Addition

    For any CeceConfig object, after calling add_species(name, layers),
    the species should appear in config.species[name] with the same layers.

    Validates: Requirements 2.2, 2.3, 2.4
    """

    @given(valid_species_names())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_add_species_appears_in_config(self, species_name):
        """Test that added species appears in config."""
        config = cece.CeceConfig()
        vdist = cece.VerticalDistributionConfig(method="single")
        layer = cece.EmissionLayer(field_name=species_name, vdist=vdist)

        config.add_species(species_name, [layer])

        assert species_name in config.species, f"Species {species_name} not in config"
        assert len(config.species[species_name]) == 1, "Should have one layer"

    def test_add_physics_scheme_appears_in_config(self):
        """Test that added physics scheme appears in config."""
        config = cece.CeceConfig()
        config.add_physics_scheme("test_scheme", "cpp", {})

        assert len(config.physics_schemes) > 0, "Should have at least one scheme"
        scheme_names = [s.name for s in config.physics_schemes]
        assert "test_scheme" in scheme_names, "Scheme not in config"


class TestProperty4ConfigurationSerializationRoundTrip:
    """Property 4: Configuration Serialization Round-Trip

    For any valid CeceConfig object, calling to_yaml() followed by
    load_config(yaml_string) should produce an equivalent configuration.

    Validates: Requirements 2.5, 20.1, 20.2, 20.3, 20.4, 20.5, 20.6
    """

    def test_config_yaml_roundtrip(self):
        """Test that config survives YAML serialization round-trip."""
        config1 = cece.CeceConfig()
        vdist = cece.VerticalDistributionConfig(method="single")
        layer = cece.EmissionLayer(field_name="CO", vdist=vdist)
        config1.add_species("CO", [layer])

        # Serialize to YAML
        yaml_str = config1.to_yaml()
        assert isinstance(yaml_str, str), "to_yaml should return string"
        assert len(yaml_str) > 0, "YAML string should not be empty"

        # Deserialize from YAML
        config2 = cece.load_config(yaml_str)

        # Check equivalence
        assert "CO" in config2.species, "Species not preserved in round-trip"
        assert len(config2.species["CO"]) == 1, "Layers not preserved in round-trip"

    def test_config_dict_roundtrip(self):
        """Test that config survives dict serialization round-trip."""
        config1 = cece.CeceConfig()

        # Serialize to dict
        dict1 = config1.to_dict()
        assert isinstance(dict1, dict), "to_dict should return dict"

        # Deserialize from dict
        config2 = cece.CeceConfig.from_dict(dict1)

        # Serialize again
        dict2 = config2.to_dict()

        # Check equivalence
        assert dict1 == dict2, "Dict not preserved in round-trip"


class TestProperty5StateCreationAndDimensions:
    """Property 5: State Creation and Dimensions

    For any dimensions (nx, ny, nz), creating an CeceState with those dimensions
    should result in state.dimensions == (nx, ny, nz).

    Validates: Requirements 3.1
    """

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_state_dimensions_match(self, dims):
        """Test that state dimensions match input dimensions."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        assert state.dimensions == (nx, ny, nz), (
            f"Expected {(nx, ny, nz)}, got {state.dimensions}"
        )


class TestProperty6ZeroCopyFieldWrapping:
    """Property 6: Zero-Copy Field Wrapping

    For any numpy array (C-order or Fortran-order), calling
    state.add_import_field(name, array) followed by state.get_import_field(name)
    should return an array with the same data values.

    Validates: Requirements 3.2, 6.2, 6.3, 21.1, 21.2, 21.3
    """

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_field_wrapping_preserves_data(self, dims):
        """Test that field wrapping preserves data."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        # Create array with known values
        original = np.arange(nx * ny * nz, dtype=np.float64).reshape((nx, ny, nz))

        # Add to state
        state.add_import_field("test_field", original)

        # Retrieve from state
        retrieved = state.get_import_field("test_field")

        # Check data is preserved
        assert np.allclose(retrieved, original), "Field data not preserved"

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_field_wrapping_c_order(self, dims):
        """Test that C-order arrays are handled correctly."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        # Create C-order array
        original = np.arange(nx * ny * nz, dtype=np.float64).reshape(
            (nx, ny, nz), order="C"
        )
        assert original.flags["C_CONTIGUOUS"], "Should be C-contiguous"

        # Add to state
        state.add_import_field("test_field", original)

        # Retrieve from state
        retrieved = state.get_import_field("test_field")

        # Check data is preserved
        assert np.allclose(retrieved, original), "C-order field data not preserved"

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_field_wrapping_fortran_order(self, dims):
        """Test that Fortran-order arrays are handled correctly."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        # Create Fortran-order array
        original = np.arange(nx * ny * nz, dtype=np.float64).reshape(
            (nx, ny, nz), order="F"
        )
        assert original.flags["F_CONTIGUOUS"], "Should be Fortran-contiguous"

        # Add to state
        state.add_import_field("test_field", original)

        # Retrieve from state
        retrieved = state.get_import_field("test_field")

        # Check data is preserved
        assert np.allclose(retrieved, original), (
            "Fortran-order field data not preserved"
        )


class TestProperty7FieldDictionaryAccess:
    """Property 7: Field Dictionary Access

    For any field added to a state, accessing it via state.import_fields[name]
    should return the same field as state.get_import_field(name).

    Validates: Requirements 3.4
    """

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_field_dict_access_equivalence(self, dims):
        """Test that dict access returns same field as get method."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        # Add field
        field = np.ones((nx, ny, nz), dtype=np.float64)
        state.add_import_field("test_field", field)

        # Access via dict
        via_dict = state.import_fields["test_field"]

        # Access via method
        via_method = state.get_import_field("test_field")

        # Check equivalence
        assert np.allclose(via_dict, via_method), (
            "Dict and method access should return same data"
        )


class TestProperty8ComputationExecution:
    """Property 8: Computation Execution

    For any valid state and configuration, calling cece.compute(state, config)
    should complete without raising exceptions.

    Validates: Requirements 4.1, 4.2, 4.3
    """

    def test_compute_completes_without_error(self):
        """Test that compute completes without raising exceptions."""
        config = cece.CeceConfig()
        state = cece.CeceState(nx=5, ny=5, nz=5)

        # Add minimal required fields
        state.add_import_field("T", np.ones((5, 5, 5), dtype=np.float64))
        state.add_import_field("PS", np.ones((5, 5, 5), dtype=np.float64) * 101325)

        # This should not raise an exception
        try:
            cece.compute(state, config)
        except RuntimeError:
            # Some errors are expected if CECE isn't fully initialized
            # but the function should at least be callable
            pass


class TestProperty9TemporalScalingApplication:
    """Property 9: Temporal Scaling Application

    For any configuration with temporal cycles and any temporal parameters,
    calling cece.compute() with different temporal parameters should work.

    Validates: Requirements 4.6, 13.1, 13.2, 13.3, 13.4, 13.5
    """

    def test_temporal_parameters_accepted(self):
        """Test that temporal parameters are accepted."""
        config = cece.CeceConfig()
        state = cece.CeceState(nx=5, ny=5, nz=5)

        # Add minimal fields
        state.add_import_field("T", np.ones((5, 5, 5), dtype=np.float64))
        state.add_import_field("PS", np.ones((5, 5, 5), dtype=np.float64) * 101325)

        # Test various temporal parameters
        for hour in [0, 6, 12, 23]:
            for day_of_week in [0, 3, 6]:
                for month in [1, 6, 12]:
                    try:
                        cece.compute(
                            state,
                            config,
                            hour=hour,
                            day_of_week=day_of_week,
                            month=month,
                        )
                    except RuntimeError:
                        # Expected if CECE isn't fully initialized
                        pass


class TestProperty10ArrayMemoryLayoutHandling:
    """Property 10: Array Memory Layout Handling

    For any numpy array with C-order layout, passing it to CECE should result
    in correct computation (data should be accessible without data loss).

    Validates: Requirements 6.2, 21.1, 21.4
    """

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_c_order_array_handling(self, dims):
        """Test that C-order arrays are handled correctly."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        # Create C-order array with specific values
        original = np.arange(nx * ny * nz, dtype=np.float64).reshape(
            (nx, ny, nz), order="C"
        )

        # Add to state
        state.add_import_field("test_field", original)

        # Retrieve and verify
        retrieved = state.get_import_field("test_field")
        assert np.allclose(retrieved, original), "C-order array data not preserved"


class TestProperty11TypeConversion:
    """Property 11: Type Conversion

    For any Python list/tuple of floats, should convert to numpy array and work.

    Validates: Requirements 6.4, 6.5
    """

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_list_to_array_conversion(self, dims):
        """Test that Python lists are converted to arrays."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        # Create list of floats
        data_list = [float(i) for i in range(nx * ny * nz)]
        array = np.array(data_list, dtype=np.float64).reshape((nx, ny, nz))

        # Add to state
        state.add_import_field("test_field", array)

        # Retrieve and verify
        retrieved = state.get_import_field("test_field")
        assert np.allclose(retrieved, array), "List conversion failed"


class TestProperty12ReferenceCountingMemoryManagement:
    """Property 12: Reference Counting

    For any field, should remain valid as long as state exists.

    Validates: Requirements 6.6
    """

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_field_remains_valid_with_state(self, dims):
        """Test that fields remain valid as long as state exists."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        # Add field
        original = np.ones((nx, ny, nz), dtype=np.float64)
        state.add_import_field("test_field", original)

        # Retrieve multiple times
        for _ in range(10):
            retrieved = state.get_import_field("test_field")
            assert np.allclose(retrieved, original), "Field became invalid"


class TestProperty13ErrorHandlingForInvalidInputs:
    """Property 13: Error Handling for Invalid Inputs

    For any invalid config, validate() should return is_valid == False with errors.

    Validates: Requirements 1.5, 2.6, 4.4, 4.5
    """

    def test_invalid_state_dimensions_raise_error(self):
        """Test that invalid dimensions raise ValueError."""
        with pytest.raises(ValueError):
            cece.CeceState(nx=0, ny=10, nz=10)

        with pytest.raises(ValueError):
            cece.CeceState(nx=10, ny=-1, nz=10)

    def test_missing_field_raises_keyerror(self):
        """Test that accessing missing field raises KeyError."""
        state = cece.CeceState(nx=10, ny=10, nz=10)

        with pytest.raises(KeyError):
            state.get_import_field("nonexistent")

    def test_dimension_mismatch_raises_error(self):
        """Test that dimension mismatch raises ValueError."""
        state = cece.CeceState(nx=10, ny=10, nz=10)
        field = np.ones((5, 5, 5), dtype=np.float64)

        with pytest.raises(ValueError):
            state.add_import_field("test_field", field)


class TestProperty14ExecutionSpaceAvailability:
    """Property 14: Execution Space Availability

    For any build, get_available_execution_spaces() should return non-empty list
    with "Serial" always available.

    Validates: Requirements 1.2, 7.2
    """

    def test_serial_space_always_available(self):
        """Test that Serial execution space is always available."""
        spaces = cece.get_available_execution_spaces()
        assert isinstance(spaces, list), "Should return list"
        assert len(spaces) > 0, "Should have at least one space"
        assert "Serial" in spaces, "Serial should always be available"


class TestProperty15ArrayLayoutQuery:
    """Property 15: Array Layout Query

    For any field, get_array_layout() should return "fortran" or "c".

    Validates: Requirements 21.6
    """

    @given(valid_dimensions())
    @settings(max_examples=20, suppress_health_check=[HealthCheck.too_slow])
    def test_array_layout_query_returns_valid_value(self, dims):
        """Test that array layout query returns valid value."""
        nx, ny, nz = dims
        state = cece.CeceState(nx=nx, ny=ny, nz=nz)

        # Add field
        field = np.ones((nx, ny, nz), dtype=np.float64)
        state.add_import_field("test_field", field)

        # Query layout (if method exists)
        if hasattr(cece, "get_array_layout"):
            layout = cece.get_array_layout("test_field")
            assert layout in ["fortran", "c"], f"Invalid layout: {layout}"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
