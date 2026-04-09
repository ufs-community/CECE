"""
ACES Python Interface.

A Python wrapper for the ACES (Atmospheric Chemistry Emissions System) C++ core.
Provides configuration management, state handling, and computation execution
through pybind11 bindings to the underlying C++ library.

The package exposes a high-level API for initializing ACES, loading
configurations, managing import/export state fields, and executing emission
computations with automatic GIL release during C++ kernel execution.

Examples
--------
>>> import aces
>>> config = aces.load_config("config.yaml")
>>> state = aces.AcesState(nx=144, ny=96, nz=72)
>>> aces.initialize(config)
>>> aces.compute(state, config)
>>> result = state.get_export_field("CO_EMIS")
"""

from __future__ import annotations

from typing import Optional, Union

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

# Import the pybind11 C++ bindings module
from . import _aces_core

# Module-level state
_cpp_config: Optional[object] = None
_initialized: bool = False
_last_error: Optional[str] = None


def initialize(config: Union[str, dict, AcesConfig]) -> None:
    """
    Initialize ACES with a configuration.

    Parses and validates the provided configuration, initializes Kokkos if
    needed, and prepares the C++ core for computation. ACES must be finalized
    before it can be re-initialized.

    Parameters
    ----------
    config : str, dict, or AcesConfig
        Configuration source. Can be a file path to a YAML file, a YAML
        string, a configuration dictionary, or an ``AcesConfig`` object.

    Raises
    ------
    AcesConfigError
        If the configuration is invalid or fails validation.
    RuntimeError
        If ACES is already initialized.

    See Also
    --------
    finalize : Clean up ACES resources.
    is_initialized : Check initialization status.

    Examples
    --------
    >>> aces.initialize("config.yaml")
    >>> aces.initialize({"species": {"CO": [...]}})
    """
    global _cpp_config, _initialized, _last_error
    if _initialized:
        raise RuntimeError("ACES is already initialized. Call finalize() first.")

    from .utils import load_config as _load_config
    from pathlib import Path

    # Ensure Kokkos is initialized
    if not _aces_core.is_kokkos_initialized():
        _aces_core.initialize_kokkos()

    # Load and validate configuration
    config_obj = _load_config(config)
    validation = config_obj.validate()
    if not validation.is_valid:
        raise AcesConfigError(f"Configuration validation failed:\n{validation}")

    # Create C++ AcesConfig via pybind11
    # If config is a file path, use ParseConfig directly
    if isinstance(config, str):
        path = Path(config)
        if path.exists() and path.is_file():
            try:
                _cpp_config = _aces_core.ParseConfig(str(path))
            except Exception as e:
                raise AcesConfigError(f"Failed to parse config file: {e}")
        else:
            # YAML string — serialize to temp file or build config from dict
            _cpp_config = _build_cpp_config(config_obj)
    else:
        _cpp_config = _build_cpp_config(config_obj)

    _initialized = True
    _last_error = None


def _build_cpp_config(config_obj: AcesConfig) -> object:
    """
    Build a C++ AcesConfig from a Python AcesConfig object.

    Translates the Python-side configuration into the corresponding C++
    ``_aces_core.AcesConfig`` struct, including species layers and vertical
    distribution settings.

    Parameters
    ----------
    config_obj : AcesConfig
        Python configuration object to translate.

    Returns
    -------
    _aces_core.AcesConfig
        C++ configuration object populated from ``config_obj``.
    """
    cpp_config = _aces_core.AcesConfig()

    # Add species
    for species_name, layers in config_obj.species.items():
        cpp_layers = []
        for layer in layers:
            cpp_layer = _aces_core.EmissionLayer()
            cpp_layer.operation = layer.operation
            cpp_layer.field_name = layer.field_name
            cpp_layer.scale = layer.scale
            cpp_layer.vdist_method = _aces_core.VerticalDistributionMethod.SINGLE
            if hasattr(layer, 'vdist') and layer.vdist:
                method_map = {
                    "single": _aces_core.VerticalDistributionMethod.SINGLE,
                    "range": _aces_core.VerticalDistributionMethod.RANGE,
                    "pressure": _aces_core.VerticalDistributionMethod.PRESSURE,
                    "height": _aces_core.VerticalDistributionMethod.HEIGHT,
                    "pbl": _aces_core.VerticalDistributionMethod.PBL,
                }
                cpp_layer.vdist_method = method_map.get(
                    layer.vdist.method, _aces_core.VerticalDistributionMethod.SINGLE
                )
                cpp_layer.vdist_layer_start = layer.vdist.layer_start
                cpp_layer.vdist_layer_end = layer.vdist.layer_end
                cpp_layer.vdist_p_start = layer.vdist.p_start
                cpp_layer.vdist_p_end = layer.vdist.p_end
                cpp_layer.vdist_h_start = layer.vdist.h_start
                cpp_layer.vdist_h_end = layer.vdist.h_end
            cpp_layers.append(cpp_layer)
        _aces_core.AddSpecies(cpp_config, species_name, cpp_layers)

    return cpp_config


