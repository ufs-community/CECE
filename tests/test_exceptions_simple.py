"""
Simple tests for exception classes (exceptions.py).

Tests the exception hierarchy, error code mapping, and formatting.
"""

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


def test_exception_hierarchy():
    """Test exception class hierarchy."""
    print("Testing exception hierarchy...")
    assert issubclass(CeceException, Exception)
    assert issubclass(CeceConfigError, CeceException)
    assert issubclass(CeceComputationError, CeceException)
    assert issubclass(CeceStateError, CeceException)
    assert issubclass(CeceExecutionSpaceError, CeceException)
    print("✓ Exception hierarchy is correct")


def test_exception_initialization():
    """Test exception initialization."""
    print("Testing exception initialization...")

    # Test with message only
    exc = CeceException("Test error")
    assert exc.message == "Test error"
    assert exc.error_code is None
    assert exc.recovery_suggestions == []
    assert exc.c_call_site is None

    # Test with error code
    exc = CeceException("Test error", error_code=42)
    assert exc.error_code == 42

    # Test with recovery suggestions
    suggestions = ["Try this", "Or try that"]
    exc = CeceException("Test error", recovery_suggestions=suggestions)
    assert exc.recovery_suggestions == suggestions

    # Test with C++ call site
    call_site = "cece_c_compute at cece_compute.cpp:123"
    exc = CeceException("Test error", c_call_site=call_site)
    assert exc.c_call_site == call_site

    print("✓ Exception initialization works correctly")


def test_exception_formatting():
    """Test exception message formatting."""
    print("Testing exception message formatting...")

    # Test with error code
    exc = CeceException("Test error", error_code=1)
    message = str(exc)
    assert "[Error Code 1]" in message
    assert "Test error" in message

    # Test with recovery suggestions
    suggestions = ["Try this", "Or try that"]
    exc = CeceException("Test error", recovery_suggestions=suggestions)
    message = str(exc)
    assert "Recovery Suggestions:" in message
    assert "1. Try this" in message
    assert "2. Or try that" in message

    # Test with C++ call site
    call_site = "cece_c_compute at cece_compute.cpp:123"
    exc = CeceException("Test error", c_call_site=call_site)
    message = str(exc)
    assert "C++ Call Site:" in message
    assert call_site in message

    # Test complete formatted message
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

    print("✓ Exception formatting works correctly")


def test_error_code_mapping():
    """Test error code to exception conversion."""
    print("Testing error code mapping...")

    # Test success returns None
    result = error_code_to_exception(CECE_SUCCESS, "Success")
    assert result is None

    # Test invalid config error
    exc = error_code_to_exception(CECE_ERROR_INVALID_CONFIG, "Bad config")
    assert isinstance(exc, CeceConfigError)
    assert exc.error_code == CECE_ERROR_INVALID_CONFIG
    assert "Bad config" in str(exc)

    # Test invalid state error
    exc = error_code_to_exception(CECE_ERROR_INVALID_STATE, "Bad state")
    assert isinstance(exc, CeceStateError)
    assert exc.error_code == CECE_ERROR_INVALID_STATE

    # Test computation failed error
    exc = error_code_to_exception(CECE_ERROR_COMPUTATION_FAILED, "Compute failed")
    assert isinstance(exc, CeceComputationError)
    assert exc.error_code == CECE_ERROR_COMPUTATION_FAILED

    # Test memory allocation error
    exc = error_code_to_exception(CECE_ERROR_MEMORY_ALLOCATION, "Out of memory")
    assert isinstance(exc, CeceException)
    assert exc.error_code == CECE_ERROR_MEMORY_ALLOCATION
    assert "Memory allocation failed" in str(exc)

    # Test invalid execution space error
    exc = error_code_to_exception(CECE_ERROR_INVALID_EXECUTION_SPACE, "Bad space")
    assert isinstance(exc, CeceExecutionSpaceError)
    assert exc.error_code == CECE_ERROR_INVALID_EXECUTION_SPACE

    # Test not initialized error
    exc = error_code_to_exception(CECE_ERROR_NOT_INITIALIZED, "Not init")
    assert isinstance(exc, CeceException)
    assert exc.error_code == CECE_ERROR_NOT_INITIALIZED
    assert "not initialized" in str(exc).lower()

    # Test already initialized error
    exc = error_code_to_exception(CECE_ERROR_ALREADY_INITIALIZED, "Already init")
    assert isinstance(exc, CeceException)
    assert exc.error_code == CECE_ERROR_ALREADY_INITIALIZED
    assert "already initialized" in str(exc).lower()

    # Test field not found error
    exc = error_code_to_exception(CECE_ERROR_FIELD_NOT_FOUND, "Field missing")
    assert isinstance(exc, CeceStateError)
    assert exc.error_code == CECE_ERROR_FIELD_NOT_FOUND

    # Test dimension mismatch error
    exc = error_code_to_exception(CECE_ERROR_DIMENSION_MISMATCH, "Dims wrong")
    assert isinstance(exc, CeceStateError)
    assert exc.error_code == CECE_ERROR_DIMENSION_MISMATCH

    # Test unknown error code
    exc = error_code_to_exception(999, "Unknown error")
    assert isinstance(exc, CeceException)
    assert exc.error_code == 999
    assert "Unknown error" in str(exc)

    # Test error includes recovery suggestions
    exc = error_code_to_exception(CECE_ERROR_INVALID_CONFIG, "Bad config")
    assert len(exc.recovery_suggestions) > 0

    # Test error with C++ call site
    call_site = "cece_c_compute at cece_compute.cpp:123"
    exc = error_code_to_exception(
        CECE_ERROR_COMPUTATION_FAILED, "Compute failed", c_call_site=call_site
    )
    assert exc.c_call_site == call_site
    assert call_site in str(exc)

    print("✓ Error code mapping works correctly")


