"""
Exception classes for ACES Python interface.

Defines custom exception hierarchy and error code mapping.
"""

from typing import Optional, List

# Error codes from C binding layer
ACES_SUCCESS = 0
ACES_ERROR_INVALID_CONFIG = 1
ACES_ERROR_INVALID_STATE = 2
ACES_ERROR_COMPUTATION_FAILED = 3
ACES_ERROR_MEMORY_ALLOCATION = 4
ACES_ERROR_INVALID_EXECUTION_SPACE = 5
ACES_ERROR_NOT_INITIALIZED = 6
ACES_ERROR_ALREADY_INITIALIZED = 7
ACES_ERROR_FIELD_NOT_FOUND = 8
ACES_ERROR_DIMENSION_MISMATCH = 9
ACES_ERROR_UNKNOWN = 255


# Recovery suggestions for each error type
RECOVERY_SUGGESTIONS = {
    ACES_ERROR_INVALID_CONFIG: [
        "Check that the configuration file is valid YAML",
        "Verify all required fields are present in the configuration",
        "Call config.validate() to check for errors before initializing",
    ],
    ACES_ERROR_INVALID_STATE: [
        "Ensure all required import fields are added to the state",
        "Check that field dimensions match the state dimensions",
        "Verify that field data types are float64 (double)",
    ],
    ACES_ERROR_COMPUTATION_FAILED: [
        "Check that ACES is initialized with a valid configuration",
        "Verify that all required import fields are present in the state",
        "Check the log output for more details about the failure",
        "Try running with a simpler configuration to isolate the issue",
    ],
    ACES_ERROR_MEMORY_ALLOCATION: [
        "Check available system memory",
        "Try reducing the problem size (nx, ny, nz dimensions)",
        "Close other applications to free up memory",
    ],
    ACES_ERROR_INVALID_EXECUTION_SPACE: [
        "Call aces.get_available_execution_spaces() to see available options",
        "Check that your build includes support for the requested execution space",
        "For GPU execution, verify that a GPU is available and drivers are installed",
    ],
    ACES_ERROR_NOT_INITIALIZED: [
        "Call aces.initialize(config) before calling other ACES functions",
        "Check that the configuration is valid",
    ],
    ACES_ERROR_ALREADY_INITIALIZED: [
        "Call aces.finalize() before calling initialize() again",
        "Or use a different ACES instance for multiple computations",
    ],
    ACES_ERROR_FIELD_NOT_FOUND: [
        "Check the field name spelling",
        "Call state.import_fields.keys() or state.export_fields.keys() to see available fields",
    ],
    ACES_ERROR_DIMENSION_MISMATCH: [
        "Ensure array dimensions match the state dimensions (nx, ny, nz)",
        "Check state.dimensions to see the expected dimensions",
    ],
}


class AcesException(Exception):
    """Base exception for all ACES errors."""

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        """
        Initialize exception with comprehensive error information.

        Args:
            message: Error message from C++ code
            error_code: Optional error code from C binding layer
            recovery_suggestions: Optional list of recovery suggestions
            c_call_site: Optional C++ call site information for traceback
        """
        self.message = message
        self.error_code = error_code
        self.recovery_suggestions = recovery_suggestions or []
        self.c_call_site = c_call_site

        # Format the full error message
        formatted_message = self._format_message()
        super().__init__(formatted_message)

    def _format_message(self) -> str:
        """
        Format the complete error message with code, suggestions, and traceback.

        Returns:
            Formatted error message string
        """
        lines = []

        # Add error code if present
        if self.error_code is not None:
            lines.append(f"[Error Code {self.error_code}] {self.message}")
        else:
            lines.append(self.message)

        # Add C++ call site if available
        if self.c_call_site:
            lines.append(f"\nC++ Call Site:\n  {self.c_call_site}")

        # Add recovery suggestions
        if self.recovery_suggestions:
            lines.append("\nRecovery Suggestions:")
            for i, suggestion in enumerate(self.recovery_suggestions, 1):
                lines.append(f"  {i}. {suggestion}")

        return "\n".join(lines)

    def __str__(self) -> str:
        """Return formatted error message."""
        return self.args[0] if self.args else self.message


