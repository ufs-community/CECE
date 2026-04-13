"""
Exception classes for the CECE Python interface.

Defines a custom exception hierarchy for CECE errors, error code constants
from the C binding layer, and recovery suggestion mappings. All CECE-specific
exceptions inherit from ``CeceException``.

Notes
-----
Error codes are retained from the original C-linkage binding layer for
backward compatibility, even though the pybind11 module uses exception
translation instead of error codes.

See Also
--------
cece.initialize : May raise ``CeceConfigError``.
cece.compute : May raise ``CeceComputationError`` or ``CeceStateError``.
"""

from __future__ import annotations

from typing import Optional, List

# Error codes from C binding layer
CECE_SUCCESS = 0
CECE_ERROR_INVALID_CONFIG = 1
CECE_ERROR_INVALID_STATE = 2
CECE_ERROR_COMPUTATION_FAILED = 3
CECE_ERROR_MEMORY_ALLOCATION = 4
CECE_ERROR_INVALID_EXECUTION_SPACE = 5
CECE_ERROR_NOT_INITIALIZED = 6
CECE_ERROR_ALREADY_INITIALIZED = 7
CECE_ERROR_FIELD_NOT_FOUND = 8
CECE_ERROR_DIMENSION_MISMATCH = 9
CECE_ERROR_UNKNOWN = 255


# Recovery suggestions for each error type
RECOVERY_SUGGESTIONS = {
    CECE_ERROR_INVALID_CONFIG: [
        "Check that the configuration file is valid YAML",
        "Verify all required fields are present in the configuration",
        "Call config.validate() to check for errors before initializing",
    ],
    CECE_ERROR_INVALID_STATE: [
        "Ensure all required import fields are added to the state",
        "Check that field dimensions match the state dimensions",
        "Verify that field data types are float64 (double)",
    ],
    CECE_ERROR_COMPUTATION_FAILED: [
        "Check that CECE is initialized with a valid configuration",
        "Verify that all required import fields are present in the state",
        "Check the log output for more details about the failure",
        "Try running with a simpler configuration to isolate the issue",
    ],
    CECE_ERROR_MEMORY_ALLOCATION: [
        "Check available system memory",
        "Try reducing the problem size (nx, ny, nz dimensions)",
        "Close other applications to free up memory",
    ],
    CECE_ERROR_INVALID_EXECUTION_SPACE: [
        "Call cece.get_available_execution_spaces() to see available options",
        "Check that your build includes support for the requested execution space",
        "For GPU execution, verify that a GPU is available and drivers are installed",
    ],
    CECE_ERROR_NOT_INITIALIZED: [
        "Call cece.initialize(config) before calling other CECE functions",
        "Check that the configuration is valid",
    ],
    CECE_ERROR_ALREADY_INITIALIZED: [
        "Call cece.finalize() before calling initialize() again",
        "Or use a different CECE instance for multiple computations",
    ],
    CECE_ERROR_FIELD_NOT_FOUND: [
        "Check the field name spelling",
        "Call state.import_fields.keys() or state.export_fields.keys() to see available fields",
    ],
    CECE_ERROR_DIMENSION_MISMATCH: [
        "Ensure array dimensions match the state dimensions (nx, ny, nz)",
        "Check state.dimensions to see the expected dimensions",
    ],
}


class CeceException(Exception):
    """
    Base exception for all CECE errors.

    Provides structured error information including an optional error code,
    recovery suggestions, and C++ call site information for debugging.

    Parameters
    ----------
    message : str
        Human-readable error message from the C++ core or Python layer.
    error_code : int or None, optional
        Numeric error code from the C binding layer. Default is ``None``.
    recovery_suggestions : list of str or None, optional
        Actionable suggestions for resolving the error. Default is ``None``.
    c_call_site : str or None, optional
        C++ source location where the error originated. Default is ``None``.

    Attributes
    ----------
    message : str
        The original error message.
    error_code : int or None
        Numeric error code, if available.
    recovery_suggestions : list of str
        List of recovery suggestions.
    c_call_site : str or None
        C++ call site information.

    Examples
    --------
    >>> raise CeceException("Something went wrong", error_code=255)
    """

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
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

        Returns
        -------
        str
            Formatted multi-line error message including error code, C++ call
            site, and numbered recovery suggestions.
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
        """Return the formatted error message."""
        return self.args[0] if self.args else self.message


class CeceConfigError(CeceException):
    """
    Configuration validation or parsing error.

    Raised when an CECE configuration file or object is invalid, contains
    missing required fields, or fails schema validation.

    Parameters
    ----------
    message : str
        Description of the configuration error.
    error_code : int or None, optional
        Numeric error code. Default is ``None``.
    recovery_suggestions : list of str or None, optional
        Suggestions for fixing the configuration. If ``None``, default
        suggestions for config errors are used.
    c_call_site : str or None, optional
        C++ source location. Default is ``None``.
    """

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        if recovery_suggestions is None:
            recovery_suggestions = RECOVERY_SUGGESTIONS.get(
                error_code, RECOVERY_SUGGESTIONS.get(CECE_ERROR_INVALID_CONFIG)
            )
        super().__init__(message, error_code, recovery_suggestions, c_call_site)


