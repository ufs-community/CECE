"""
Tests for exception classes (exceptions.py).

Tests the exception hierarchy, error code mapping, and formatting.
"""

import pytest
import sys
from pathlib import Path

# Add src/python to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "src" / "python"))

from exceptions import (
    CECE_SUCCESS,
    CECE_ERROR_INVALID_CONFIG,
    CECE_ERROR_INVALID_STATE,
    CECE_ERROR_COMPUTATION_FAILED,
    CECE_ERROR_MEMORY_ALLOCATION,
    CECE_ERROR_INVALID_EXECUTION_SPACE,
    CECE_ERROR_NOT_INITIALIZED,
    CECE_ERROR_ALREADY_INITIALIZED,
    CECE_ERROR_FIELD_NOT_FOUND,
    CECE_ERROR_DIMENSION_MISMATCH,
    CeceException,
    CeceConfigError,
    CeceComputationError,
    CeceStateError,
    CeceExecutionSpaceError,
    error_code_to_exception,
    RECOVERY_SUGGESTIONS,
)


class TestExceptionHierarchy:
    """Tests for exception class hierarchy."""

    def test_cece_exception_is_exception(self):
        """Test that CeceException is a subclass of Exception."""
        assert issubclass(CeceException, Exception)

    def test_config_error_is_cece_exception(self):
        """Test that CeceConfigError is a subclass of CeceException."""
        assert issubclass(CeceConfigError, CeceException)

    def test_computation_error_is_cece_exception(self):
        """Test that CeceComputationError is a subclass of CeceException."""
        assert issubclass(CeceComputationError, CeceException)

    def test_state_error_is_cece_exception(self):
        """Test that CeceStateError is a subclass of CeceException."""
        assert issubclass(CeceStateError, CeceException)

    def test_execution_space_error_is_cece_exception(self):
        """Test that CeceExecutionSpaceError is a subclass of CeceException."""
        assert issubclass(CeceExecutionSpaceError, CeceException)


class TestExceptionInitialization:
    """Tests for exception initialization and attributes."""

    def test_cece_exception_with_message_only(self):
        """Test CeceException with just a message."""
        exc = CeceException("Test error")
        assert exc.message == "Test error"
        assert exc.error_code is None
        assert exc.recovery_suggestions == []
        assert exc.c_call_site is None

    def test_cece_exception_with_error_code(self):
        """Test CeceException with error code."""
        exc = CeceException("Test error", error_code=42)
        assert exc.message == "Test error"
        assert exc.error_code == 42

    def test_cece_exception_with_recovery_suggestions(self):
        """Test CeceException with recovery suggestions."""
        suggestions = ["Try this", "Or try that"]
        exc = CeceException("Test error", recovery_suggestions=suggestions)
        assert exc.recovery_suggestions == suggestions

    def test_cece_exception_with_c_call_site(self):
        """Test CeceException with C++ call site."""
        call_site = "cece_c_compute at cece_compute.cpp:123"
        exc = CeceException("Test error", c_call_site=call_site)
        assert exc.c_call_site == call_site

    def test_config_error_inherits_suggestions(self):
        """Test that CeceConfigError inherits default suggestions."""
        exc = CeceConfigError("Invalid config")
        assert len(exc.recovery_suggestions) > 0
        assert any("YAML" in s for s in exc.recovery_suggestions)

    def test_computation_error_inherits_suggestions(self):
        """Test that CeceComputationError inherits default suggestions."""
        exc = CeceComputationError("Computation failed")
        assert len(exc.recovery_suggestions) > 0

    def test_state_error_inherits_suggestions(self):
        """Test that CeceStateError inherits default suggestions."""
        exc = CeceStateError("Invalid state")
        assert len(exc.recovery_suggestions) > 0

    def test_execution_space_error_inherits_suggestions(self):
        """Test that CeceExecutionSpaceError inherits default suggestions."""
        exc = CeceExecutionSpaceError("Invalid execution space")
        assert len(exc.recovery_suggestions) > 0


