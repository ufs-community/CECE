"""
Utility functions for the CECE Python interface.

Provides configuration loading from multiple sources (files, YAML strings,
dicts), array validation helpers, YAML serialization/deserialization, and
logging setup.

See Also
--------
cece.config.CeceConfig : Configuration class used by these utilities.
cece.initialize : Uses ``load_config`` internally.
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Union

import numpy as np

from .config import CeceConfig
from .exceptions import CeceConfigError


def load_config(config: Union[str, dict, CeceConfig]) -> CeceConfig:
    """
    Load an CECE configuration from various sources.

    Accepts a file path, YAML string, dictionary, or an existing
    ``CeceConfig`` object and returns a validated ``CeceConfig``.

    Parameters
    ----------
    config : str, dict, or CeceConfig
        Configuration source. If a string, it is first checked as a file
        path; if the file exists it is read as YAML. Otherwise the string
        is parsed directly as YAML. Dictionaries are converted via
        ``CeceConfig.from_dict``. An existing ``CeceConfig`` is returned
        as-is.

    Returns
    -------
    CeceConfig
        Loaded and parsed configuration object.

    Raises
    ------
    CeceConfigError
        If the configuration source is invalid, the file cannot be read,
        or the YAML/dict content is malformed.

    Examples
    --------
    >>> config = load_config("config.yaml")
    >>> config = load_config({"species": {"CO": [...]}})
    >>> config = load_config(existing_config)
    """
    if isinstance(config, CeceConfig):
        return config

    if isinstance(config, dict):
        try:
            return CeceConfig.from_dict(config)
        except ValueError as e:
            raise CeceConfigError(f"Invalid configuration dict: {str(e)}")

    if isinstance(config, str):
        # Check if it's a file path
        path = Path(config)
        if path.exists() and path.is_file():
            try:
                with open(path, "r") as f:
                    yaml_str = f.read()
                return _load_config_from_yaml(yaml_str)
            except Exception as e:
                raise CeceConfigError(f"Failed to load config from file '{config}': {str(e)}")
        else:
            # Treat as YAML string
            try:
                return _load_config_from_yaml(config)
            except Exception as e:
                raise CeceConfigError(f"Failed to parse config YAML: {str(e)}")

    raise CeceConfigError(f"Invalid config type: {type(config)}")


def _load_config_from_yaml(yaml_str: str) -> CeceConfig:
    """
    Load configuration from a YAML string.

    Parameters
    ----------
    yaml_str : str
        YAML-formatted configuration string.

    Returns
    -------
    CeceConfig
        Parsed configuration object.

    Raises
    ------
    CeceConfigError
        If the YAML string is malformed or cannot be parsed.
    """
    try:
        return CeceConfig.from_yaml(yaml_str)
    except (ImportError, ValueError) as e:
        raise CeceConfigError(f"Failed to parse config YAML: {str(e)}")


def validate_array_dimensions(array: np.ndarray, expected_shape: tuple) -> None:
    """
    Validate that an array has the expected shape.

    Parameters
    ----------
    array : numpy.ndarray
        Array to validate.
    expected_shape : tuple of int
        Expected shape (e.g., ``(nx, ny, nz)``).

    Raises
    ------
    ValueError
        If the array shape does not match ``expected_shape``.
    """
    if array.shape != expected_shape:
        raise ValueError(f"Array shape {array.shape} doesn't match expected {expected_shape}")


def validate_array_dtype(array: np.ndarray) -> None:
    """
    Validate that an array has float64 dtype.

    Parameters
    ----------
    array : numpy.ndarray
        Array to validate.

    Raises
    ------
    TypeError
        If the array dtype is not ``float64``.
    """
    if array.dtype != np.float64:
        raise TypeError(f"Array dtype must be float64, got {array.dtype}")


def validate_array_contiguity(array: np.ndarray) -> None:
    """
    Validate that an array is contiguous in memory.

    Parameters
    ----------
    array : numpy.ndarray
        Array to validate.

    Raises
    ------
    ValueError
        If the array is neither C-contiguous nor Fortran-contiguous.
    """
    if not (array.flags["C_CONTIGUOUS"] or array.flags["F_CONTIGUOUS"]):
        raise ValueError("Array must be C-contiguous or Fortran-contiguous")


def dict_to_yaml(config_dict: dict) -> str:
    """
    Convert a dictionary to a YAML string.

    Parameters
    ----------
    config_dict : dict
        Configuration dictionary to serialize.

    Returns
    -------
    str
        YAML-formatted string.

    Raises
    ------
    ImportError
        If PyYAML is not installed.
    """
    try:
        import yaml
    except ImportError:
        raise ImportError("PyYAML is required. Install with: pip install pyyaml")

    return yaml.dump(config_dict, default_flow_style=False, sort_keys=False)


def yaml_to_dict(yaml_str: str) -> dict:
    """
    Parse a YAML string into a dictionary.

    Parameters
    ----------
    yaml_str : str
        YAML-formatted string.

    Returns
    -------
    dict
        Parsed configuration dictionary.

    Raises
    ------
    ImportError
        If PyYAML is not installed.
    ValueError
        If the YAML string is malformed or does not represent a dictionary.
    """
    try:
        import yaml
    except ImportError:
        raise ImportError("PyYAML is required. Install with: pip install pyyaml")

    try:
        config_dict = yaml.safe_load(yaml_str)
        if not isinstance(config_dict, dict):
            raise ValueError("YAML must represent a dictionary")
        return config_dict
    except yaml.YAMLError as e:
        raise ValueError(f"Invalid YAML: {str(e)}")


def setup_logging(level: str = "INFO") -> None:
    """
    Configure Python logging for CECE.

    Sets up a basic logging configuration with timestamp, logger name,
    level, and message format.

    Parameters
    ----------
    level : str, optional
        Log level string. One of ``"DEBUG"``, ``"INFO"``, ``"WARNING"``,
        ``"ERROR"``. Default is ``"INFO"``.

    Raises
    ------
    ValueError
        If ``level`` is not a recognized log level.

    Examples
    --------
    >>> setup_logging("DEBUG")
    """
    numeric_level = getattr(logging, level.upper(), None)
    if not isinstance(numeric_level, int):
        raise ValueError(f"Invalid log level: {level}")

    logging.basicConfig(
        level=numeric_level,
        format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    )


def get_logger(name: str) -> logging.Logger:
    """
    Get a named logger instance.

    Parameters
    ----------
    name : str
        Logger name, typically ``__name__`` of the calling module.

    Returns
    -------
    logging.Logger
        Configured logger instance.
    """
    return logging.getLogger(name)
