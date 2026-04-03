"""
Simple tests for exception classes (exceptions.py).

Tests the exception hierarchy, error code mapping, and formatting.
"""

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
    AcesException,
    AcesConfigError,
    AcesComputationError,
    AcesStateError,
    AcesExecutionSpaceError,
    error_code_to_exception,
    RECOVERY_SUGGESTIONS,
)


def test_exception_hierarchy():
    """Test exception class hierarchy."""
    print("Testing exception hierarchy...")
    assert issubclass(AcesException, Exception)
    assert issubclass(AcesConfigError, AcesException)
    assert issubclass(AcesComputationError, AcesException)
    assert issubclass(AcesStateError, AcesException)
    assert issubclass(AcesExecutionSpaceError, AcesException)
    print("✓ Exception hierarchy is correct")


def test_exception_initialization():
    """Test exception initialization."""
    print("Testing exception initialization...")

    # Test with message only
    exc = AcesException("Test error")
    assert exc.message == "Test error"
    assert exc.error_code is None
    assert exc.recovery_suggestions == []
    assert exc.c_call_site is None

    # Test with error code
    exc = AcesException("Test error", error_code=42)
    assert exc.error_code == 42

    # Test with recovery suggestions
    suggestions = ["Try this", "Or try that"]
    exc = AcesException("Test error", recovery_suggestions=suggestions)
    assert exc.recovery_suggestions == suggestions

    # Test with C++ call site
    call_site = "aces_c_compute at aces_compute.cpp:123"
    exc = AcesException("Test error", c_call_site=call_site)
    assert exc.c_call_site == call_site

    print("✓ Exception initialization works correctly")


def test_exception_formatting():
    """Test exception message formatting."""
    print("Testing exception message formatting...")

    # Test with error code
    exc = AcesException("Test error", error_code=1)
    message = str(exc)
    assert "[Error Code 1]" in message
    assert "Test error" in message

    # Test with recovery suggestions
    suggestions = ["Try this", "Or try that"]
    exc = AcesException("Test error", recovery_suggestions=suggestions)
    message = str(exc)
    assert "Recovery Suggestions:" in message
    assert "1. Try this" in message
    assert "2. Or try that" in message

    # Test with C++ call site
    call_site = "aces_c_compute at aces_compute.cpp:123"
    exc = AcesException("Test error", c_call_site=call_site)
    message = str(exc)
    assert "C++ Call Site:" in message
    assert call_site in message

    # Test complete formatted message
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

    print("✓ Exception formatting works correctly")


def test_error_code_mapping():
    """Test error code to exception conversion."""
    print("Testing error code mapping...")

    # Test success returns None
    result = error_code_to_exception(ACES_SUCCESS, "Success")
    assert result is None

    # Test invalid config error
    exc = error_code_to_exception(ACES_ERROR_INVALID_CONFIG, "Bad config")
    assert isinstance(exc, AcesConfigError)
    assert exc.error_code == ACES_ERROR_INVALID_CONFIG
    assert "Bad config" in str(exc)

    # Test invalid state error
    exc = error_code_to_exception(ACES_ERROR_INVALID_STATE, "Bad state")
    assert isinstance(exc, AcesStateError)
    assert exc.error_code == ACES_ERROR_INVALID_STATE

    # Test computation failed error
    exc = error_code_to_exception(ACES_ERROR_COMPUTATION_FAILED, "Compute failed")
    assert isinstance(exc, AcesComputationError)
    assert exc.error_code == ACES_ERROR_COMPUTATION_FAILED

    # Test memory allocation error
    exc = error_code_to_exception(ACES_ERROR_MEMORY_ALLOCATION, "Out of memory")
    assert isinstance(exc, AcesException)
    assert exc.error_code == ACES_ERROR_MEMORY_ALLOCATION
    assert "Memory allocation failed" in str(exc)

    # Test invalid execution space error
    exc = error_code_to_exception(ACES_ERROR_INVALID_EXECUTION_SPACE, "Bad space")
    assert isinstance(exc, AcesExecutionSpaceError)
    assert exc.error_code == ACES_ERROR_INVALID_EXECUTION_SPACE

    # Test not initialized error
    exc = error_code_to_exception(ACES_ERROR_NOT_INITIALIZED, "Not init")
    assert isinstance(exc, AcesException)
    assert exc.error_code == ACES_ERROR_NOT_INITIALIZED
    assert "not initialized" in str(exc).lower()

    # Test already initialized error
    exc = error_code_to_exception(ACES_ERROR_ALREADY_INITIALIZED, "Already init")
    assert isinstance(exc, AcesException)
    assert exc.error_code == ACES_ERROR_ALREADY_INITIALIZED
    assert "already initialized" in str(exc).lower()

    # Test field not found error
    exc = error_code_to_exception(ACES_ERROR_FIELD_NOT_FOUND, "Field missing")
    assert isinstance(exc, AcesStateError)
    assert exc.error_code == ACES_ERROR_FIELD_NOT_FOUND

    # Test dimension mismatch error
    exc = error_code_to_exception(ACES_ERROR_DIMENSION_MISMATCH, "Dims wrong")
    assert isinstance(exc, AcesStateError)
    assert exc.error_code == ACES_ERROR_DIMENSION_MISMATCH

    # Test unknown error code
    exc = error_code_to_exception(999, "Unknown error")
    assert isinstance(exc, AcesException)
    assert exc.error_code == 999
    assert "Unknown error" in str(exc)

    # Test error includes recovery suggestions
    exc = error_code_to_exception(ACES_ERROR_INVALID_CONFIG, "Bad config")
    assert len(exc.recovery_suggestions) > 0

    # Test error with C++ call site
    call_site = "aces_c_compute at aces_compute.cpp:123"
    exc = error_code_to_exception(
        ACES_ERROR_COMPUTATION_FAILED, "Compute failed", c_call_site=call_site
    )
    assert exc.c_call_site == call_site
    assert call_site in str(exc)

    print("✓ Error code mapping works correctly")


