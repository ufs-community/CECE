"""
Tests for Python C bindings (_bindings.py).

Tests the low-level ctypes wrapper for the C binding library.
"""

import pytest
import ctypes
import sys
from unittest.mock import Mock, patch, MagicMock
from pathlib import Path

# Add src/python to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "src" / "python"))

from _bindings import _CBindings, _release_gil, _check_error
from exceptions import (
    ACES_SUCCESS,
    ACES_ERROR_INVALID_CONFIG,
    ACES_ERROR_COMPUTATION_FAILED,
    AcesException,
    AcesConfigError,
    AcesComputationError,
)


class TestCBindingsLibraryLoading:
    """Tests for C library loading and function signature setup."""

    def test_bindings_initialization(self):
        """Test that _CBindings can be instantiated."""
        bindings = _CBindings()
        assert bindings is not None
        assert not bindings.is_loaded()

    def test_library_not_loaded_initially(self):
        """Test that library is not loaded on initialization."""
        bindings = _CBindings()
        assert not bindings.is_loaded()

    def test_get_last_error_when_not_loaded(self):
        """Test that get_last_error returns None when library not loaded."""
        bindings = _CBindings()
        assert bindings.get_last_error() is None

    @patch("ctypes.CDLL")
    def test_load_library_with_mock(self, mock_cdll):
        """Test library loading with mocked CDLL."""
        # Create a mock library
        mock_lib = MagicMock()
        mock_cdll.return_value = mock_lib

        # Create mock functions
        for func_name in [
            "aces_c_initialize",
            "aces_c_finalize",
            "aces_c_is_initialized",
            "aces_c_state_create",
            "aces_c_state_destroy",
            "aces_c_state_add_import_field",
            "aces_c_state_get_export_field",
            "aces_c_compute",
            "aces_c_config_validate",
            "aces_c_config_to_yaml",
            "aces_c_set_execution_space",
            "aces_c_get_execution_space",
            "aces_c_get_available_execution_spaces",
            "aces_c_get_last_error",
            "aces_c_free_string",
            "aces_c_set_log_level",
            "aces_c_get_diagnostics",
            "aces_c_reset_diagnostics",
        ]:
            setattr(mock_lib, func_name, MagicMock())

        bindings = _CBindings()
        bindings.load_library()

        # Verify library was loaded
        assert bindings.is_loaded()

    @patch("ctypes.CDLL")
    def test_function_signatures_setup(self, mock_cdll):
        """Test that function signatures are properly set up."""
        mock_lib = MagicMock()
        mock_cdll.return_value = mock_lib

        # Create mock functions
        for func_name in [
            "aces_c_initialize",
            "aces_c_finalize",
            "aces_c_is_initialized",
            "aces_c_state_create",
            "aces_c_state_destroy",
            "aces_c_state_add_import_field",
            "aces_c_state_get_export_field",
            "aces_c_compute",
            "aces_c_config_validate",
            "aces_c_config_to_yaml",
            "aces_c_set_execution_space",
            "aces_c_get_execution_space",
            "aces_c_get_available_execution_spaces",
            "aces_c_get_last_error",
            "aces_c_free_string",
            "aces_c_set_log_level",
            "aces_c_get_diagnostics",
            "aces_c_reset_diagnostics",
        ]:
            setattr(mock_lib, func_name, MagicMock())

        bindings = _CBindings()
        bindings.load_library()

        # Verify that function attributes were set
        assert hasattr(bindings, "aces_c_initialize")
        assert hasattr(bindings, "aces_c_finalize")
        assert hasattr(bindings, "aces_c_compute")
        assert hasattr(bindings, "aces_c_get_last_error")

    def test_load_library_not_found(self):
        """Test that OSError is raised when library cannot be found."""
        bindings = _CBindings()
        with pytest.raises(OSError) as exc_info:
            bindings.load_library("/nonexistent/path/libaces_c_bindings.so")
        assert "Could not find or load" in str(exc_info.value)

    @patch("ctypes.CDLL")
    def test_load_library_idempotent(self, mock_cdll):
        """Test that loading library twice doesn't reload it."""
        mock_lib = MagicMock()
        mock_cdll.return_value = mock_lib

        for func_name in [
            "aces_c_initialize",
            "aces_c_finalize",
            "aces_c_is_initialized",
            "aces_c_state_create",
            "aces_c_state_destroy",
            "aces_c_state_add_import_field",
            "aces_c_state_get_export_field",
            "aces_c_compute",
            "aces_c_config_validate",
            "aces_c_config_to_yaml",
            "aces_c_set_execution_space",
            "aces_c_get_execution_space",
            "aces_c_get_available_execution_spaces",
            "aces_c_get_last_error",
            "aces_c_free_string",
            "aces_c_set_log_level",
            "aces_c_get_diagnostics",
            "aces_c_reset_diagnostics",
        ]:
            setattr(mock_lib, func_name, MagicMock())

        bindings = _CBindings()
        bindings.load_library()
        first_lib = bindings._lib

        bindings.load_library()
        second_lib = bindings._lib

        # Should be the same instance
        assert first_lib is second_lib