class TestExceptionFormatting:
    """Tests for exception message formatting."""

    def test_format_message_with_error_code(self):
        """Test that error code is included in formatted message."""
        exc = CeceException("Test error", error_code=1)
        message = str(exc)
        assert "[Error Code 1]" in message
        assert "Test error" in message

    def test_format_message_with_recovery_suggestions(self):
        """Test that recovery suggestions are included in formatted message."""
        suggestions = ["Try this", "Or try that"]
        exc = CeceException("Test error", recovery_suggestions=suggestions)
        message = str(exc)
        assert "Recovery Suggestions:" in message
        assert "1. Try this" in message
        assert "2. Or try that" in message

    def test_format_message_with_c_call_site(self):
        """Test that C++ call site is included in formatted message."""
        call_site = "cece_c_compute at cece_compute.cpp:123"
        exc = CeceException("Test error", c_call_site=call_site)
        message = str(exc)
        assert "C++ Call Site:" in message
        assert call_site in message

    def test_format_message_complete(self):
        """Test complete formatted message with all components."""
        suggestions = ["Try this"]
        call_site = "cece_c_compute at cece_compute.cpp:123"
        exc = CeceException(
            "Test error",
            error_code=1,
            recovery_suggestions=suggestions,
            c_call_site=call_site,
        )
        message = str(exc)
        assert "[Error Code 1]" in message
        assert "Test error" in message
        assert "C++ Call Site:" in message
        assert call_site in message
        assert "Recovery Suggestions:" in message
        assert "1. Try this" in message

    def test_format_message_without_error_code(self):
        """Test formatted message without error code."""
        exc = CeceException("Test error")
        message = str(exc)
        assert "Test error" in message
        assert "[Error Code" not in message


class TestErrorCodeMapping:
    """Tests for error code to exception conversion."""

    def test_success_returns_none(self):
        """Test that CECE_SUCCESS returns None."""
        result = error_code_to_exception(CECE_SUCCESS, "Success")
        assert result is None

    def test_invalid_config_error(self):
        """Test that CECE_ERROR_INVALID_CONFIG creates CeceConfigError."""
        exc = error_code_to_exception(CECE_ERROR_INVALID_CONFIG, "Bad config")
        assert isinstance(exc, CeceConfigError)
        assert exc.error_code == CECE_ERROR_INVALID_CONFIG
        assert "Bad config" in str(exc)

    def test_invalid_state_error(self):
        """Test that CECE_ERROR_INVALID_STATE creates CeceStateError."""
        exc = error_code_to_exception(CECE_ERROR_INVALID_STATE, "Bad state")
        assert isinstance(exc, CeceStateError)
        assert exc.error_code == CECE_ERROR_INVALID_STATE

    def test_computation_failed_error(self):
        """Test that CECE_ERROR_COMPUTATION_FAILED creates CeceComputationError."""
        exc = error_code_to_exception(CECE_ERROR_COMPUTATION_FAILED, "Compute failed")
        assert isinstance(exc, CeceComputationError)
        assert exc.error_code == CECE_ERROR_COMPUTATION_FAILED

    def test_memory_allocation_error(self):
        """Test that CECE_ERROR_MEMORY_ALLOCATION creates CeceException."""
        exc = error_code_to_exception(CECE_ERROR_MEMORY_ALLOCATION, "Out of memory")
        assert isinstance(exc, CeceException)
        assert exc.error_code == CECE_ERROR_MEMORY_ALLOCATION
        assert "Memory allocation failed" in str(exc)

    def test_invalid_execution_space_error(self):
        """Test that CECE_ERROR_INVALID_EXECUTION_SPACE creates CeceExecutionSpaceError."""
        exc = error_code_to_exception(CECE_ERROR_INVALID_EXECUTION_SPACE, "Bad space")
        assert isinstance(exc, CeceExecutionSpaceError)
        assert exc.error_code == CECE_ERROR_INVALID_EXECUTION_SPACE

    def test_not_initialized_error(self):
        """Test that CECE_ERROR_NOT_INITIALIZED creates CeceException."""
        exc = error_code_to_exception(CECE_ERROR_NOT_INITIALIZED, "Not init")
        assert isinstance(exc, CeceException)
        assert exc.error_code == CECE_ERROR_NOT_INITIALIZED
        assert "not initialized" in str(exc).lower()

    def test_already_initialized_error(self):
        """Test that CECE_ERROR_ALREADY_INITIALIZED creates CeceException."""
        exc = error_code_to_exception(CECE_ERROR_ALREADY_INITIALIZED, "Already init")
        assert isinstance(exc, CeceException)
        assert exc.error_code == CECE_ERROR_ALREADY_INITIALIZED
        assert "already initialized" in str(exc).lower()

    def test_field_not_found_error(self):
        """Test that CECE_ERROR_FIELD_NOT_FOUND creates CeceStateError."""
        exc = error_code_to_exception(CECE_ERROR_FIELD_NOT_FOUND, "Field missing")
        assert isinstance(exc, CeceStateError)
        assert exc.error_code == CECE_ERROR_FIELD_NOT_FOUND

    def test_dimension_mismatch_error(self):
        """Test that CECE_ERROR_DIMENSION_MISMATCH creates CeceStateError."""
        exc = error_code_to_exception(CECE_ERROR_DIMENSION_MISMATCH, "Dims wrong")
        assert isinstance(exc, CeceStateError)
        assert exc.error_code == CECE_ERROR_DIMENSION_MISMATCH

    def test_unknown_error_code(self):
        """Test that unknown error code creates CeceException."""
        exc = error_code_to_exception(999, "Unknown error")
        assert isinstance(exc, CeceException)
        assert exc.error_code == 999
        assert "Unknown error" in str(exc)

    def test_error_includes_recovery_suggestions(self):
        """Test that error includes recovery suggestions."""
        exc = error_code_to_exception(CECE_ERROR_INVALID_CONFIG, "Bad config")
        assert len(exc.recovery_suggestions) > 0

    def test_error_with_c_call_site(self):
        """Test that error includes C++ call site."""
        call_site = "cece_c_compute at cece_compute.cpp:123"
        exc = error_code_to_exception(
            CECE_ERROR_COMPUTATION_FAILED, "Compute failed", c_call_site=call_site
        )
        assert exc.c_call_site == call_site
        assert call_site in str(exc)