def finalize() -> None:
    """
    Clean up ACES resources and reset module state.

    Releases the C++ configuration object and resets the initialization flag.
    Must be called before ``initialize`` can be called again.

    Raises
    ------
    RuntimeError
        If ACES is not currently initialized.

    See Also
    --------
    initialize : Initialize ACES with a configuration.
    is_initialized : Check initialization status.
    """
    global _cpp_config, _initialized, _last_error
    if not _initialized:
        raise RuntimeError("ACES is not initialized.")

    _cpp_config = None
    _initialized = False
    _last_error = None


def is_initialized() -> bool:
    """
    Check whether ACES is currently initialized.

    Returns
    -------
    bool
        ``True`` if ACES has been initialized and not yet finalized.
    """
    return _initialized


def compute(
    state: AcesState,
    config: Optional[Union[str, dict, AcesConfig]] = None,
    hour: int = 0,
    day_of_week: int = 0,
    month: int = 0,
) -> None:
    """
    Execute ACES emission computation.

    Runs the ACES stacking engine and physics schemes on the provided state.
    The GIL is released automatically during the C++ computation, allowing
    other Python threads to run concurrently.

    Parameters
    ----------
    state : AcesState
        State object containing import fields. Export fields are populated
        in-place after computation completes.
    config : str, dict, AcesConfig, or None, optional
        Configuration override. If ``None``, uses the configuration from
        ``initialize()``. Default is ``None``.
    hour : int, optional
        Hour of day (0--23) for temporal scaling. Default is 0.
    day_of_week : int, optional
        Day of week (0--6, Monday=0) for temporal scaling. Default is 0.
    month : int, optional
        Month (1--12) for temporal scaling. Default is 0.

    Raises
    ------
    RuntimeError
        If ACES is not initialized or the computation fails.
    AcesStateError
        If ``state`` is not a valid ``AcesState`` object.

    See Also
    --------
    initialize : Initialize ACES before computing.
    AcesState : Container for import and export fields.

    Examples
    --------
    >>> state = aces.AcesState(nx=144, ny=96, nz=72)
    >>> state.add_import_field("TEMPERATURE", temp_array)
    >>> aces.compute(state, hour=12, month=7)
    >>> co_emis = state.get_export_field("CO_EMIS")
    """
    global _last_error
    if not _initialized:
        raise RuntimeError("ACES is not initialized. Call initialize() first.")

    import numpy as np

    # Validate state
    if not isinstance(state, AcesState):
        raise AcesStateError("state must be an AcesState object")

    # Determine which config to use
    if config is not None:
        from .utils import load_config as _load_config
        config_obj = _load_config(config)
        cpp_config = _build_cpp_config(config_obj)
    else:
        cpp_config = _cpp_config

    # Create C++ import state and populate fields
    import_state = _aces_core.AcesImportState()
    for field_name, field_array in state.import_fields.items():
        arr = field_array
        if not isinstance(arr, np.ndarray):
            arr = np.array(arr, dtype=np.float64)
        if arr.dtype != np.float64:
            arr = arr.astype(np.float64)
        if not arr.flags["F_CONTIGUOUS"]:
            arr = np.asfortranarray(arr)
        import_state.set_field(field_name, arr)

    # Create C++ export state
    export_state = _aces_core.AcesExportState()

    # Pre-populate export fields for each species so the resolver can find them
    for species_name in cpp_config.species_layers:
        export_data = np.asfortranarray(
            np.zeros((state.nx, state.ny, state.nz), dtype=np.float64)
        )
        export_state.set_field(species_name, export_data)

    # Create resolver with met_mapping from config
    met_mapping = dict(cpp_config.met_mapping) if cpp_config.met_mapping else {}
    sf_mapping = dict(cpp_config.scale_factor_mapping) if cpp_config.scale_factor_mapping else {}
    mask_mapping = dict(cpp_config.mask_mapping) if cpp_config.mask_mapping else {}

    resolver = _aces_core.AcesStateResolver(
        import_state, export_state, met_mapping, sf_mapping, mask_mapping
    )

    # Execute computation (GIL is released automatically by pybind11)
    try:
        _aces_core.compute_emissions(
            cpp_config, resolver,
            state.nx, state.ny, state.nz,
            hour, day_of_week, month,
        )
    except Exception as e:
        _last_error = str(e)
        raise

    # Retrieve export fields back into the Python state
    for field_name in export_state.get_field_names():
        export_array = export_state.get_field(field_name)
        state._export_fields[field_name] = AcesField(field_name, export_array)


