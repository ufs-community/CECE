"""
ACES Python Interface

A Python wrapper for the ACES (Atmospheric Chemistry Emissions System) C++ core.
Provides configuration management, state handling, and computation execution.

Example:
    >>> import aces
    >>> config = aces.load_config("config.yaml")
    >>> state = aces.AcesState(nx=144, ny=96, nz=72)
    >>> aces.initialize(config)
    >>> aces.compute(state, config)
    >>> result = state.get_export_field("CO_EMIS")
"""

__version__ = "0.1.0"
__all__ = [
    "initialize",
    "finalize",
    "is_initialized",
    "compute",
    "load_config",
    "set_execution_space",
    "get_execution_space",
    "get_available_execution_spaces",
    "set_log_level",
    "get_diagnostics",
    "reset_diagnostics",
    "get_last_error",
    "AcesConfig",
    "AcesState",
    "AcesField",
    "EmissionLayer",
    "VerticalDistributionConfig",
    "AcesException",
    "AcesConfigError",
    "AcesComputationError",
    "AcesStateError",
    "AcesExecutionSpaceError",
]

# Placeholder imports - will be implemented in subsequent tasks
from .exceptions import (
    AcesException,
    AcesConfigError,
    AcesComputationError,
    AcesStateError,
    AcesExecutionSpaceError,
)
from .config import AcesConfig, EmissionLayer, VerticalDistributionConfig
from .state import AcesState, AcesField
from .utils import load_config

# Module-level state
_aces_handle = None
_initialized = False


def initialize(config):
    """
    Initialize ACES with a configuration.

    Args:
        config: Configuration file path (str), YAML string, dict, or AcesConfig object

    Raises:
        AcesConfigError: If configuration is invalid
        RuntimeError: If ACES is already initialized
    """
    global _aces_handle, _initialized
    if _initialized:
        raise RuntimeError("ACES is already initialized. Call finalize() first.")

    from ._bindings import _c_bindings, _check_error
    from .utils import load_config as _load_config

    # Load C library if not already loaded
    if not _c_bindings.is_loaded():
        _c_bindings.load_library()

    # Load and validate configuration
    config_obj = _load_config(config)
    validation = config_obj.validate()
    if not validation.is_valid:
        raise AcesConfigError(f"Configuration validation failed:\n{validation}")

    # Serialize to YAML for C layer
    config_yaml = config_obj.to_yaml()

    # Call C initialization
    import ctypes
    handle = ctypes.c_void_p()
    error_code = _c_bindings.aces_c_initialize(config_yaml.encode("utf-8"), ctypes.byref(handle))
    _check_error(error_code)

    _aces_handle = handle
    _initialized = True


def finalize():
    """
    Clean up ACES resources.

    Raises:
        RuntimeError: If ACES is not initialized
    """
    global _aces_handle, _initialized
    if not _initialized:
        raise RuntimeError("ACES is not initialized.")

    from ._bindings import _c_bindings, _check_error

    if _aces_handle and _c_bindings.is_loaded():
        error_code = _c_bindings.aces_c_finalize(_aces_handle)
        _check_error(error_code)

    _initialized = False
    _aces_handle = None


def is_initialized():
    """Check if ACES is initialized."""
    return _initialized


def compute(state, config=None, hour=0, day_of_week=0, month=0):
    """
    Execute ACES computation.

    Args:
        state: AcesState object with import fields
        config: Optional AcesConfig object (uses initialized config if None)
        hour: Hour of day (0-23) for temporal scaling
        day_of_week: Day of week (0-6) for temporal scaling
        month: Month (1-12) for temporal scaling

    Raises:
        RuntimeError: If ACES is not initialized or computation fails
        AcesStateError: If state is invalid
    """
    if not _initialized:
        raise RuntimeError("ACES is not initialized. Call initialize() first.")

    from ._bindings import _c_bindings, _check_error, _release_gil
    import ctypes
    import numpy as np

    # Validate state
    if not isinstance(state, AcesState):
        raise AcesStateError("state must be an AcesState object")

    # Create C state
    state_handle = ctypes.c_void_p()
    error_code = _c_bindings.aces_c_state_create(
        state.nx, state.ny, state.nz, ctypes.byref(state_handle)
    )
    _check_error(error_code)

    try:
        # Add import fields to C state
        for field_name, field_array in state.import_fields.items():
            # Ensure array is Fortran-contiguous
            if not field_array.flags["F_CONTIGUOUS"]:
                field_array = np.asfortranarray(field_array)

            # Get pointer to data
            data_ptr = field_array.ctypes.data_as(ctypes.POINTER(ctypes.c_double))

            error_code = _c_bindings.aces_c_state_add_import_field(
                state_handle,
                field_name.encode("utf-8"),
                data_ptr,
                state.nx,
                state.ny,
                state.nz,
            )
            _check_error(error_code)

        # Execute computation with GIL released
        with _release_gil():
            error_code = _c_bindings.aces_c_compute(
                _aces_handle, state_handle, hour, day_of_week, month
            )
        _check_error(error_code)

        # Retrieve export fields
        # Implementation: get export field pointers and wrap as numpy arrays
        # This will be completed in subsequent tasks

    finally:
        # Clean up C state
        if state_handle:
            _c_bindings.aces_c_state_destroy(state_handle)


