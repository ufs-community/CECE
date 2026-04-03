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
    ACES_SUCCESS,
    ACES_ERROR_INVALID_CONFIG,
    ACES_ERROR_INVALID_STATE,
    ACES_ERROR_COMPUTATION_FAILED,
    ACES_ERROR_MEMORY_ALLOCATION,
    ACES_ERROR_INVALID_EXECUTION_SPACE,
    ACES_ERROR_NOT_INITIALIZED,
    ACES_ERROR_ALREADY_INITIALIZED,
    ACES_ERROR_FIELD_NOT_FOUND,
    ACES_ERROR_DIMENSION_MISMATCH,
    ACES_ERROR_UNKNOWN,
    AcesException,
    AcesConfigError,
    AcesComputationError,
    AcesStateError,
    AcesExecutionSpaceError,
    error_code_to_exception,
    RECOVERY_SUGGESTIONS,
)


class TestExceptionHierarchy:
    """Tests for exception class hierarchy."""

    def test_aces_exception_is_exception(self):
        """Test that AcesException is a subclass of Exception."""
        assert issubclass(AcesException, Exception)

    def test_config_error_is_aces_exception(self):
        """Test that AcesConfigError is a subclass of AcesException."""
        assert issubclass(AcesConfigError, AcesException)

    def test_computation_error_is_aces_exception(self):
        """Test that AcesComputationError is a subclass of AcesException."""
        assert issubclass(AcesComputationError, AcesException)

    def test_state_error_is_aces_exception(self):
        """Test that AcesStateError is a subclass of AcesException."""
        assert issubclass(AcesStateError, AcesException)

    def test_execution_space_error_is_aces_exception(self):
        """Test that AcesExecutionSpaceError is a subclass of AcesException."""
        assert issubclass(AcesExecutionSpaceError, AcesException)


class TestExceptionInitialization:
    """Tests for exception initialization and attributes."""

    def test_aces_exception_with_message_only(self):
        """Test AcesException with just a message."""
        exc = AcesException("Test error")
        assert exc.message == "Test error"
        assert exc.error_code is None
        assert exc.recovery_suggestions == []
        assert exc.c_call_site is None

    def test_aces_exception_with_error_code(self):
        """Test AcesException with error code."""
        exc = AcesException("Test error", error_code=42)
        assert exc.message == "Test error"
        assert exc.error_code == 42

    def test_aces_exception_with_recovery_suggestions(self):
        """Test AcesException with recovery suggestions."""
        suggestions = ["Try this", "Or try that"]
        exc = AcesException("Test error", recovery_suggestions=suggestions)
        assert exc.recovery_suggestions == suggestions

    def test_aces_exception_with_c_call_site(self):
        """Test AcesException with C++ call site."""
        call_site = "aces_c_compute at aces_compute.cpp:123"
        exc = AcesException("Test error", c_call_site=call_site)
        assert exc.c_call_site == call_site

    def test_config_error_inherits_suggestions(self):
        """Test that AcesConfigError inherits default suggestions."""
        exc = AcesConfigError("Invalid config")
        assert len(exc.recovery_suggestions) > 0
        assert any("YAML" in s for s in exc.recovery_suggestions)

    def test_computation_error_inherits_suggestions(self):
        """Test that AcesComputationError inherits default suggestions."""
        exc = AcesComputationError("Computation failed")
        assert len(exc.recovery_suggestions) > 0

    def test_state_error_inherits_suggestions(self):
        """Test that AcesStateError inherits default suggestions."""
        exc = AcesStateError("Invalid state")
        assert len(exc.recovery_suggestions) > 0

    def test_execution_space_error_inherits_suggestions(self):
        """Test that AcesExecutionSpaceError inherits default suggestions."""
        exc = AcesExecutionSpaceError("Invalid execution space")
        assert len(exc.recovery_suggestions) > 0


