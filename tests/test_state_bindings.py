"""
Tests for pybind11 state and zero-copy NumPy bindings (_cece_core module).

Tests CeceImportState.set_field, CeceExportState.get_field, CeceStateResolver,
and zero-copy behavior.
"""

import os
import sys
import pytest
import numpy as np

# Add the build output directory to the path so _cece_core can be imported
sys.path.insert(
    0, os.path.join(os.path.dirname(__file__), "..", "build", "src", "python", "cece")
)

import _cece_core


@pytest.fixture(scope="session", autouse=True)
def kokkos_runtime():
    """Initialize Kokkos once for the entire test session."""
    _cece_core.initialize_kokkos()
    yield
    # Do NOT finalize Kokkos here: DualViews created during tests may still
    # be alive and their destructors would crash after finalize.


class TestCeceImportStateSetField:
    """Tests for CeceImportState.set_field method."""

    def test_set_field_fortran_contiguous(self):
        """Test set_field with Fortran-contiguous float64 array."""
        state = _cece_core.CeceImportState()
        arr = np.asfortranarray(np.ones((4, 5, 3), dtype=np.float64))
        state.set_field("temperature", arr)
        names = state.get_field_names()
        assert "temperature" in names

    def test_set_field_c_contiguous(self):
        """Test set_field with C-contiguous array (verify conversion happens)."""
        state = _cece_core.CeceImportState()
        arr = np.ones((4, 5, 3), dtype=np.float64, order="C")
        assert arr.flags["C_CONTIGUOUS"]
        # Should succeed (converts internally with warning logged)
        state.set_field("pressure", arr)
        names = state.get_field_names()
        assert "pressure" in names

    def test_set_field_wrong_dtype_raises(self):
        """Test set_field with wrong dtype raises ValueError."""
        state = _cece_core.CeceImportState()
        arr = np.ones((4, 5, 3), dtype=np.int32, order="F")
        with pytest.raises(ValueError, match="float64"):
            state.set_field("bad_dtype", arr)

    def test_set_field_wrong_ndim_raises(self):
        """Test set_field with non-3D array raises ValueError."""
        state = _cece_core.CeceImportState()
        arr = np.ones((4, 5), dtype=np.float64, order="F")
        with pytest.raises(ValueError, match="3D"):
            state.set_field("bad_ndim", arr)

    def test_set_field_multiple_fields(self):
        """Test setting multiple fields."""
        state = _cece_core.CeceImportState()
        state.set_field("T", np.asfortranarray(np.ones((2, 3, 4), dtype=np.float64)))
        state.set_field("P", np.asfortranarray(np.zeros((2, 3, 4), dtype=np.float64)))
        names = state.get_field_names()
        assert len(names) == 2
        assert "T" in names
        assert "P" in names

    def test_get_field_names_empty(self):
        """Test get_field_names on empty state."""
        state = _cece_core.CeceImportState()
        assert state.get_field_names() == []


class TestCeceExportStateGetField:
    """Tests for CeceExportState.get_field method."""

    def test_get_field_missing_raises(self):
        """Test get_field with missing field raises KeyError."""
        export_state = _cece_core.CeceExportState()
        with pytest.raises(KeyError):
            export_state.get_field("nonexistent")

    def test_get_field_names_empty(self):
        """Test get_field_names on empty export state."""
        export_state = _cece_core.CeceExportState()
        assert export_state.get_field_names() == []


class TestExportFieldRoundTrip:
    """Tests for data round-trip and zero-copy behavior."""

    def test_import_data_preserved(self):
        """Test that data set via import state is preserved in the DualView."""
        import_state = _cece_core.CeceImportState()
        original = np.asfortranarray(
            np.arange(60, dtype=np.float64).reshape(3, 4, 5, order="F")
        )
        import_state.set_field("data", original)
        assert "data" in import_state.get_field_names()

    def test_import_c_contiguous_data_preserved(self):
        """Test that C-contiguous data is correctly converted and stored."""
        import_state = _cece_core.CeceImportState()
        original = np.arange(24, dtype=np.float64).reshape(2, 3, 4, order="C")
        assert original.flags["C_CONTIGUOUS"]
        import_state.set_field("c_data", original)
        assert "c_data" in import_state.get_field_names()

    def test_set_field_overwrite(self):
        """Test that setting a field with the same name overwrites it."""
        import_state = _cece_core.CeceImportState()
        arr1 = np.asfortranarray(np.ones((2, 3, 4), dtype=np.float64))
        arr2 = np.asfortranarray(np.zeros((2, 3, 4), dtype=np.float64))
        import_state.set_field("field", arr1)
        import_state.set_field("field", arr2)
        names = import_state.get_field_names()
        assert names.count("field") == 1


class TestCeceStateResolver:
    """Tests for CeceStateResolver binding."""

    def test_create_resolver(self):
        """Test creating a state resolver with import/export states and mappings."""
        import_state = _cece_core.CeceImportState()
        export_state = _cece_core.CeceExportState()
        met_mapping = {"T": "temperature", "P": "pressure"}
        sf_mapping = {"sf1": "scale_field_1"}
        mask_mapping = {"canada": "Canada_mask"}

        resolver = _cece_core.CeceStateResolver(
            import_state, export_state, met_mapping, sf_mapping, mask_mapping
        )
        assert resolver is not None

    def test_create_resolver_empty_mappings(self):
        """Test creating a resolver with empty mappings."""
        import_state = _cece_core.CeceImportState()
        export_state = _cece_core.CeceExportState()

        resolver = _cece_core.CeceStateResolver(import_state, export_state, {})
        assert resolver is not None

    def test_create_resolver_default_optional_mappings(self):
        """Test creating a resolver with only met_mapping (sf and mask default to empty)."""
        import_state = _cece_core.CeceImportState()
        export_state = _cece_core.CeceExportState()

        resolver = _cece_core.CeceStateResolver(
            import_state, export_state, {"T": "temperature"}
        )
        assert resolver is not None


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