class TestRecoverySuggestions:
    """Tests for recovery suggestions."""

    def test_recovery_suggestions_exist(self):
        """Test that recovery suggestions are defined for all error codes."""
        assert CECE_ERROR_INVALID_CONFIG in RECOVERY_SUGGESTIONS
        assert CECE_ERROR_INVALID_STATE in RECOVERY_SUGGESTIONS
        assert CECE_ERROR_COMPUTATION_FAILED in RECOVERY_SUGGESTIONS
        assert CECE_ERROR_MEMORY_ALLOCATION in RECOVERY_SUGGESTIONS
        assert CECE_ERROR_INVALID_EXECUTION_SPACE in RECOVERY_SUGGESTIONS
        assert CECE_ERROR_NOT_INITIALIZED in RECOVERY_SUGGESTIONS
        assert CECE_ERROR_ALREADY_INITIALIZED in RECOVERY_SUGGESTIONS
        assert CECE_ERROR_FIELD_NOT_FOUND in RECOVERY_SUGGESTIONS
        assert CECE_ERROR_DIMENSION_MISMATCH in RECOVERY_SUGGESTIONS

    def test_recovery_suggestions_are_lists(self):
        """Test that all recovery suggestions are lists."""
        for code, suggestions in RECOVERY_SUGGESTIONS.items():
            assert isinstance(suggestions, list)
            assert len(suggestions) > 0
            for suggestion in suggestions:
                assert isinstance(suggestion, str)
                assert len(suggestion) > 0

    def test_config_error_suggestions_mention_yaml(self):
        """Test that config error suggestions mention YAML."""
        suggestions = RECOVERY_SUGGESTIONS[CECE_ERROR_INVALID_CONFIG]
        assert any("YAML" in s or "yaml" in s for s in suggestions)

    def test_execution_space_suggestions_mention_available_spaces(self):
        """Test that execution space suggestions mention available spaces."""
        suggestions = RECOVERY_SUGGESTIONS[CECE_ERROR_INVALID_EXECUTION_SPACE]
        assert any("available" in s.lower() for s in suggestions)

    def test_not_initialized_suggestions_mention_initialize(self):
        """Test that not initialized suggestions mention initialize."""
        suggestions = RECOVERY_SUGGESTIONS[CECE_ERROR_NOT_INITIALIZED]
        assert any("initialize" in s.lower() for s in suggestions)


class TestExceptionRaising:
    """Tests for raising and catching exceptions."""

    def test_raise_and_catch_cece_exception(self):
        """Test raising and catching CeceException."""
        with pytest.raises(CeceException):
            raise CeceException("Test error")

    def test_raise_and_catch_config_error(self):
        """Test raising and catching CeceConfigError."""
        with pytest.raises(CeceConfigError):
            raise CeceConfigError("Config error")

    def test_catch_config_error_as_cece_exception(self):
        """Test catching CeceConfigError as CeceException."""
        with pytest.raises(CeceException):
            raise CeceConfigError("Config error")

    def test_exception_message_preserved(self):
        """Test that exception message is preserved."""
        message = "Test error message"
        with pytest.raises(CeceException) as exc_info:
            raise CeceException(message)
        assert message in str(exc_info.value)

    def test_exception_error_code_preserved(self):
        """Test that exception error code is preserved."""
        with pytest.raises(CeceException) as exc_info:
            raise CeceException("Test", error_code=42)
        assert exc_info.value.error_code == 42


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
