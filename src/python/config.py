"""
Configuration classes for the CECE Python interface.

Provides dataclass-based configuration objects for emission layers, vertical
distribution, physics schemes, data streams, and the top-level ``CeceConfig``
container. Supports loading from YAML files or dictionaries, validation, and
serialization.

See Also
--------
cece.utils.load_config : Load configuration from various sources.
cece.initialize : Initialize CECE with a configuration.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Any


@dataclass
class VerticalDistributionConfig:
    """
    Vertical distribution parameters for emission layers.

    Controls how emissions are distributed across vertical model levels using
    one of several methods: single layer, layer range, pressure range, height
    range, or planetary boundary layer scaling.

    Parameters
    ----------
    method : str, optional
        Distribution method. One of ``"single"``, ``"range"``,
        ``"pressure"``, ``"height"``, ``"pbl"``. Default is ``"single"``.
    layer_start : int, optional
        Starting model layer index (for ``"range"`` method). Default is 0.
    layer_end : int, optional
        Ending model layer index (for ``"range"`` method). Default is 0.
    p_start : float, optional
        Starting pressure in Pa (for ``"pressure"`` method). Default is 0.0.
    p_end : float, optional
        Ending pressure in Pa (for ``"pressure"`` method). Default is 0.0.
    h_start : float, optional
        Starting height in meters (for ``"height"`` method). Default is 0.0.
    h_end : float, optional
        Ending height in meters (for ``"height"`` method). Default is 0.0.

    Examples
    --------
    >>> vdist = VerticalDistributionConfig(method="range", layer_start=0, layer_end=5)
    >>> vdist.validate()
    """

    method: str = "single"
    layer_start: int = 0
    layer_end: int = 0
    p_start: float = 0.0
    p_end: float = 0.0
    h_start: float = 0.0
    h_end: float = 0.0

    def validate(self) -> None:
        """
        Validate vertical distribution parameters.

        Raises
        ------
        ValueError
            If ``method`` is not recognized, or if range parameters are
            negative or inverted (start > end).
        """
        valid_methods = ["single", "range", "pressure", "height", "pbl"]
        if self.method not in valid_methods:
            raise ValueError(f"Invalid vdist_method: {self.method}. Must be one of {valid_methods}")

        if self.method == "range":
            if self.layer_start < 0 or self.layer_end < 0:
                raise ValueError("layer_start and layer_end must be non-negative")
            if self.layer_start > self.layer_end:
                raise ValueError("layer_start must be <= layer_end")

        if self.method == "pressure":
            if self.p_start < 0 or self.p_end < 0:
                raise ValueError("p_start and p_end must be non-negative")
            if self.p_start > self.p_end:
                raise ValueError("p_start must be <= p_end")

        if self.method == "height":
            if self.h_start < 0 or self.h_end < 0:
                raise ValueError("h_start and h_end must be non-negative")
            if self.h_start > self.h_end:
                raise ValueError("h_start must be <= h_end")


@dataclass
class EmissionLayer:
    """
    Configuration for a single emission layer.

    Represents one data layer contributing to a species' total emissions,
    with operation type, scaling, masking, and temporal cycle settings.

    Parameters
    ----------
    field_name : str
        Name of the data field in the import state.
    operation : str, optional
        How this layer combines with others. One of ``"add"``, ``"replace"``,
        ``"scale"``. Default is ``"add"``.
    masks : list of str, optional
        Names of mask fields to apply. Default is an empty list.
    scale : float, optional
        Multiplicative scale factor. Default is 1.0.
    hierarchy : int, optional
        Priority level for layer stacking. Default is 0.
    vdist : VerticalDistributionConfig, optional
        Vertical distribution configuration. Default is a new
        ``VerticalDistributionConfig`` instance.
    diurnal_cycle : str or None, optional
        Name of the diurnal temporal cycle to apply. Default is ``None``.
    weekly_cycle : str or None, optional
        Name of the weekly temporal cycle to apply. Default is ``None``.
    seasonal_cycle : str or None, optional
        Name of the seasonal temporal cycle to apply. Default is ``None``.

    Examples
    --------
    >>> layer = EmissionLayer(field_name="CO_ANTHRO", operation="add", scale=1.5)
    >>> layer.validate()
    """

    field_name: str
    operation: str = "add"
    masks: List[str] = field(default_factory=list)
    scale: float = 1.0
    hierarchy: int = 0
    vdist: VerticalDistributionConfig = field(default_factory=VerticalDistributionConfig)
    diurnal_cycle: Optional[str] = None
    weekly_cycle: Optional[str] = None
    seasonal_cycle: Optional[str] = None

    def validate(self) -> None:
        """
        Validate emission layer parameters.

        Raises
        ------
        ValueError
            If ``field_name`` is empty, ``operation`` is not recognized,
            ``scale`` is negative, or vertical distribution is invalid.
        """
        if not self.field_name:
            raise ValueError("field_name cannot be empty")
        if self.operation not in ["add", "replace", "scale"]:
            raise ValueError(f"Invalid operation: {self.operation}")
        if self.scale < 0:
            raise ValueError("scale must be non-negative")
        self.vdist.validate()


@dataclass
class PhysicsSchemeConfig:
    """
    Configuration for a physics scheme plugin.

    Parameters
    ----------
    name : str
        Name of the physics scheme (e.g., ``"megan"``, ``"sea_salt"``).
    language : str, optional
        Implementation language. One of ``"cpp"``, ``"fortran"``,
        ``"python"``. Default is ``"cpp"``.
    options : dict, optional
        Scheme-specific options as key-value pairs. Default is an empty dict.

    Examples
    --------
    >>> scheme = PhysicsSchemeConfig(name="megan", language="fortran")
    >>> scheme.validate()
    """

    name: str
    language: str = "cpp"
    options: Dict[str, Any] = field(default_factory=dict)

    def validate(self) -> None:
        """
        Validate physics scheme parameters.

        Raises
        ------
        ValueError
            If ``name`` is empty or ``language`` is not recognized.
        """
        if not self.name:
            raise ValueError("scheme name cannot be empty")
        if self.language not in ["cpp", "fortran", "python"]:
            raise ValueError(f"Invalid language: {self.language}")


@dataclass
class DataStreamConfig:
    """
    Configuration for a TIDE data stream.

    Parameters
    ----------
    name : str
        Stream identifier.
    file_paths : list of str, optional
        Paths to data files. Default is an empty list.
    variables : dict, optional
        Mapping of file variable names to model variable names.
        Default is an empty dict.
    taxmode : str, optional
        Time axis mode. One of ``"cycle"``, ``"extend"``, ``"interp"``.
        Default is ``"cycle"``.
    tintalgo : str, optional
        Time interpolation algorithm. One of ``"linear"``, ``"constant"``.
        Default is ``"linear"``.
    mapalgo : str, optional
        Spatial mapping algorithm. Default is ``"default"``.

    Examples
    --------
    >>> stream = DataStreamConfig(name="anthro_co", file_paths=["co.nc"])
    """

    name: str
    file_paths: List[str] = field(default_factory=list)
    variables: Dict[str, str] = field(default_factory=dict)
    taxmode: str = "cycle"
    tintalgo: str = "linear"
    mapalgo: str = "default"

    def validate(self) -> None:
        """
        Validate data stream parameters.

        Raises
        ------
        ValueError
            If ``name`` is empty, ``file_paths`` is empty, ``taxmode`` is
            not recognized, or ``tintalgo`` is not recognized.
        """
        if not self.name:
            raise ValueError("stream name cannot be empty")
        if not self.file_paths:
            raise ValueError("file_paths cannot be empty")
        if self.taxmode not in ["cycle", "extend", "interp"]:
            raise ValueError(f"Invalid taxmode: {self.taxmode}")
        if self.tintalgo not in ["linear", "constant"]:
            raise ValueError(f"Invalid tintalgo: {self.tintalgo}")


class ValidationResult:
    """
    Result of configuration validation.

    Attributes
    ----------
    is_valid : bool
        ``True`` if validation passed with no errors.
    errors : list of str
        List of validation error messages. Empty if valid.

    Examples
    --------
    >>> result = config.validate()
    >>> if not result.is_valid:
    ...     for err in result.errors:
    ...         print(err)
    """

    def __init__(self, is_valid: bool = True, errors: Optional[List[str]] = None) -> None:
        """
        Initialize validation result.

        Parameters
        ----------
        is_valid : bool, optional
            Whether validation passed. Default is ``True``.
        errors : list of str or None, optional
            List of error messages. Default is ``None`` (empty list).
        """
        self.is_valid = is_valid
        self.errors = errors or []

    def __bool__(self) -> bool:
        """Return ``True`` if validation passed."""
        return self.is_valid

    def __str__(self) -> str:
        """Return a human-readable summary of the validation result."""
        if self.is_valid:
            return "Validation passed"
        return "Validation failed:\n" + "\n".join(f"  - {e}" for e in self.errors)


class CeceConfig:
    """
    Top-level configuration for CECE computations.

    Manages species definitions, physics scheme registrations, data stream
    configurations, and temporal cycle definitions. Supports construction
    from dictionaries or YAML strings, validation, and serialization.

    Parameters
    ----------
    config_dict : dict or None, optional
        Initial configuration data. If provided, the configuration is
        populated from this dictionary. Default is ``None``.

    Attributes
    ----------
    species : dict
        Mapping of species names to lists of ``EmissionLayer`` objects.
    physics_schemes : list of PhysicsSchemeConfig
        Registered physics scheme configurations.
    cece_data : dict
        Data stream configuration with a ``"streams"`` key.
    vertical_config : VerticalDistributionConfig
        Default vertical distribution configuration.

    Examples
    --------
    >>> config = CeceConfig()
    >>> config.add_species("CO", [EmissionLayer(field_name="CO_ANTHRO")])
    >>> result = config.validate()
    >>> print(result)
    Validation passed
    """

    def __init__(self, config_dict: Optional[dict] = None) -> None:
        """
        Initialize configuration.

        Parameters
        ----------
        config_dict : dict or None, optional
            Dictionary with configuration data to populate from.
            Default is ``None``.
        """
        self._species: Dict[str, List[EmissionLayer]] = {}
        self._physics_schemes: List[PhysicsSchemeConfig] = []
        self._cece_data: Dict[str, Any] = {"streams": []}
        self._vertical_config: VerticalDistributionConfig = VerticalDistributionConfig()
        self._temporal_cycles: Dict[str, List] = {}

        if config_dict:
            self._from_dict(config_dict)

    def add_species(self, name: str, layers: List[EmissionLayer]) -> None:
        """
        Add a species with its emission layers.

        Parameters
        ----------
        name : str
            Species name (e.g., ``"CO"``, ``"NO2"``).
        layers : list of EmissionLayer
            Emission layers contributing to this species.

        Raises
        ------
        ValueError
            If ``name`` is empty, ``layers`` is not a list, or any layer
            fails validation.
        """
        if not name:
            raise ValueError("Species name cannot be empty")
        if not isinstance(layers, list):
            raise ValueError("layers must be a list")
        for layer in layers:
            if not isinstance(layer, EmissionLayer):
                raise ValueError("All layers must be EmissionLayer objects")
            layer.validate()
        self._species[name] = layers

    def add_physics_scheme(
        self,
        name: str,
        language: str = "cpp",
        options: Optional[Dict[str, Any]] = None,
    ) -> None:
        """
        Register a physics scheme.

        Parameters
        ----------
        name : str
            Scheme name (e.g., ``"megan"``, ``"dust"``).
        language : str, optional
            Implementation language. One of ``"cpp"``, ``"fortran"``,
            ``"python"``. Default is ``"cpp"``.
        options : dict or None, optional
            Scheme-specific options. Default is ``None``.

        Raises
        ------
        ValueError
            If ``name`` is empty or ``language`` is not recognized.
        """
        scheme = PhysicsSchemeConfig(name, language, options or {})
        scheme.validate()
        self._physics_schemes.append(scheme)

    def add_data_stream(
        self,
        name: str,
        file_paths: List[str],
        variables: Dict[str, str],
        taxmode: str = "cycle",
        tintalgo: str = "linear",
        mapalgo: str = "default",
    ) -> None:
        """
        Configure a TIDE data stream.

        Parameters
        ----------
        name : str
            Stream identifier.
        file_paths : list of str
            Paths to data files.
        variables : dict
            Mapping of file variable names to model variable names.
        taxmode : str, optional
            Time axis mode. Default is ``"cycle"``.
        tintalgo : str, optional
            Time interpolation algorithm. Default is ``"linear"``.
        mapalgo : str, optional
            Spatial mapping algorithm. Default is ``"default"``.

        Raises
        ------
        ValueError
            If parameters fail validation.
        """
        stream = DataStreamConfig(name, file_paths, variables, taxmode, tintalgo, mapalgo)
        stream.validate()
        self._cece_data["streams"].append(stream)

    def add_temporal_cycle(self, name: str, factors: List) -> None:
        """
        Add a temporal cycle (diurnal, weekly, or seasonal).

        Parameters
        ----------
        name : str
            Cycle name referenced by emission layers.
        factors : list of int or float
            Scaling factors for each time step in the cycle.

        Raises
        ------
        ValueError
            If ``name`` is empty, ``factors`` is not a list, ``factors`` is
            empty, or any factor is negative.
        """
        if not name:
            raise ValueError("Cycle name cannot be empty")
        if not isinstance(factors, list):
            raise ValueError("factors must be a list")
        if not factors:
            raise ValueError("factors cannot be empty")
        for f in factors:
            if not isinstance(f, (int, float)) or f < 0:
                raise ValueError("All factors must be non-negative numbers")
        self._temporal_cycles[name] = factors

    def validate(self) -> ValidationResult:
        """
        Validate the entire configuration.

        Checks all species layers, physics schemes, data streams, and
        temporal cycles for consistency and correctness.

        Returns
        -------
        ValidationResult
            Result object with ``is_valid`` flag and list of error messages.

        Examples
        --------
        >>> result = config.validate()
        >>> if not result:
        ...     print(result)
        """
        errors: List[str] = []

        # Validate species
        for name, layers in self._species.items():
            if not name:
                errors.append("Species name cannot be empty")
            for layer in layers:
                try:
                    layer.validate()
                except ValueError as e:
                    errors.append(f"Species '{name}': {str(e)}")

        # Validate physics schemes
        for scheme in self._physics_schemes:
            try:
                scheme.validate()
            except ValueError as e:
                errors.append(f"Physics scheme: {str(e)}")

        # Validate data streams
        for stream in self._cece_data.get("streams", []):
            try:
                stream.validate()
            except ValueError as e:
                errors.append(f"Data stream: {str(e)}")

        # Validate temporal cycles
        for name, factors in self._temporal_cycles.items():
            if not name:
                errors.append("Temporal cycle name cannot be empty")
            if not factors:
                errors.append(f"Temporal cycle '{name}' has no factors")

        return ValidationResult(is_valid=len(errors) == 0, errors=errors)

    def to_yaml(self) -> str:
        """
        Serialize configuration to a YAML string.

        Returns
        -------
        str
            YAML representation of the configuration.

        Raises
        ------
        ImportError
            If PyYAML is not installed.
        """
        try:
            import yaml
        except ImportError:
            raise ImportError("PyYAML is required for YAML serialization. Install with: pip install pyyaml")

        config_dict = self.to_dict()
        return yaml.dump(config_dict, default_flow_style=False, sort_keys=False)

    def to_dict(self) -> dict:
        """
        Serialize configuration to a Python dictionary.

        Returns
        -------
        dict
            Dictionary representation of the full configuration, suitable
            for YAML serialization or ``from_dict`` round-tripping.
        """
        return {
            "species": {
                name: [
                    {
                        "field": layer.field_name,
                        "operation": layer.operation,
                        "scale": layer.scale,
                        "vdist_method": layer.vdist.method,
                        "vdist_layer_start": layer.vdist.layer_start,
                        "vdist_layer_end": layer.vdist.layer_end,
                        "vdist_p_start": layer.vdist.p_start,
                        "vdist_p_end": layer.vdist.p_end,
                        "vdist_h_start": layer.vdist.h_start,
                        "vdist_h_end": layer.vdist.h_end,
                    }
                    for layer in layers
                ]
                for name, layers in self._species.items()
            },
            "physics_schemes": [
                {"name": s.name, "language": s.language, "options": s.options} for s in self._physics_schemes
            ],
            "cece_data": {
                "streams": [
                    {
                        "name": s.name,
                        "file_paths": s.file_paths,
                        "variables": s.variables,
                        "taxmode": s.taxmode,
                        "tintalgo": s.tintalgo,
                        "mapalgo": s.mapalgo,
                    }
                    for s in self._cece_data.get("streams", [])
                ]
            },
            "temporal_cycles": self._temporal_cycles,
        }

    @classmethod
    def from_dict(cls, config_dict: dict) -> CeceConfig:
        """
        Create a configuration from a dictionary.

        Parameters
        ----------
        config_dict : dict
            Dictionary with configuration data.

        Returns
        -------
        CeceConfig
            New configuration object populated from the dictionary.

        Raises
        ------
        ValueError
            If the dictionary contains invalid configuration data.
        """
        config = cls()
        config._from_dict(config_dict)
        return config

    @classmethod
    def from_yaml(cls, yaml_str: str) -> CeceConfig:
        """
        Create a configuration from a YAML string.

        Parameters
        ----------
        yaml_str : str
            YAML-formatted configuration string.

        Returns
        -------
        CeceConfig
            New configuration object populated from the YAML data.

        Raises
        ------
        ImportError
            If PyYAML is not installed.
        ValueError
            If the YAML string is malformed or does not represent a dict.
        """
        try:
            import yaml
        except ImportError:
            raise ImportError("PyYAML is required for YAML parsing. Install with: pip install pyyaml")

        try:
            config_dict = yaml.safe_load(yaml_str)
            if not isinstance(config_dict, dict):
                raise ValueError("YAML must represent a dictionary")
            return cls.from_dict(config_dict)
        except yaml.YAMLError as e:
            raise ValueError(f"Invalid YAML: {str(e)}")

    def _from_dict(self, config_dict: dict) -> None:
        """
        Populate configuration from a dictionary.

        Parameters
        ----------
        config_dict : dict
            Dictionary with species, physics_schemes, cece_data, and
            temporal_cycles keys.
        """
        # Species
        for name, layers_data in config_dict.get("species", {}).items():
            layers = []
            for layer_data in layers_data:
                vdist = VerticalDistributionConfig(
                    method=layer_data.get("vdist_method", "single"),
                    layer_start=layer_data.get("vdist_layer_start", 0),
                    layer_end=layer_data.get("vdist_layer_end", 0),
                    p_start=layer_data.get("vdist_p_start", 0.0),
                    p_end=layer_data.get("vdist_p_end", 0.0),
                    h_start=layer_data.get("vdist_h_start", 0.0),
                    h_end=layer_data.get("vdist_h_end", 0.0),
                )
                layer = EmissionLayer(
                    field_name=layer_data.get("field", ""),
                    operation=layer_data.get("operation", "add"),
                    scale=layer_data.get("scale", 1.0),
                    vdist=vdist,
                )
                layers.append(layer)
            if layers:
                self.add_species(name, layers)

        # Physics schemes
        for scheme_data in config_dict.get("physics_schemes", []):
            self.add_physics_scheme(
                name=scheme_data.get("name", ""),
                language=scheme_data.get("language", "cpp"),
                options=scheme_data.get("options", {}),
            )

        # Data streams
        for stream_data in config_dict.get("cece_data", {}).get("streams", []):
            self.add_data_stream(
                name=stream_data.get("name", ""),
                file_paths=stream_data.get("file_paths", []),
                variables=stream_data.get("variables", {}),
                taxmode=stream_data.get("taxmode", "cycle"),
                tintalgo=stream_data.get("tintalgo", "linear"),
                mapalgo=stream_data.get("mapalgo", "default"),
            )

        # Temporal cycles
        for name, factors in config_dict.get("temporal_cycles", {}).items():
            self.add_temporal_cycle(name, factors)

    @property
    def species(self) -> Dict[str, List[EmissionLayer]]:
        """dict : Mapping of species names to lists of ``EmissionLayer``."""
        return self._species

    @property
    def physics_schemes(self) -> List[PhysicsSchemeConfig]:
        """list of PhysicsSchemeConfig : Registered physics schemes."""
        return self._physics_schemes

    @property
    def cece_data(self) -> Dict[str, Any]:
        """dict : Data stream configuration."""
        return self._cece_data

    @property
    def vertical_config(self) -> VerticalDistributionConfig:
        """VerticalDistributionConfig : Default vertical distribution settings."""
        return self._vertical_config
