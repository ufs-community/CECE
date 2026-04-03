"""
Configuration classes for ACES.

Provides AcesConfig, EmissionLayer, and related configuration management.
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Any


@dataclass
class VerticalDistributionConfig:
    """Vertical distribution parameters for emission layers."""

    method: str = "single"  # "single", "range", "pressure", "height", "pbl"
    layer_start: int = 0
    layer_end: int = 0
    p_start: float = 0.0
    p_end: float = 0.0
    h_start: float = 0.0
    h_end: float = 0.0

    def validate(self):
        """Validate vertical distribution parameters."""
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
    """Single emission layer configuration."""

    field_name: str
    operation: str = "add"
    masks: List[str] = field(default_factory=list)
    scale: float = 1.0
    hierarchy: int = 0
    vdist: VerticalDistributionConfig = field(default_factory=VerticalDistributionConfig)
    diurnal_cycle: Optional[str] = None
    weekly_cycle: Optional[str] = None
    seasonal_cycle: Optional[str] = None

    def validate(self):
        """Validate emission layer parameters."""
        if not self.field_name:
            raise ValueError("field_name cannot be empty")
        if self.operation not in ["add", "replace", "scale"]:
            raise ValueError(f"Invalid operation: {self.operation}")
        if self.scale < 0:
            raise ValueError("scale must be non-negative")
        self.vdist.validate()


@dataclass
class PhysicsSchemeConfig:
    """Physics scheme configuration."""

    name: str
    language: str = "cpp"
    options: Dict[str, Any] = field(default_factory=dict)

    def validate(self):
        """Validate physics scheme parameters."""
        if not self.name:
            raise ValueError("scheme name cannot be empty")
        if self.language not in ["cpp", "fortran", "python"]:
            raise ValueError(f"Invalid language: {self.language}")


@dataclass
class DataStreamConfig:
    """Data stream configuration."""

    name: str
    file_paths: List[str] = field(default_factory=list)
    variables: Dict[str, str] = field(default_factory=dict)
    taxmode: str = "cycle"
    tintalgo: str = "linear"
    mapalgo: str = "default"

    def validate(self):
        """Validate data stream parameters."""
        if not self.name:
            raise ValueError("stream name cannot be empty")
        if not self.file_paths:
            raise ValueError("file_paths cannot be empty")
        if self.taxmode not in ["cycle", "extend", "interp"]:
            raise ValueError(f"Invalid taxmode: {self.taxmode}")
        if self.tintalgo not in ["linear", "constant"]:
            raise ValueError(f"Invalid tintalgo: {self.tintalgo}")


class ValidationResult:
    """Result of configuration validation."""

    def __init__(self, is_valid=True, errors=None):
        """
        Initialize validation result.

        Args:
            is_valid: Whether validation passed
            errors: List of error messages
        """
        self.is_valid = is_valid
        self.errors = errors or []

    def __bool__(self):
        """Return True if validation passed."""
        return self.is_valid

    def __str__(self):
        """Return string representation."""
        if self.is_valid:
            return "Validation passed"
        return "Validation failed:\n" + "\n".join(f"  - {e}" for e in self.errors)


class AcesConfig:
    """Configuration for ACES computations."""

    def __init__(self, config_dict=None):
        """
        Initialize configuration.

        Args:
            config_dict: Optional dict with configuration data
        """
        self._species = {}
        self._physics_schemes = []
        self._aces_data = {"streams": []}
        self._vertical_config = VerticalDistributionConfig()
        self._temporal_cycles = {}

        if config_dict:
            self._from_dict(config_dict)

    def add_species(self, name, layers):
        """
        Add a species with emission layers.

        Args:
            name: Species name
            layers: List of EmissionLayer objects

        Raises:
            ValueError: If name is empty or layers is invalid
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

    def add_physics_scheme(self, name, language="cpp", options=None):
        """
        Register a physics scheme.

        Args:
            name: Scheme name
            language: Programming language ("cpp", "fortran", "python")
            options: Optional dict of scheme options

        Raises:
            ValueError: If parameters are invalid
        """
        scheme = PhysicsSchemeConfig(name, language, options or {})
        scheme.validate()
        self._physics_schemes.append(scheme)

    def add_data_stream(self, name, file_paths, variables, taxmode="cycle", tintalgo="linear", mapalgo="default"):
        """
        Configure a data stream.

        Args:
            name: Stream name
            file_paths: List of file paths
            variables: Dict mapping file names to model names
            taxmode: Time axis mode ("cycle", "extend", "interp")
            tintalgo: Time interpolation algorithm ("linear", "constant")
            mapalgo: Mapping algorithm ("default", etc.)

        Raises:
            ValueError: If parameters are invalid
        """
        stream = DataStreamConfig(name, file_paths, variables, taxmode, tintalgo, mapalgo)
        stream.validate()
        self._aces_data["streams"].append(stream)

    def add_temporal_cycle(self, name, factors):
        """
        Add temporal cycle (diurnal, weekly, seasonal).

        Args:
            name: Cycle name
            factors: List of scaling factors

        Raises:
            ValueError: If factors are invalid
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

    def validate(self):
        """
        Validate configuration.

        Returns:
            ValidationResult with is_valid flag and error messages
        """
        errors = []

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
        for stream in self._aces_data.get("streams", []):
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

    def to_yaml(self):
        """
        Serialize configuration to YAML string.

        Returns:
            YAML string representation

        Raises:
            ImportError: If PyYAML is not installed
        """
        try:
            import yaml
        except ImportError:
            raise ImportError("PyYAML is required for YAML serialization. Install with: pip install pyyaml")

        config_dict = self.to_dict()
        return yaml.dump(config_dict, default_flow_style=False, sort_keys=False)

    def to_dict(self):
        """
        Serialize configuration to Python dict.

        Returns:
            Dict representation of configuration
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
            "aces_data": {
                "streams": [
                    {
                        "name": s.name,
                        "file_paths": s.file_paths,
                        "variables": s.variables,
                        "taxmode": s.taxmode,
                        "tintalgo": s.tintalgo,
                        "mapalgo": s.mapalgo,
                    }
                    for s in self._aces_data.get("streams", [])
                ]
            },
            "temporal_cycles": self._temporal_cycles,
        }

    @classmethod
    def from_dict(cls, config_dict):
        """
        Create configuration from dict.

        Args:
            config_dict: Dict with configuration data

        Returns:
            AcesConfig object

        Raises:
            ValueError: If configuration is invalid
        """
        config = cls()
        config._from_dict(config_dict)
        return config

    @classmethod
    def from_yaml(cls, yaml_str):
        """
        Create configuration from YAML string.

        Args:
            yaml_str: YAML string with configuration data

        Returns:
            AcesConfig object

        Raises:
            ImportError: If PyYAML is not installed
            ValueError: If YAML is invalid
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

    def _from_dict(self, config_dict):
        """Load configuration from dict."""
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
        for stream_data in config_dict.get("aces_data", {}).get("streams", []):
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
    def species(self):
        """Get species configuration."""
        return self._species

    @property
    def physics_schemes(self):
        """Get physics schemes."""
        return self._physics_schemes

    @property
    def aces_data(self):
        """Get ACES data configuration."""
        return self._aces_data

    @property
    def vertical_config(self):
        """Get vertical configuration."""
        return self._vertical_config
