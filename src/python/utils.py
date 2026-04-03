"""
Utility functions for ACES Python interface.

Provides configuration loading, validation, serialization, and logging setup.
"""

import logging
from pathlib import Path

from .config import AcesConfig
from .exceptions import AcesConfigError


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
    if isinstance(config, AcesConfig):
        return config

    if isinstance(config, dict):
        try:
            return AcesConfig.from_dict(config)
        except ValueError as e:
            raise AcesConfigError(f"Invalid configuration dict: {str(e)}")

    if isinstance(config, str):
        # Check if it's a file path
        path = Path(config)
        if path.exists() and path.is_file():
            try:
                with open(path, "r") as f:
                    yaml_str = f.read()
                return _load_config_from_yaml(yaml_str)
            except Exception as e:
                raise AcesConfigError(f"Failed to load config from file '{config}': {str(e)}")
        else:
            # Treat as YAML string
            try:
                return _load_config_from_yaml(config)
            except Exception as e:
                raise AcesConfigError(f"Failed to parse config YAML: {str(e)}")

    raise AcesConfigError(f"Invalid config type: {type(config)}")


def _load_config_from_yaml(yaml_str):
    """
    Load configuration from YAML string.

    Args:
        yaml_str: YAML string

    Returns:
        AcesConfig object

    Raises:
        AcesConfigError: If YAML is invalid
    """
    try:
        return AcesConfig.from_yaml(yaml_str)
    except (ImportError, ValueError) as e:
        raise AcesConfigError(f"Failed to parse config YAML: {str(e)}")


def validate_array_dimensions(array, expected_shape):
    """
    Validate array dimensions.

    Args:
        array: Numpy array
        expected_shape: Expected shape tuple

    Raises:
        ValueError: If dimensions don't match
    """
    if array.shape != expected_shape:
        raise ValueError(f"Array shape {array.shape} doesn't match expected {expected_shape}")


def validate_array_dtype(array):
    """
    Validate array data type.

    Args:
        array: Numpy array

    Raises:
        TypeError: If dtype is not float64
    """
    import numpy as np

    if array.dtype != np.float64:
        raise TypeError(f"Array dtype must be float64, got {array.dtype}")


def validate_array_contiguity(array):
    """
    Validate array contiguity.

    Args:
        array: Numpy array

    Raises:
        ValueError: If array is not contiguous
    """
    if not (array.flags["C_CONTIGUOUS"] or array.flags["F_CONTIGUOUS"]):
        raise ValueError("Array must be C-contiguous or Fortran-contiguous")


def dict_to_yaml(config_dict):
    """
    Convert dict to YAML string.

    Args:
        config_dict: Configuration dict

    Returns:
        YAML string

    Raises:
        ImportError: If PyYAML is not installed
    """
    try:
        import yaml
    except ImportError:
        raise ImportError("PyYAML is required. Install with: pip install pyyaml")

    return yaml.dump(config_dict, default_flow_style=False, sort_keys=False)


def yaml_to_dict(yaml_str):
    """
    Convert YAML string to dict.

    Args:
        yaml_str: YAML string

    Returns:
        Configuration dict

    Raises:
        ImportError: If PyYAML is not installed
        ValueError: If YAML is invalid
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


def setup_logging(level="INFO"):
    """
    Configure Python logging.

    Args:
        level: Log level ("DEBUG", "INFO", "WARNING", "ERROR")
    """
    numeric_level = getattr(logging, level.upper(), None)
    if not isinstance(numeric_level, int):
        raise ValueError(f"Invalid log level: {level}")

    logging.basicConfig(
        level=numeric_level,
        format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    )


def get_logger(name):
    """
    Get logger instance.

    Args:
        name: Logger name

    Returns:
        Logger instance
    """
    return logging.getLogger(name)