def test_recovery_suggestions():
    """Test recovery suggestions."""
    print("Testing recovery suggestions...")

    # Test that recovery suggestions exist for all error codes
    assert ACES_ERROR_INVALID_CONFIG in RECOVERY_SUGGESTIONS
    assert ACES_ERROR_INVALID_STATE in RECOVERY_SUGGESTIONS
    assert ACES_ERROR_COMPUTATION_FAILED in RECOVERY_SUGGESTIONS
    assert ACES_ERROR_MEMORY_ALLOCATION in RECOVERY_SUGGESTIONS
    assert ACES_ERROR_INVALID_EXECUTION_SPACE in RECOVERY_SUGGESTIONS
    assert ACES_ERROR_NOT_INITIALIZED in RECOVERY_SUGGESTIONS
    assert ACES_ERROR_ALREADY_INITIALIZED in RECOVERY_SUGGESTIONS
    assert ACES_ERROR_FIELD_NOT_FOUND in RECOVERY_SUGGESTIONS
    assert ACES_ERROR_DIMENSION_MISMATCH in RECOVERY_SUGGESTIONS

    # Test that all recovery suggestions are lists
    for code, suggestions in RECOVERY_SUGGESTIONS.items():
        assert isinstance(suggestions, list)
        assert len(suggestions) > 0
        for suggestion in suggestions:
            assert isinstance(suggestion, str)
            assert len(suggestion) > 0

    # Test specific suggestions
    config_suggestions = RECOVERY_SUGGESTIONS[ACES_ERROR_INVALID_CONFIG]
    assert any("YAML" in s or "yaml" in s for s in config_suggestions)

    exec_space_suggestions = RECOVERY_SUGGESTIONS[ACES_ERROR_INVALID_EXECUTION_SPACE]
    assert any("available" in s.lower() for s in exec_space_suggestions)

    not_init_suggestions = RECOVERY_SUGGESTIONS[ACES_ERROR_NOT_INITIALIZED]
    assert any("initialize" in s.lower() for s in not_init_suggestions)

    print("✓ Recovery suggestions are correct")


def test_exception_raising():
    """Test raising and catching exceptions."""
    print("Testing exception raising and catching...")

    # Test raising and catching AcesException
    try:
        raise AcesException("Test error")
    except AcesException as e:
        assert "Test error" in str(e)

    # Test raising and catching AcesConfigError
    try:
        raise AcesConfigError("Config error")
    except AcesConfigError as e:
        assert "Config error" in str(e)

    # Test catching AcesConfigError as AcesException
    try:
        raise AcesConfigError("Config error")
    except AcesException as e:
        assert isinstance(e, AcesConfigError)

    # Test exception message preserved
    message = "Test error message"
    try:
        raise AcesException(message)
    except AcesException as e:
        assert message in str(e)

    # Test exception error code preserved
    try:
        raise AcesException("Test", error_code=42)
    except AcesException as e:
        assert e.error_code == 42

    print("✓ Exception raising and catching works correctly")


def test_specific_exception_types():
    """Test specific exception types with default suggestions."""
    print("Testing specific exception types...")

    # Test AcesConfigError inherits suggestions
    exc = AcesConfigError("Invalid config")
    assert len(exc.recovery_suggestions) > 0
    assert any("YAML" in s for s in exc.recovery_suggestions)

    # Test AcesComputationError inherits suggestions
    exc = AcesComputationError("Computation failed")
    assert len(exc.recovery_suggestions) > 0

    # Test AcesStateError inherits suggestions
    exc = AcesStateError("Invalid state")
    assert len(exc.recovery_suggestions) > 0

    # Test AcesExecutionSpaceError inherits suggestions
    exc = AcesExecutionSpaceError("Invalid execution space")
    assert len(exc.recovery_suggestions) > 0

    print("✓ Specific exception types work correctly")


def main():
    """Run all tests."""
    print("=" * 60)
    print("Running Exception Tests")
    print("=" * 60)

    try:
        test_exception_hierarchy()
        test_exception_initialization()
        test_exception_formatting()
        test_error_code_mapping()
        test_recovery_suggestions()
        test_exception_raising()
        test_specific_exception_types()

        print("=" * 60)
        print("All tests passed! ✓")
        print("=" * 60)
        return 0
    except AssertionError as e:
        print(f"\n✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return 1
    except Exception as e:
        print(f"\n✗ Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