class AcesConfigError(AcesException):
    """Configuration validation or parsing error."""

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        """Initialize configuration error with recovery suggestions."""
        if recovery_suggestions is None:
            recovery_suggestions = RECOVERY_SUGGESTIONS.get(
                error_code, RECOVERY_SUGGESTIONS.get(ACES_ERROR_INVALID_CONFIG)
            )
        super().__init__(message, error_code, recovery_suggestions, c_call_site)


class AcesComputationError(AcesException):
    """Error during computation execution."""

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        """Initialize computation error with recovery suggestions."""
        if recovery_suggestions is None:
            recovery_suggestions = RECOVERY_SUGGESTIONS.get(
                error_code, RECOVERY_SUGGESTIONS.get(ACES_ERROR_COMPUTATION_FAILED)
            )
        super().__init__(message, error_code, recovery_suggestions, c_call_site)


class AcesStateError(AcesException):
    """Error in state management."""

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        """Initialize state error with recovery suggestions."""
        if recovery_suggestions is None:
            recovery_suggestions = RECOVERY_SUGGESTIONS.get(
                error_code, RECOVERY_SUGGESTIONS.get(ACES_ERROR_INVALID_STATE)
            )
        super().__init__(message, error_code, recovery_suggestions, c_call_site)


class AcesExecutionSpaceError(AcesException):
    """Error in execution space configuration."""

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        """Initialize execution space error with recovery suggestions."""
        if recovery_suggestions is None:
            recovery_suggestions = RECOVERY_SUGGESTIONS.get(
                error_code, RECOVERY_SUGGESTIONS.get(ACES_ERROR_INVALID_EXECUTION_SPACE)
            )
        super().__init__(message, error_code, recovery_suggestions, c_call_site)


def error_code_to_exception(
    error_code: int,
    message: str,
    c_call_site: Optional[str] = None,
) -> Optional[AcesException]:
    """
    Convert C error code to Python exception with recovery suggestions.

    Args:
        error_code: Error code from C binding layer
        message: Error message from C layer
        c_call_site: Optional C++ call site information for traceback

    Returns:
        Exception instance of appropriate type, or None if success
    """
    if error_code == ACES_SUCCESS:
        return None

    # Get default recovery suggestions for this error code
    suggestions = RECOVERY_SUGGESTIONS.get(error_code, [])

    if error_code == ACES_ERROR_INVALID_CONFIG:
        return AcesConfigError(message, error_code, suggestions, c_call_site)
    elif error_code == ACES_ERROR_INVALID_STATE:
        return AcesStateError(message, error_code, suggestions, c_call_site)
    elif error_code == ACES_ERROR_COMPUTATION_FAILED:
        return AcesComputationError(message, error_code, suggestions, c_call_site)
    elif error_code == ACES_ERROR_MEMORY_ALLOCATION:
        return AcesException(
            f"Memory allocation failed: {message}",
            error_code,
            suggestions,
            c_call_site,
        )
    elif error_code == ACES_ERROR_INVALID_EXECUTION_SPACE:
        return AcesExecutionSpaceError(message, error_code, suggestions, c_call_site)
    elif error_code == ACES_ERROR_NOT_INITIALIZED:
        return AcesException(
            f"ACES not initialized: {message}",
            error_code,
            suggestions,
            c_call_site,
        )
    elif error_code == ACES_ERROR_ALREADY_INITIALIZED:
        return AcesException(
            f"ACES already initialized: {message}",
            error_code,
            suggestions,
            c_call_site,
        )
    elif error_code == ACES_ERROR_FIELD_NOT_FOUND:
        return AcesStateError(message, error_code, suggestions, c_call_site)
    elif error_code == ACES_ERROR_DIMENSION_MISMATCH:
        return AcesStateError(message, error_code, suggestions, c_call_site)
    else:
        return AcesException(
            f"Unknown error: {message}",
            error_code,
            RECOVERY_SUGGESTIONS.get(ACES_ERROR_UNKNOWN, []),
            c_call_site,
        )