def set_execution_space(space: str) -> None:
    """
    Set the Kokkos execution space.

    Validates that the requested execution space is available in the current
    build. Note that the Kokkos execution space is determined at compile time
    and cannot be changed at runtime; this function validates availability.

    Parameters
    ----------
    space : str
        Execution space name. One of ``"Serial"``, ``"OpenMP"``, ``"CUDA"``,
        or ``"HIP"``.

    Raises
    ------
    AcesExecutionSpaceError
        If the requested execution space is not available in the current build.

    See Also
    --------
    get_execution_space : Get the current execution space.
    get_available_execution_spaces : List all available execution spaces.
    """
    available = _aces_core.get_available_execution_spaces()
    if space not in available:
        raise AcesExecutionSpaceError(
            f"Execution space '{space}' is not available. "
            f"Available spaces: {available}"
        )
    # Note: Kokkos execution space is set at compile time and cannot be changed
    # at runtime. This validates the requested space is available.


def get_execution_space() -> str:
    """
    Get the current Kokkos execution space name.

    Returns
    -------
    str
        Name of the default Kokkos execution space (e.g., ``"Serial"``,
        ``"OpenMP"``).

    See Also
    --------
    set_execution_space : Set the execution space.
    get_available_execution_spaces : List all available execution spaces.
    """
    return _aces_core.get_default_execution_space_name()


def get_available_execution_spaces() -> list:
    """
    Get the list of available Kokkos execution spaces.

    Returns the execution spaces that were compiled into the current ACES
    build.

    Returns
    -------
    list of str
        Names of available execution spaces (e.g., ``["Serial", "OpenMP"]``).

    See Also
    --------
    get_execution_space : Get the current execution space.
    set_execution_space : Set the execution space.
    """
    return list(_aces_core.get_available_execution_spaces())


def set_log_level(level: str) -> None:
    """
    Set the ACES C++ logging level.

    Parameters
    ----------
    level : str
        Log level string. Must be one of ``"DEBUG"``, ``"INFO"``,
        ``"WARNING"``, or ``"ERROR"`` (case-insensitive).

    Raises
    ------
    ValueError
        If ``level`` is not a recognized log level.

    See Also
    --------
    get_diagnostics : Retrieve diagnostic information.

    Examples
    --------
    >>> aces.set_log_level("DEBUG")
    """
    valid_levels = ["DEBUG", "INFO", "WARNING", "ERROR"]
    if level.upper() not in valid_levels:
        raise ValueError(f"Invalid log level: {level}. Must be one of {valid_levels}")
    _aces_core.set_log_level(level.upper())


def get_diagnostics() -> dict:
    """
    Get performance and timing diagnostics.

    Returns
    -------
    dict
        Dictionary containing diagnostic information such as timing data
        and performance counters. Currently returns an empty dict as
        diagnostics are not yet exposed via pybind11.
    """
    # pybind11 module does not expose a diagnostics endpoint yet;
    # return empty dict as a safe default.
    return {}


def reset_diagnostics() -> None:
    """
    Clear accumulated diagnostic data.

    Notes
    -----
    Currently a no-op as diagnostics are not yet exposed via pybind11.
    """
    # No-op: diagnostics are not yet exposed via pybind11.
    pass


def get_last_error() -> Optional[str]:
    """
    Get the last error message, if any.

    Returns
    -------
    str or None
        The error message from the most recent failed operation, or ``None``
        if no error has occurred since the last ``initialize`` or ``finalize``.
    """
    return _last_error