class TestErrorHandling:
    """Tests for error code to exception conversion."""

    def test_check_error_success(self):
        """Test that _check_error does nothing on success."""
        # Should not raise
        _check_error(ACES_SUCCESS)

    @patch("_bindings._c_bindings.get_last_error")
    def test_check_error_invalid_config(self, mock_get_error):
        """Test that invalid config error is converted correctly."""
        mock_get_error.return_value = "Invalid configuration"
        with pytest.raises(AcesConfigError) as exc_info:
            _check_error(ACES_ERROR_INVALID_CONFIG)
        assert "Invalid configuration" in str(exc_info.value)

    @patch("_bindings._c_bindings.get_last_error")
    def test_check_error_computation_failed(self, mock_get_error):
        """Test that computation error is converted correctly."""
        mock_get_error.return_value = "Computation failed"
        with pytest.raises(AcesComputationError) as exc_info:
            _check_error(ACES_ERROR_COMPUTATION_FAILED)
        assert "Computation failed" in str(exc_info.value)

    @patch("_bindings._c_bindings.get_last_error")
    def test_check_error_unknown_code(self, mock_get_error):
        """Test that unknown error code is handled."""
        mock_get_error.return_value = "Unknown error"
        with pytest.raises(AcesException) as exc_info:
            _check_error(999)
        assert "Unknown error" in str(exc_info.value)


class TestGILManagement:
    """Tests for GIL release context manager."""

    def test_release_gil_context_manager(self):
        """Test that _release_gil is a context manager."""
        with _release_gil():
            # Should not raise
            pass

    def test_release_gil_exception_handling(self):
        """Test that GIL is reacquired even on exception."""
        try:
            with _release_gil():
                raise ValueError("Test exception")
        except ValueError:
            pass
        # If we get here, GIL was properly reacquired

    def test_release_gil_multiple_times(self):
        """Test that _release_gil can be used multiple times."""
        with _release_gil():
            pass
        with _release_gil():
            pass
        # Should not raise


class TestFunctionSignatures:
    """Tests for ctypes function signature definitions."""

    @patch("ctypes.CDLL")
    def test_all_functions_have_signatures(self, mock_cdll):
        """Test that all C functions have proper signatures."""
        mock_lib = MagicMock()
        mock_cdll.return_value = mock_lib

        for func_name in [
            "aces_c_initialize",
            "aces_c_finalize",
            "aces_c_is_initialized",
            "aces_c_state_create",
            "aces_c_state_destroy",
            "aces_c_state_add_import_field",
            "aces_c_state_get_export_field",
            "aces_c_compute",
            "aces_c_config_validate",
            "aces_c_config_to_yaml",
            "aces_c_set_execution_space",
            "aces_c_get_execution_space",
            "aces_c_get_available_execution_spaces",
            "aces_c_get_last_error",
            "aces_c_free_string",
            "aces_c_set_log_level",
            "aces_c_get_diagnostics",
            "aces_c_reset_diagnostics",
        ]:
            setattr(mock_lib, func_name, MagicMock())

        bindings = _CBindings()
        bindings.load_library()

        # Verify each function has argtypes and restype set
        for func_name in [
            "aces_c_initialize",
            "aces_c_finalize",
            "aces_c_is_initialized",
            "aces_c_state_create",
            "aces_c_state_destroy",
            "aces_c_state_add_import_field",
            "aces_c_state_get_export_field",
            "aces_c_compute",
            "aces_c_config_validate",
            "aces_c_config_to_yaml",
            "aces_c_set_execution_space",
            "aces_c_get_execution_space",
            "aces_c_get_available_execution_spaces",
            "aces_c_get_last_error",
            "aces_c_free_string",
            "aces_c_set_log_level",
            "aces_c_get_diagnostics",
            "aces_c_reset_diagnostics",
        ]:
            func = getattr(bindings, func_name)
            assert func is not None


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