def load_config(config):
    """
    Load configuration from file, YAML string, or dict.

    Args:
        config: File path (str), YAML string, dict, or AcesConfig object

    Returns:
        AcesConfig object

    Raises:
        AcesConfigError: If configuration is invalid
    """
    from .utils import load_config as _load_config
    return _load_config(config)


def set_execution_space(space):
    """
    Set Kokkos execution space.

    Args:
        space: Execution space name ("Serial", "OpenMP", "CUDA", "HIP")

    Raises:
        AcesExecutionSpaceError: If space is not available
    """
    from ._bindings import _c_bindings, _check_error
    if not _c_bindings.is_loaded():
        _c_bindings.load_library()
    error_code = _c_bindings.aces_c_set_execution_space(space.encode("utf-8"))
    _check_error(error_code)


def get_execution_space():
    """
    Get current Kokkos execution space.

    Returns:
        Execution space name string

    Raises:
        RuntimeError: If unable to query execution space
    """
    from ._bindings import _c_bindings, _check_error
    if not _c_bindings.is_loaded():
        _c_bindings.load_library()
    import ctypes
    space_ptr = ctypes.c_char_p()
    error_code = _c_bindings.aces_c_get_execution_space(ctypes.byref(space_ptr))
    _check_error(error_code)
    if space_ptr.value:
        space = space_ptr.value.decode("utf-8")
        _c_bindings.aces_c_free_string(space_ptr)
        return space
    return "Unknown"


def get_available_execution_spaces():
    """
    Get list of available Kokkos execution spaces.

    Returns:
        List of execution space names

    Raises:
        RuntimeError: If unable to query available spaces
    """
    from ._bindings import _c_bindings, _check_error
    if not _c_bindings.is_loaded():
        _c_bindings.load_library()
    import ctypes
    import json
    spaces_ptr = ctypes.c_char_p()
    error_code = _c_bindings.aces_c_get_available_execution_spaces(ctypes.byref(spaces_ptr))
    _check_error(error_code)
    if spaces_ptr.value:
        spaces_json = spaces_ptr.value.decode("utf-8")
        _c_bindings.aces_c_free_string(spaces_ptr)
        try:
            return json.loads(spaces_json)
        except json.JSONDecodeError:
            # Fallback: return as list if not JSON
            return spaces_json.split(",")
    return ["Serial"]


def set_log_level(level):
    """
    Set logging level.

    Args:
        level: Log level ("DEBUG", "INFO", "WARNING", "ERROR")

    Raises:
        ValueError: If level is invalid
    """
    from ._bindings import _c_bindings, _check_error
    if not _c_bindings.is_loaded():
        _c_bindings.load_library()
    valid_levels = ["DEBUG", "INFO", "WARNING", "ERROR"]
    if level.upper() not in valid_levels:
        raise ValueError(f"Invalid log level: {level}. Must be one of {valid_levels}")
    error_code = _c_bindings.aces_c_set_log_level(level.upper().encode("utf-8"))
    _check_error(error_code)


def get_diagnostics():
    """
    Get performance and timing diagnostics.

    Returns:
        Dict with diagnostic information

    Raises:
        RuntimeError: If unable to retrieve diagnostics
    """
    from ._bindings import _c_bindings, _check_error
    if not _c_bindings.is_loaded():
        _c_bindings.load_library()
    import ctypes
    import json
    diag_ptr = ctypes.c_char_p()
    error_code = _c_bindings.aces_c_get_diagnostics(ctypes.byref(diag_ptr))
    _check_error(error_code)
    if diag_ptr.value:
        diag_json = diag_ptr.value.decode("utf-8")
        _c_bindings.aces_c_free_string(diag_ptr)
        try:
            return json.loads(diag_json)
        except json.JSONDecodeError:
            return {"raw": diag_json}
    return {}


def reset_diagnostics():
    """
    Clear diagnostic data.

    Raises:
        RuntimeError: If unable to reset diagnostics
    """
    from ._bindings import _c_bindings, _check_error
    if not _c_bindings.is_loaded():
        _c_bindings.load_library()
    error_code = _c_bindings.aces_c_reset_diagnostics()
    _check_error(error_code)


def get_last_error():
    """
    Get last error message or None.

    Returns:
        Error message string or None if no error
    """
    from ._bindings import _c_bindings
    if not _c_bindings.is_loaded():
        return None
    return _c_bindings.get_last_error()