def test_recovery_suggestions():
    """Test recovery suggestions."""
    print("Testing recovery suggestions...")

    # Test that recovery suggestions exist for all error codes
    assert CECE_ERROR_INVALID_CONFIG in RECOVERY_SUGGESTIONS
    assert CECE_ERROR_INVALID_STATE in RECOVERY_SUGGESTIONS
    assert CECE_ERROR_COMPUTATION_FAILED in RECOVERY_SUGGESTIONS
    assert CECE_ERROR_MEMORY_ALLOCATION in RECOVERY_SUGGESTIONS
    assert CECE_ERROR_INVALID_EXECUTION_SPACE in RECOVERY_SUGGESTIONS
    assert CECE_ERROR_NOT_INITIALIZED in RECOVERY_SUGGESTIONS
    assert CECE_ERROR_ALREADY_INITIALIZED in RECOVERY_SUGGESTIONS
    assert CECE_ERROR_FIELD_NOT_FOUND in RECOVERY_SUGGESTIONS
    assert CECE_ERROR_DIMENSION_MISMATCH in RECOVERY_SUGGESTIONS

    # Test that all recovery suggestions are lists
    for code, suggestions in RECOVERY_SUGGESTIONS.items():
        assert isinstance(suggestions, list)
        assert len(suggestions) > 0
        for suggestion in suggestions:
            assert isinstance(suggestion, str)
            assert len(suggestion) > 0

    # Test specific suggestions
    config_suggestions = RECOVERY_SUGGESTIONS[CECE_ERROR_INVALID_CONFIG]
    assert any("YAML" in s or "yaml" in s for s in config_suggestions)

    exec_space_suggestions = RECOVERY_SUGGESTIONS[CECE_ERROR_INVALID_EXECUTION_SPACE]
    assert any("available" in s.lower() for s in exec_space_suggestions)

    not_init_suggestions = RECOVERY_SUGGESTIONS[CECE_ERROR_NOT_INITIALIZED]
    assert any("initialize" in s.lower() for s in not_init_suggestions)

    print("✓ Recovery suggestions are correct")


def test_exception_raising():
    """Test raising and catching exceptions."""
    print("Testing exception raising and catching...")

    # Test raising and catching CeceException
    try:
        raise CeceException("Test error")
    except CeceException as e:
        assert "Test error" in str(e)

    # Test raising and catching CeceConfigError
    try:
        raise CeceConfigError("Config error")
    except CeceConfigError as e:
        assert "Config error" in str(e)

    # Test catching CeceConfigError as CeceException
    try:
        raise CeceConfigError("Config error")
    except CeceException as e:
        assert isinstance(e, CeceConfigError)

    # Test exception message preserved
    message = "Test error message"
    try:
        raise CeceException(message)
    except CeceException as e:
        assert message in str(e)

    # Test exception error code preserved
    try:
        raise CeceException("Test", error_code=42)
    except CeceException as e:
        assert e.error_code == 42

    print("✓ Exception raising and catching works correctly")


def test_specific_exception_types():
    """Test specific exception types with default suggestions."""
    print("Testing specific exception types...")

    # Test CeceConfigError inherits suggestions
    exc = CeceConfigError("Invalid config")
    assert len(exc.recovery_suggestions) > 0
    assert any("YAML" in s for s in exc.recovery_suggestions)

    # Test CeceComputationError inherits suggestions
    exc = CeceComputationError("Computation failed")
    assert len(exc.recovery_suggestions) > 0

    # Test CeceStateError inherits suggestions
    exc = CeceStateError("Invalid state")
    assert len(exc.recovery_suggestions) > 0

    # Test CeceExecutionSpaceError inherits suggestions
    exc = CeceExecutionSpaceError("Invalid execution space")
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