class TestExceptionFormatting:
    """Tests for exception message formatting."""

    def test_format_message_with_error_code(self):
        """Test that error code is included in formatted message."""
        exc = AcesException("Test error", error_code=1)
        message = str(exc)
        assert "[Error Code 1]" in message
        assert "Test error" in message

    def test_format_message_with_recovery_suggestions(self):
        """Test that recovery suggestions are included in formatted message."""
        suggestions = ["Try this", "Or try that"]
        exc = AcesException("Test error", recovery_suggestions=suggestions)
        message = str(exc)
        assert "Recovery Suggestions:" in message
        assert "1. Try this" in message
        assert "2. Or try that" in message

    def test_format_message_with_c_call_site(self):
        """Test that C++ call site is included in formatted message."""
        call_site = "aces_c_compute at aces_compute.cpp:123"
        exc = AcesException("Test error", c_call_site=call_site)
        message = str(exc)
        assert "C++ Call Site:" in message
        assert call_site in message

    def test_format_message_complete(self):
        """Test complete formatted message with all components."""
        suggestions = ["Try this"]
        call_site = "aces_c_compute at aces_compute.cpp:123"
        exc = AcesException(
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
        exc = AcesException("Test error")
        message = str(exc)
        assert "Test error" in message
        assert "[Error Code" not in message


class TestErrorCodeMapping:
    """Tests for error code to exception conversion."""

    def test_success_returns_none(self):
        """Test that ACES_SUCCESS returns None."""
        result = error_code_to_exception(ACES_SUCCESS, "Success")
        assert result is None

    def test_invalid_config_error(self):
        """Test that ACES_ERROR_INVALID_CONFIG creates AcesConfigError."""
        exc = error_code_to_exception(ACES_ERROR_INVALID_CONFIG, "Bad config")
        assert isinstance(exc, AcesConfigError)
        assert exc.error_code == ACES_ERROR_INVALID_CONFIG
        assert "Bad config" in str(exc)

    def test_invalid_state_error(self):
        """Test that ACES_ERROR_INVALID_STATE creates AcesStateError."""
        exc = error_code_to_exception(ACES_ERROR_INVALID_STATE, "Bad state")
        assert isinstance(exc, AcesStateError)
        assert exc.error_code == ACES_ERROR_INVALID_STATE

    def test_computation_failed_error(self):
        """Test that ACES_ERROR_COMPUTATION_FAILED creates AcesComputationError."""
        exc = error_code_to_exception(ACES_ERROR_COMPUTATION_FAILED, "Compute failed")
        assert isinstance(exc, AcesComputationError)
        assert exc.error_code == ACES_ERROR_COMPUTATION_FAILED

    def test_memory_allocation_error(self):
        """Test that ACES_ERROR_MEMORY_ALLOCATION creates AcesException."""
        exc = error_code_to_exception(ACES_ERROR_MEMORY_ALLOCATION, "Out of memory")
        assert isinstance(exc, AcesException)
        assert exc.error_code == ACES_ERROR_MEMORY_ALLOCATION
        assert "Memory allocation failed" in str(exc)

    def test_invalid_execution_space_error(self):
        """Test that ACES_ERROR_INVALID_EXECUTION_SPACE creates AcesExecutionSpaceError."""
        exc = error_code_to_exception(
            ACES_ERROR_INVALID_EXECUTION_SPACE, "Bad space"
        )
        assert isinstance(exc, AcesExecutionSpaceError)
        assert exc.error_code == ACES_ERROR_INVALID_EXECUTION_SPACE

    def test_not_initialized_error(self):
        """Test that ACES_ERROR_NOT_INITIALIZED creates AcesException."""
        exc = error_code_to_exception(ACES_ERROR_NOT_INITIALIZED, "Not init")
        assert isinstance(exc, AcesException)
        assert exc.error_code == ACES_ERROR_NOT_INITIALIZED
        assert "not initialized" in str(exc).lower()

    def test_already_initialized_error(self):
        """Test that ACES_ERROR_ALREADY_INITIALIZED creates AcesException."""
        exc = error_code_to_exception(ACES_ERROR_ALREADY_INITIALIZED, "Already init")
        assert isinstance(exc, AcesException)
        assert exc.error_code == ACES_ERROR_ALREADY_INITIALIZED
        assert "already initialized" in str(exc).lower()

    def test_field_not_found_error(self):
        """Test that ACES_ERROR_FIELD_NOT_FOUND creates AcesStateError."""
        exc = error_code_to_exception(ACES_ERROR_FIELD_NOT_FOUND, "Field missing")
        assert isinstance(exc, AcesStateError)
        assert exc.error_code == ACES_ERROR_FIELD_NOT_FOUND

    def test_dimension_mismatch_error(self):
        """Test that ACES_ERROR_DIMENSION_MISMATCH creates AcesStateError."""
        exc = error_code_to_exception(ACES_ERROR_DIMENSION_MISMATCH, "Dims wrong")
        assert isinstance(exc, AcesStateError)
        assert exc.error_code == ACES_ERROR_DIMENSION_MISMATCH

    def test_unknown_error_code(self):
        """Test that unknown error code creates AcesException."""
        exc = error_code_to_exception(999, "Unknown error")
        assert isinstance(exc, AcesException)
        assert exc.error_code == 999
        assert "Unknown error" in str(exc)

    def test_error_includes_recovery_suggestions(self):
        """Test that error includes recovery suggestions."""
        exc = error_code_to_exception(ACES_ERROR_INVALID_CONFIG, "Bad config")
        assert len(exc.recovery_suggestions) > 0

    def test_error_with_c_call_site(self):
        """Test that error includes C++ call site."""
        call_site = "aces_c_compute at aces_compute.cpp:123"
        exc = error_code_to_exception(
            ACES_ERROR_COMPUTATION_FAILED, "Compute failed", c_call_site=call_site
        )
        assert exc.c_call_site == call_site
        assert call_site in str(exc)


class TestRecoverySuggestions:
    """Tests for recovery suggestions."""

    def test_recovery_suggestions_exist(self):
        """Test that recovery suggestions are defined for all error codes."""
        assert ACES_ERROR_INVALID_CONFIG in RECOVERY_SUGGESTIONS
        assert ACES_ERROR_INVALID_STATE in RECOVERY_SUGGESTIONS
        assert ACES_ERROR_COMPUTATION_FAILED in RECOVERY_SUGGESTIONS
        assert ACES_ERROR_MEMORY_ALLOCATION in RECOVERY_SUGGESTIONS
        assert ACES_ERROR_INVALID_EXECUTION_SPACE in RECOVERY_SUGGESTIONS
        assert ACES_ERROR_NOT_INITIALIZED in RECOVERY_SUGGESTIONS
        assert ACES_ERROR_ALREADY_INITIALIZED in RECOVERY_SUGGESTIONS
        assert ACES_ERROR_FIELD_NOT_FOUND in RECOVERY_SUGGESTIONS
        assert ACES_ERROR_DIMENSION_MISMATCH in RECOVERY_SUGGESTIONS

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
        suggestions = RECOVERY_SUGGESTIONS[ACES_ERROR_INVALID_CONFIG]
        assert any("YAML" in s or "yaml" in s for s in suggestions)

    def test_execution_space_suggestions_mention_available_spaces(self):
        """Test that execution space suggestions mention available spaces."""
        suggestions = RECOVERY_SUGGESTIONS[ACES_ERROR_INVALID_EXECUTION_SPACE]
        assert any("available" in s.lower() for s in suggestions)

    def test_not_initialized_suggestions_mention_initialize(self):
        """Test that not initialized suggestions mention initialize."""
        suggestions = RECOVERY_SUGGESTIONS[ACES_ERROR_NOT_INITIALIZED]
        assert any("initialize" in s.lower() for s in suggestions)


class TestExceptionRaising:
    """Tests for raising and catching exceptions."""

    def test_raise_and_catch_aces_exception(self):
        """Test raising and catching AcesException."""
        with pytest.raises(AcesException):
            raise AcesException("Test error")

    def test_raise_and_catch_config_error(self):
        """Test raising and catching AcesConfigError."""
        with pytest.raises(AcesConfigError):
            raise AcesConfigError("Config error")

    def test_catch_config_error_as_aces_exception(self):
        """Test catching AcesConfigError as AcesException."""
        with pytest.raises(AcesException):
            raise AcesConfigError("Config error")

    def test_exception_message_preserved(self):
        """Test that exception message is preserved."""
        message = "Test error message"
        with pytest.raises(AcesException) as exc_info:
            raise AcesException(message)
        assert message in str(exc_info.value)

    def test_exception_error_code_preserved(self):
        """Test that exception error code is preserved."""
        with pytest.raises(AcesException) as exc_info:
            raise AcesException("Test", error_code=42)
        assert exc_info.value.error_code == 42


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