class CeceComputationError(CeceException):
    """
    Error during emission computation execution.

    Raised when the CECE stacking engine or physics schemes encounter a
    failure during the compute phase.

    Parameters
    ----------
    message : str
        Description of the computation error.
    error_code : int or None, optional
        Numeric error code. Default is ``None``.
    recovery_suggestions : list of str or None, optional
        Suggestions for resolving the computation failure. If ``None``,
        default suggestions for computation errors are used.
    c_call_site : str or None, optional
        C++ source location. Default is ``None``.
    """

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        if recovery_suggestions is None:
            recovery_suggestions = RECOVERY_SUGGESTIONS.get(
                error_code, RECOVERY_SUGGESTIONS.get(CECE_ERROR_COMPUTATION_FAILED)
            )
        super().__init__(message, error_code, recovery_suggestions, c_call_site)


class CeceStateError(CeceException):
    """
    Error in state management.

    Raised when import or export field operations fail due to missing fields,
    dimension mismatches, or invalid data types.

    Parameters
    ----------
    message : str
        Description of the state error.
    error_code : int or None, optional
        Numeric error code. Default is ``None``.
    recovery_suggestions : list of str or None, optional
        Suggestions for resolving the state issue. If ``None``, default
        suggestions for state errors are used.
    c_call_site : str or None, optional
        C++ source location. Default is ``None``.
    """

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        if recovery_suggestions is None:
            recovery_suggestions = RECOVERY_SUGGESTIONS.get(
                error_code, RECOVERY_SUGGESTIONS.get(CECE_ERROR_INVALID_STATE)
            )
        super().__init__(message, error_code, recovery_suggestions, c_call_site)


class CeceExecutionSpaceError(CeceException):
    """
    Error in Kokkos execution space configuration.

    Raised when a requested execution space (e.g., CUDA, OpenMP) is not
    available in the current CECE build.

    Parameters
    ----------
    message : str
        Description of the execution space error.
    error_code : int or None, optional
        Numeric error code. Default is ``None``.
    recovery_suggestions : list of str or None, optional
        Suggestions for resolving the execution space issue. If ``None``,
        default suggestions for execution space errors are used.
    c_call_site : str or None, optional
        C++ source location. Default is ``None``.
    """

    def __init__(
        self,
        message: str,
        error_code: Optional[int] = None,
        recovery_suggestions: Optional[List[str]] = None,
        c_call_site: Optional[str] = None,
    ):
        if recovery_suggestions is None:
            recovery_suggestions = RECOVERY_SUGGESTIONS.get(
                error_code, RECOVERY_SUGGESTIONS.get(CECE_ERROR_INVALID_EXECUTION_SPACE)
            )
        super().__init__(message, error_code, recovery_suggestions, c_call_site)


def error_code_to_exception(
    error_code: int,
    message: str,
    c_call_site: Optional[str] = None,
) -> Optional[CeceException]:
    """
    Convert a C error code to the appropriate Python exception.

    Maps numeric error codes from the C binding layer to the corresponding
    CECE exception class, attaching default recovery suggestions.

    Parameters
    ----------
    error_code : int
        Error code from the C binding layer (e.g., ``CECE_ERROR_INVALID_CONFIG``).
    message : str
        Error message from the C layer.
    c_call_site : str or None, optional
        C++ call site information for traceback. Default is ``None``.

    Returns
    -------
    CeceException or None
        An exception instance of the appropriate subclass, or ``None`` if
        ``error_code`` is ``CECE_SUCCESS``.

    Examples
    --------
    >>> exc = error_code_to_exception(1, "Bad config")
    >>> type(exc)
    <class 'cece.exceptions.CeceConfigError'>
    """
    if error_code == CECE_SUCCESS:
        return None

    # Get default recovery suggestions for this error code
    suggestions = RECOVERY_SUGGESTIONS.get(error_code, [])

    if error_code == CECE_ERROR_INVALID_CONFIG:
        return CeceConfigError(message, error_code, suggestions, c_call_site)
    elif error_code == CECE_ERROR_INVALID_STATE:
        return CeceStateError(message, error_code, suggestions, c_call_site)
    elif error_code == CECE_ERROR_COMPUTATION_FAILED:
        return CeceComputationError(message, error_code, suggestions, c_call_site)
    elif error_code == CECE_ERROR_MEMORY_ALLOCATION:
        return CeceException(
            f"Memory allocation failed: {message}",
            error_code,
            suggestions,
            c_call_site,
        )
    elif error_code == CECE_ERROR_INVALID_EXECUTION_SPACE:
        return CeceExecutionSpaceError(message, error_code, suggestions, c_call_site)
    elif error_code == CECE_ERROR_NOT_INITIALIZED:
        return CeceException(
            f"CECE not initialized: {message}",
            error_code,
            suggestions,
            c_call_site,
        )
    elif error_code == CECE_ERROR_ALREADY_INITIALIZED:
        return CeceException(
            f"CECE already initialized: {message}",
            error_code,
            suggestions,
            c_call_site,
        )
    elif error_code == CECE_ERROR_FIELD_NOT_FOUND:
        return CeceStateError(message, error_code, suggestions, c_call_site)
    elif error_code == CECE_ERROR_DIMENSION_MISMATCH:
        return CeceStateError(message, error_code, suggestions, c_call_site)
    else:
        return CeceException(
            f"Unknown error: {message}",
            error_code,
            RECOVERY_SUGGESTIONS.get(CECE_ERROR_UNKNOWN, []),
            c_call_site,
        )
