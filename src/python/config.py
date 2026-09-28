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

import math
from collections.abc import Mapping
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Dict, List, Optional, Union


def _require_mapping(value: Any, where: str) -> Mapping:
    if not isinstance(value, Mapping):
        raise ValueError(f"{where} must be a mapping")
    return value


def _reject_unknown_keys(data: Mapping, allowed: set[str], where: str) -> None:
    unknown = sorted(set(data) - allowed, key=str)
    if unknown:
        key = unknown[0]
        message = f"{where}: unknown key {key!r}"
        if where.startswith("physics_schemes[") and key in {
            "input_mapping",
            "output_mapping",
        }:
            message += f"; put {key!r} under 'options'"
        raise ValueError(message)


def _validate_string(value: Any, where: str, allow_empty: bool = False) -> str:
    if not isinstance(value, str) or (not allow_empty and not value.strip()):
        raise ValueError(f"{where} must be a non-empty string")
    return value


def _validate_integer(value: Any, where: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{where} must be an integer")
    return value


def _validate_finite_number(value: Any, where: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{where} must be a number")
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"{where} must be finite")
    return number


def _validate_string_list(value: Any, where: str) -> List[str]:
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ValueError(f"{where} must be a list of strings")
    return value


def _validate_string_mapping(value: Any, where: str) -> Dict[str, str]:
    data = _require_mapping(value, where)
    result: Dict[str, str] = {}
    for key, item in data.items():
        if not isinstance(key, str) or not isinstance(item, str):
            raise ValueError(f"{where} must map strings to strings")
        result[key] = item
    return result


def _validate_temporal_factors(factors: Any) -> None:
    if not isinstance(factors, list) or not factors:
        raise ValueError("factors must be a non-empty list")
    for factor in factors:
        if isinstance(factor, bool) or not isinstance(factor, (int, float)):
            raise ValueError("factors must contain numbers")
        if not math.isfinite(factor) or factor < 0:
            raise ValueError("factors must be finite and non-negative")


def _parse_temporal_factors(factors: Any) -> List[float]:
    _validate_temporal_factors(factors)
    return factors


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
        if not isinstance(self.method, str):
            raise ValueError("vdist method must be a string")
        if self.method not in valid_methods:
            raise ValueError(
                f"Invalid vdist_method: {self.method}. Must be one of {valid_methods}"
            )

        _validate_integer(self.layer_start, "vdist.layer_start")
        _validate_integer(self.layer_end, "vdist.layer_end")
        _validate_finite_number(self.p_start, "vdist.p_start")
        _validate_finite_number(self.p_end, "vdist.p_end")
        _validate_finite_number(self.h_start, "vdist.h_start")
        _validate_finite_number(self.h_end, "vdist.h_end")
        if min(self.layer_start, self.layer_end) < 0:
            raise ValueError("vdist layer indices must be non-negative")
        if min(self.p_start, self.p_end, self.h_start, self.h_end) < 0:
            raise ValueError("vdist pressure and height values must be non-negative")

        if self.method == "range":
            if self.layer_start > self.layer_end:
                raise ValueError("layer_start must be <= layer_end")

        if self.method == "pressure":
            if self.p_start > self.p_end:
                raise ValueError("p_start must be <= p_end")

        if self.method == "height":
            if self.h_start > self.h_end:
                raise ValueError("h_start must be <= h_end")

    @classmethod
    def from_dict(
        cls, data: Mapping[str, Any], where: str = "vdist"
    ) -> VerticalDistributionConfig:
        values = _require_mapping(data, where)
        _reject_unknown_keys(
            values,
            {
                "method",
                "layer_start",
                "layer_end",
                "p_start",
                "p_end",
                "h_start",
                "h_end",
            },
            where,
        )
        result = cls(**values)
        result.validate()
        return result


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
    category: str = "1"
    scale_fields: List[str] = field(default_factory=list)
    vdist: VerticalDistributionConfig = field(
        default_factory=VerticalDistributionConfig
    )
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
        _validate_string(self.field_name, "field")
        _validate_string(self.operation, "operation")
        if self.operation not in ["add", "replace", "scale"]:
            raise ValueError(f"Invalid operation: {self.operation}")
        _validate_finite_number(self.scale, "scale")
        _validate_integer(self.hierarchy, "hierarchy")
        if self.hierarchy < 0:
            raise ValueError("hierarchy must be non-negative")
        _validate_string(self.category, "category")
        _validate_string_list(self.masks, "mask")
        _validate_string_list(self.scale_fields, "scale_fields")
        for key in ("diurnal_cycle", "weekly_cycle", "seasonal_cycle"):
            value = getattr(self, key)
            if value is not None:
                _validate_string(value, key)
        if self.scale < 0:
            raise ValueError("scale must be non-negative")
        self.vdist.validate()

    @classmethod
    def from_dict(cls, data: Mapping[str, Any], where: str = "layer") -> EmissionLayer:
        values = _require_mapping(data, where)
        _reject_unknown_keys(
            values,
            {
                "field",
                "operation",
                "mask",
                "scale",
                "hierarchy",
                "category",
                "scale_fields",
                "diurnal_cycle",
                "weekly_cycle",
                "seasonal_cycle",
                "vdist",
            },
            where,
        )
        if "field" not in values or "operation" not in values:
            missing = "field" if "field" not in values else "operation"
            raise ValueError(f"{where}: missing required key {missing!r}")
        masks = values.get("mask", [])
        if isinstance(masks, str):
            masks = [masks]
        elif not isinstance(masks, list) or any(
            not isinstance(mask, str) for mask in masks
        ):
            raise ValueError(f"{where}.mask must be a string or list of strings")
        vdist_data = values.get("vdist", {})
        vdist = VerticalDistributionConfig.from_dict(vdist_data, f"{where}.vdist")
        result = cls(
            field_name=values["field"],
            operation=values["operation"],
            masks=masks,
            scale=values.get("scale", 1.0),
            hierarchy=values.get("hierarchy", 0),
            category=values.get("category", "1"),
            scale_fields=values.get("scale_fields", []),
            vdist=vdist,
            diurnal_cycle=values.get("diurnal_cycle"),
            weekly_cycle=values.get("weekly_cycle"),
            seasonal_cycle=values.get("seasonal_cycle"),
        )
        try:
            result.validate()
        except ValueError as error:
            raise ValueError(f"{where}: {error}") from error
        return result


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
    refresh_interval_seconds: int = 0

    def validate(self) -> None:
        """
        Validate physics scheme parameters.

        Raises
        ------
        ValueError
            If ``name`` is empty or ``language`` is not recognized.
        """
        _validate_string(self.name, "scheme name")
        if self.language not in [
            "cpp",
            "c++",
            "cxx",
            "fortran",
            "f90",
            "f",
            "python",
            "py",
        ]:
            raise ValueError(f"Invalid language: {self.language}")
        _validate_string(self.language, "language")
        _require_mapping(self.options, "scheme options")
        _validate_integer(self.refresh_interval_seconds, "refresh_interval_seconds")
        if self.refresh_interval_seconds < 0:
            raise ValueError("refresh_interval_seconds must be non-negative")

    @classmethod
    def from_dict(
        cls, data: Mapping[str, Any], where: str = "physics scheme"
    ) -> PhysicsSchemeConfig:
        values = _require_mapping(data, where)
        _reject_unknown_keys(
            values,
            {"name", "language", "options", "refresh_interval_seconds"},
            where,
        )
        result = cls(
            name=values.get("name", ""),
            language=values.get("language", "cpp"),
            options=values.get("options", {}),
            refresh_interval_seconds=values.get("refresh_interval_seconds", 0),
        )
        try:
            result.validate()
        except ValueError as error:
            raise ValueError(f"{where}: {error}") from error
        return result


@dataclass
class DataVariableConfig:
    """A file-to-model variable mapping in a data stream."""

    file: str
    model: str
    levels: Optional[int] = None

    def validate(self) -> None:
        _validate_string(self.file, "variable file")
        _validate_string(self.model, "variable model")
        if self.levels is not None:
            _validate_integer(self.levels, "variable levels")
            if self.levels < 1:
                raise ValueError("variable levels must be >= 1")

    @classmethod
    def from_dict(cls, data: Any, where: str = "variable") -> DataVariableConfig:
        if isinstance(data, str):
            result = cls(file=data, model=data)
        else:
            values = _require_mapping(data, where)
            _reject_unknown_keys(values, {"file", "model", "levels"}, where)
            file_name = values.get("file", "")
            result = cls(
                file=file_name,
                model=values.get("model", file_name),
                levels=values.get("levels"),
            )
        try:
            result.validate()
        except ValueError as error:
            raise ValueError(f"{where}: {error}") from error
        return result


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
    variables : list of DataVariableConfig, optional
        File-to-model variable mappings. Default is an empty list.
    taxmode : str, optional
        Behavior when the simulation time falls outside the file's coverage.
        One of ``"cycle"`` (wrap), ``"extend"`` (clamp to the nearest end),
        or ``"limit"`` (fail). Default is ``"cycle"``.
    tintalgo : str, optional
        Time interpolation algorithm. One of ``"linear"``, ``"nearest"``.
        Default is ``"linear"``, matching the C++ config parser.
    mapalgo : str, optional
        Spatial mapping algorithm. One of ``"bilinear"``, ``"consd"``,
        ``"consf"``, ``"nn"``, ``"redist"``, or ``"passthrough"``
        (skip regridding when data is already on the model grid).
        Default is ``"bilinear"``.
    cadence : str, optional
        How file records are addressed. One of ``"series"`` (decode the
        file's time axis), ``"daily"``/``"monthly"`` (series with a calendar
        arithmetic fallback), ``"hourly"``/``"weekly"`` (climatological
        profile indexed by hour-of-day / day-of-week), or ``"stepwise"``
        (also spelled ``"step"``; ignore time and walk the record index).
        Default is ``"series"``.
    time_label : str, optional
        Position of each time coordinate relative to the implied valid interval
        associated with the data. One of ``"auto"``, ``"start"``, ``"center"``,
        or ``"end"``. Default is ``"auto"``.

    Examples
    --------
    >>> stream = DataStreamConfig(name="anthro_co", file_paths=["co.nc"])
    """

    name: str
    file_paths: List[str] = field(default_factory=list)
    variables: List[DataVariableConfig] = field(default_factory=list)
    taxmode: str = "cycle"
    tintalgo: str = "linear"
    mapalgo: str = "bilinear"
    cadence: str = "series"
    time_label: str = "auto"
    dtlimit: int = 1500000000
    yearFirst: int = 1
    yearLast: int = 1
    yearAlign: int = 1
    offset: int = 0
    meshfile: str = ""
    lev_dimname: str = "lev"
    time_var: str = "time"
    lon_var: str = "lon"
    lat_var: str = "lat"
    time_units: str = ""
    calendar: str = ""
    data_model: str = "auto"
    refresh_interval_seconds: int = 0

    def validate(self) -> None:
        """
        Validate data stream parameters.

        Raises
        ------
        ValueError
            If ``name`` is empty, ``file_paths`` is empty, ``taxmode`` is
            not recognized, ``tintalgo`` is not recognized, or ``cadence``
            is not recognized.
        """
        _validate_string(self.name, "stream name")
        if not self.file_paths:
            raise ValueError("file_paths cannot be empty")
        _validate_string_list(self.file_paths, "file")
        for path in self.file_paths:
            _validate_string(path, "file path")
        if not isinstance(self.variables, list) or any(
            not isinstance(variable, DataVariableConfig) for variable in self.variables
        ):
            raise ValueError("variables must contain DataVariableConfig objects")
        for variable in self.variables:
            variable.validate()
        for key in (
            "taxmode",
            "tintalgo",
            "mapalgo",
            "cadence",
            "time_label",
            "data_model",
        ):
            _validate_string(getattr(self, key), key)
        if self.taxmode not in ["cycle", "extend", "limit"]:
            raise ValueError(f"Invalid taxmode: {self.taxmode}")
        if self.time_label not in ["auto", "start", "center", "end"]:
            raise ValueError(f"Invalid time_label: {self.time_label}")
        if self.tintalgo not in ["linear", "nearest"]:
            raise ValueError(f"Invalid tintalgo: {self.tintalgo}")
        if self.cadence not in [
            "series",
            "daily",
            "monthly",
            "hourly",
            "weekly",
            "stepwise",
            "step",
        ]:
            raise ValueError(f"Invalid cadence: {self.cadence}")
        valid_mapalgos = {
            "passthrough",
            "nearest",
            "near",
            "nn",
            "bilinear",
            "bilin",
            "bi",
            "cubic",
            "bicubic",
            "cu",
            "conss",
            "conservative2nd",
            "cons2nd",
            "consd",
            "conservative",
            "cons",
            "conservative1st",
            "consf",
            "redist",
        }
        if self.mapalgo not in valid_mapalgos:
            raise ValueError(f"Invalid mapalgo: {self.mapalgo}")
        if self.data_model not in ["classic", "enhanced", "auto"]:
            raise ValueError(f"Invalid data_model: {self.data_model}")
        for key in (
            "dtlimit",
            "yearFirst",
            "yearLast",
            "yearAlign",
            "offset",
            "refresh_interval_seconds",
        ):
            _validate_integer(getattr(self, key), key)
        if self.dtlimit <= 0:
            raise ValueError("dtlimit must be positive")
        if (
            self.yearFirst != 0
            and self.yearLast != 0
            and self.yearLast < self.yearFirst
        ):
            raise ValueError("yearLast must be >= yearFirst")
        if self.refresh_interval_seconds < 0:
            raise ValueError("refresh_interval_seconds must be non-negative")
        for key in (
            "meshfile",
            "lev_dimname",
            "time_var",
            "lon_var",
            "lat_var",
            "time_units",
            "calendar",
        ):
            _validate_string(getattr(self, key), key, allow_empty=True)

    @classmethod
    def from_dict(
        cls, data: Mapping[str, Any], where: str = "data stream"
    ) -> DataStreamConfig:
        values = _require_mapping(data, where)
        allowed = {
            "name",
            "file",
            "variables",
            "taxmode",
            "tintalgo",
            "interpolation",
            "mapalgo",
            "cadence",
            "time_label",
            "dtlimit",
            "yearFirst",
            "yearLast",
            "yearAlign",
            "offset",
            "meshfile",
            "lev_dimname",
            "time_var",
            "lon_var",
            "lat_var",
            "time_units",
            "calendar",
            "data_model",
            "refresh_interval_seconds",
        }
        _reject_unknown_keys(values, allowed, where)
        if "tintalgo" in values and "interpolation" in values:
            raise ValueError(
                f"{where}: use either 'tintalgo' or 'interpolation', not both"
            )
        files = values.get("file", [])
        if isinstance(files, str):
            files = [files]
        if not isinstance(files, list) or any(
            not isinstance(path, str) for path in files
        ):
            raise ValueError(f"{where}.file must be a string or list of strings")
        raw_variables = values.get("variables", [])
        if not isinstance(raw_variables, list):
            raise ValueError(f"{where}.variables must be a list")
        variables = [
            DataVariableConfig.from_dict(item, f"{where}.variables[{index}]")
            for index, item in enumerate(raw_variables)
        ]
        name = _validate_string(values.get("name", ""), f"{where}.name")
        if not variables and name:
            variables = [DataVariableConfig(file=name, model=name)]

        def normalized(key: str, default: str, alias: Optional[str] = None) -> str:
            raw = values.get(key, values.get(alias, default) if alias else default)
            return _validate_string(raw, f"{where}.{key}").lower()

        result = cls(
            name=name,
            file_paths=files,
            variables=variables,
            taxmode=normalized("taxmode", "cycle"),
            tintalgo=normalized("tintalgo", "linear", "interpolation"),
            mapalgo=_validate_string(
                values.get("mapalgo", "bilinear"), f"{where}.mapalgo"
            ).lower(),
            cadence=normalized("cadence", "series"),
            time_label=normalized("time_label", "auto"),
            dtlimit=values.get("dtlimit", 1500000000),
            yearFirst=values.get("yearFirst", 1),
            yearLast=values.get("yearLast", 1),
            yearAlign=values.get("yearAlign", 1),
            offset=values.get("offset", 0),
            meshfile=values.get("meshfile", ""),
            lev_dimname=values.get("lev_dimname", "lev"),
            time_var=values.get("time_var", "time"),
            lon_var=values.get("lon_var", "lon"),
            lat_var=values.get("lat_var", "lat"),
            time_units=values.get("time_units", ""),
            calendar=values.get("calendar", ""),
            data_model=normalized("data_model", "auto"),
            refresh_interval_seconds=values.get("refresh_interval_seconds", 0),
        )
        try:
            result.validate()
        except ValueError as error:
            raise ValueError(f"{where}: {error}") from error
        return result


@dataclass
class GridConfig:
    grid_name: str = ""
    nx: int = 4
    ny: int = 4
    nz: int = 1
    lon_min: float = -135.0
    lon_max: float = 135.0
    lat_min: float = -67.5
    lat_max: float = 67.5

    def validate(self) -> None:
        _validate_string(self.grid_name, "driver.grid.grid_name", allow_empty=True)
        for key in ("nx", "ny", "nz"):
            _validate_integer(getattr(self, key), f"driver.grid.{key}")
            if getattr(self, key) < 1:
                raise ValueError(f"driver.grid.{key} must be >= 1")
        for key in ("lon_min", "lon_max", "lat_min", "lat_max"):
            _validate_finite_number(getattr(self, key), f"driver.grid.{key}")
        if self.lon_min >= self.lon_max:
            raise ValueError("driver.grid.lon_min must be less than lon_max")
        if self.lat_min >= self.lat_max:
            raise ValueError("driver.grid.lat_min must be less than lat_max")
        if self.lat_min < -90 or self.lat_max > 90:
            raise ValueError("driver.grid latitude bounds must be within [-90, 90]")

    @classmethod
    def from_dict(
        cls, data: Mapping[str, Any], where: str = "driver.grid"
    ) -> GridConfig:
        values = _require_mapping(data, where)
        _reject_unknown_keys(
            values,
            {
                "grid_name",
                "nx",
                "ny",
                "nz",
                "lon_min",
                "lon_max",
                "lat_min",
                "lat_max",
            },
            where,
        )
        result = cls(**values)
        result.validate()
        return result


@dataclass
class DriverConfig:
    start_time: str = "2020-01-01T00:00:00"
    end_time: str = "2020-01-02T00:00:00"
    timestep_seconds: int = 3600
    log_file: Optional[str] = None
    gridspec_file: Optional[str] = None
    grid: GridConfig = field(default_factory=GridConfig)
    stacking_refresh_interval_seconds: int = 0
    amio_worker_threads: int = 1
    amio_staging_buffer_count: int = 8
    amio_staging_buffer_capacity_bytes: int = 33554432
    amio_prefetch_depth: int = 2

    def validate(self) -> None:
        for key in ("start_time", "end_time"):
            _validate_string(getattr(self, key), f"driver.{key}")
        try:
            start = datetime.fromisoformat(self.start_time.replace("Z", "+00:00"))
            end = datetime.fromisoformat(self.end_time.replace("Z", "+00:00"))
        except ValueError as error:
            raise ValueError(
                "driver start_time and end_time must be ISO 8601 timestamps"
            ) from error
        try:
            if end <= start:
                raise ValueError("driver.end_time must be after start_time")
        except TypeError as error:
            raise ValueError(
                "driver start_time and end_time must use compatible time zones"
            ) from error
        _validate_integer(self.timestep_seconds, "driver.timestep_seconds")
        if self.timestep_seconds < 1:
            raise ValueError("driver.timestep_seconds must be >= 1")
        if self.log_file is not None:
            _validate_string(self.log_file, "driver.log_file")
        if self.gridspec_file is not None:
            _validate_string(self.gridspec_file, "driver.gridspec_file")
        self.grid.validate()
        _validate_integer(
            self.stacking_refresh_interval_seconds,
            "driver.stacking_refresh_interval_seconds",
        )
        if self.stacking_refresh_interval_seconds < 0:
            raise ValueError(
                "driver.stacking_refresh_interval_seconds must be non-negative"
            )
        if (
            self.stacking_refresh_interval_seconds
            and self.stacking_refresh_interval_seconds % self.timestep_seconds
        ):
            raise ValueError(
                "driver.stacking_refresh_interval_seconds must be a multiple "
                "of timestep_seconds"
            )
        for key in (
            "amio_worker_threads",
            "amio_staging_buffer_count",
            "amio_staging_buffer_capacity_bytes",
            "amio_prefetch_depth",
        ):
            _validate_integer(getattr(self, key), f"driver.{key}")
            if getattr(self, key) < 1:
                raise ValueError(f"driver.{key} must be >= 1")

    @classmethod
    def from_dict(cls, data: Mapping[str, Any], where: str = "driver") -> DriverConfig:
        values = _require_mapping(data, where)
        _reject_unknown_keys(
            values,
            {
                "start_time",
                "end_time",
                "timestep_seconds",
                "log_file",
                "gridspec_file",
                "grid",
                "stacking_refresh_interval_seconds",
                "amio_worker_threads",
                "amio_staging_buffer_count",
                "amio_staging_buffer_capacity_bytes",
                "amio_prefetch_depth",
            },
            where,
        )
        args = dict(values)
        args["grid"] = GridConfig.from_dict(values.get("grid", {}), f"{where}.grid")
        result = cls(**args)
        result.validate()
        return result


@dataclass
class OutputFieldConfig:
    name: str
    attributes: Dict[str, str] = field(default_factory=dict)

    def validate(self) -> None:
        _validate_string(self.name, "output field name")
        _validate_string_mapping(self.attributes, "output field attributes")

    @classmethod
    def from_dict(cls, data: Any, where: str = "output field") -> OutputFieldConfig:
        if isinstance(data, str):
            result = cls(name=data)
        else:
            values = _require_mapping(data, where)
            _reject_unknown_keys(values, {"name", "attributes"}, where)
            result = cls(
                name=values.get("name", ""),
                attributes=_validate_string_mapping(
                    values.get("attributes", {}), f"{where}.attributes"
                ),
            )
        try:
            result.validate()
        except ValueError as error:
            raise ValueError(f"{where}: {error}") from error
        return result


@dataclass
class OutputConfig:
    enabled: bool = True
    directory: str = "."
    filename_pattern: str = "cece_output_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
    frequency_steps: int = 1
    fields: List[OutputFieldConfig] = field(default_factory=list)
    amio_worker_threads: Optional[int] = None
    diagnostics: bool = False
    global_attributes: Dict[str, str] = field(default_factory=dict)

    def validate(self) -> None:
        if not isinstance(self.enabled, bool):
            raise ValueError("output.enabled must be a boolean")
        if not isinstance(self.diagnostics, bool):
            raise ValueError("output.diagnostics must be a boolean")
        _validate_string(self.directory, "output.directory")
        _validate_string(self.filename_pattern, "output.filename_pattern")
        _validate_string_mapping(self.global_attributes, "output.global_attributes")
        _validate_integer(self.frequency_steps, "output.frequency_steps")
        if self.frequency_steps < 1:
            raise ValueError("output.frequency_steps must be >= 1")
        if not isinstance(self.fields, list) or any(
            not isinstance(item, OutputFieldConfig) for item in self.fields
        ):
            raise ValueError("output.fields must contain OutputFieldConfig objects")
        for field_config in self.fields:
            field_config.validate()
        if self.amio_worker_threads is not None:
            _validate_integer(self.amio_worker_threads, "output.amio_worker_threads")
            if self.amio_worker_threads < 1:
                raise ValueError("output.amio_worker_threads must be >= 1")

    @classmethod
    def from_dict(cls, data: Mapping[str, Any], where: str = "output") -> OutputConfig:
        values = _require_mapping(data, where)
        _reject_unknown_keys(
            values,
            {
                "enabled",
                "directory",
                "filename_pattern",
                "frequency_steps",
                "fields",
                "amio_worker_threads",
                "diagnostics",
                "global_attributes",
            },
            where,
        )
        raw_fields = values.get("fields", [])
        if not isinstance(raw_fields, list):
            raise ValueError(f"{where}.fields must be a list")
        result = cls(
            enabled=values.get("enabled", True),
            directory=values.get("directory", "."),
            filename_pattern=values.get(
                "filename_pattern", "cece_output_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
            ),
            frequency_steps=values.get("frequency_steps", 1),
            fields=[
                OutputFieldConfig.from_dict(item, f"{where}.fields[{index}]")
                for index, item in enumerate(raw_fields)
            ],
            amio_worker_threads=values.get("amio_worker_threads"),
            diagnostics=values.get("diagnostics", False),
            global_attributes=_validate_string_mapping(
                values.get("global_attributes", {}), f"{where}.global_attributes"
            ),
        )
        result.validate()
        return result


@dataclass
class DiagnosticsConfig:
    output_interval: int = 0
    grid_type: str = "native"
    grid_file: str = ""
    nx: int = 0
    ny: int = 0
    variables: List[str] = field(default_factory=list)
    sequence_form: bool = False

    def validate(self) -> None:
        _validate_string(self.grid_type, "diagnostics.grid_type")
        _validate_integer(self.output_interval, "diagnostics.output_interval")
        if self.output_interval < 0:
            raise ValueError("diagnostics.output_interval must be non-negative")
        if self.grid_type not in {"native", "gaussian", "mesh"}:
            raise ValueError(f"Invalid diagnostics.grid_type: {self.grid_type}")
        _validate_string(self.grid_file, "diagnostics.grid_file", allow_empty=True)
        _validate_integer(self.nx, "diagnostics.nx")
        _validate_integer(self.ny, "diagnostics.ny")
        if self.nx < 0 or self.ny < 0:
            raise ValueError("diagnostics.nx and ny must be non-negative")
        if self.grid_type == "mesh" and not self.grid_file:
            raise ValueError(
                "diagnostics.grid_file is required when grid_type is 'mesh'"
            )
        _validate_string_list(self.variables, "diagnostics.variables")

    @classmethod
    def from_value(cls, data: Any, where: str = "diagnostics") -> DiagnosticsConfig:
        if isinstance(data, list):
            result = cls(
                variables=_validate_string_list(data, where), sequence_form=True
            )
        else:
            values = _require_mapping(data, where)
            _reject_unknown_keys(
                values,
                {"output_interval", "grid_type", "grid_file", "nx", "ny", "variables"},
                where,
            )
            result = cls(
                output_interval=values.get("output_interval", 0),
                grid_type=values.get("grid_type", "native"),
                grid_file=values.get("grid_file", ""),
                nx=values.get("nx", 0),
                ny=values.get("ny", 0),
                variables=_validate_string_list(
                    values.get("variables", []), f"{where}.variables"
                ),
            )
        result.validate()
        return result


@dataclass
class VerticalGridConfig:
    type: str = "none"
    ak_field: str = "hyam"
    bk_field: str = "hybm"
    p_surf_field: str = "ps"
    z_field: str = "height"
    pbl_field: str = "hpbl"

    def validate(self) -> None:
        _validate_string(self.type, "vertical_grid.type")
        if self.type not in {"fv3", "mpas", "wrf", "none"}:
            raise ValueError(f"Invalid vertical_grid type: {self.type}")
        for key in ("ak_field", "bk_field", "p_surf_field", "z_field", "pbl_field"):
            _validate_string(getattr(self, key), f"vertical_grid.{key}")

    @classmethod
    def from_dict(
        cls, data: Mapping[str, Any], where: str = "vertical_grid"
    ) -> VerticalGridConfig:
        values = _require_mapping(data, where)
        _reject_unknown_keys(
            values,
            {"type", "ak_field", "bk_field", "p_surf_field", "z_field", "pbl_field"},
            where,
        )
        result = cls(**values)
        result.validate()
        return result


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

    def __init__(
        self, is_valid: bool = True, errors: Optional[List[str]] = None
    ) -> None:
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
        self._cece_data: Dict[str, Any] = {"streams": [], "debug_level": 0}
        self._vertical_config = VerticalDistributionConfig()
        self._vertical_grid_config = VerticalGridConfig()
        self._vertical_grid_defined = False
        self._driver_config: Optional[DriverConfig] = None
        self._output_config: Optional[OutputConfig] = None
        self._diagnostics_config: Optional[DiagnosticsConfig] = None
        self._meteorology: Dict[str, str] = {}
        self._scale_factors: Dict[str, str] = {}
        self._masks: Dict[str, str] = {}
        self._met_registry: Dict[str, List[str]] = {}
        self._temporal_cycles: Dict[str, List] = {}
        self._temporal_profiles: Dict[str, List] = {}

        if config_dict is not None:
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
        _validate_string(name, "species name")
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
        scheme = PhysicsSchemeConfig(name, language, {} if options is None else options)
        scheme.validate()
        self._physics_schemes.append(scheme)

    def add_data_stream(
        self,
        name: str,
        file_paths: Union[str, List[str]],
        variables: Optional[Union[Dict[str, str], List[DataVariableConfig]]] = None,
        taxmode: str = "cycle",
        tintalgo: str = "linear",
        mapalgo: str = "bilinear",
        cadence: str = "series",
        time_label: str = "auto",
        **stream_options: Any,
    ) -> None:
        """
        Configure a TIDE data stream.

        Parameters
        ----------
        name : str
            Stream identifier.
        file_paths : list of str
            Paths to data files.
        variables : dict or list, optional
            Mapping of file variable names to model variable names, or a list
            of ``DataVariableConfig`` objects.
        taxmode : str, optional
            Behavior outside the file's coverage. One of ``"cycle"``,
            ``"extend"``, or ``"limit"``. Default is ``"cycle"``.
        tintalgo : str, optional
            Time interpolation algorithm. One of ``"linear"`` or
            ``"nearest"``. Default is ``"linear"``, matching the parser.
        mapalgo : str, optional
            Spatial mapping algorithm. One of ``"bilinear"``, ``"consd"``,
            ``"consf"``, ``"nn"``, ``"redist"``, or ``"passthrough"``
            (skip regridding when data is already on the model grid).
            Default is ``"bilinear"``.
        cadence : str, optional
            How file records are addressed: ``"series"``, ``"daily"``,
            ``"monthly"``, ``"hourly"``, ``"weekly"``, or ``"stepwise"``
            (alias ``"step"``).
            Default is ``"series"``.
        time_label : str, optional
            Position of each time coordinate relative to the validity interval
            associated with its data. One of ``"auto"``, ``"start"``,
            ``"center"``, or ``"end"``. Default is ``"auto"``.

        Raises
        ------
        ValueError
            If parameters fail validation.
        """
        if isinstance(file_paths, str):
            file_paths = [file_paths]
        if isinstance(variables, Mapping):
            variables = [
                DataVariableConfig(file=key, model=value)
                for key, value in variables.items()
            ]
        elif variables is None:
            variables = []
        if not variables and name:
            variables = [DataVariableConfig(file=name, model=name)]
        if any(not isinstance(item, DataVariableConfig) for item in variables):
            raise ValueError("variables must contain DataVariableConfig objects")
        allowed_options = {
            "dtlimit",
            "yearFirst",
            "yearLast",
            "yearAlign",
            "offset",
            "meshfile",
            "lev_dimname",
            "time_var",
            "lon_var",
            "lat_var",
            "time_units",
            "calendar",
            "data_model",
            "refresh_interval_seconds",
        }
        unknown_options = set(stream_options) - allowed_options
        if unknown_options:
            raise ValueError(
                f"Unknown data stream option: {sorted(unknown_options)[0]}"
            )
        stream = DataStreamConfig(
            name,
            file_paths,
            variables,
            taxmode,
            tintalgo,
            mapalgo,
            cadence,
            time_label,
            **stream_options,
        )
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
        _validate_string(name, "cycle name")
        _validate_temporal_factors(factors)
        self._temporal_cycles[name] = factors

    def add_temporal_profile(self, name: str, factors: List[float]) -> None:
        _validate_string(name, "temporal profile name")
        _validate_temporal_factors(factors)
        self._temporal_profiles[name] = factors

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

        def capture(where: str, validator: Any) -> None:
            try:
                validator()
            except (TypeError, ValueError) as error:
                errors.append(f"{where}: {error}")

        # Validate species
        for name, layers in self._species.items():
            if not name:
                errors.append("Species name cannot be empty")
            for index, layer in enumerate(layers):
                capture(f"species.{name}[{index}]", layer.validate)

        # Validate physics schemes
        for index, scheme in enumerate(self._physics_schemes):
            capture(f"physics_schemes[{index}]", scheme.validate)

        # Validate data streams
        for index, stream in enumerate(self._cece_data.get("streams", [])):
            capture(f"cece_data.streams[{index}]", stream.validate)

        for key, mapping in (
            ("meteorology", self._meteorology),
            ("scale_factors", self._scale_factors),
            ("masks", self._masks),
        ):
            capture(
                key,
                lambda mapping=mapping, key=key: _validate_string_mapping(mapping, key),
            )
        for name, aliases in self._met_registry.items():
            if not name:
                errors.append("met_registry keys cannot be empty")
            capture(
                f"met_registry.{name}",
                lambda aliases=aliases: _validate_string_list(aliases, "aliases"),
            )

        debug_level = self._cece_data.get("debug_level", 0)
        capture(
            "cece_data.debug_level",
            lambda: _validate_integer(debug_level, "debug_level"),
        )
        if (
            isinstance(debug_level, int)
            and not isinstance(debug_level, bool)
            and debug_level < 0
        ):
            errors.append("cece_data.debug_level must be non-negative")
        if self._driver_config is not None:
            capture("driver", self._driver_config.validate)
        if self._output_config is not None:
            capture("output", self._output_config.validate)
        if self._diagnostics_config is not None:
            capture("diagnostics", self._diagnostics_config.validate)
        if self._vertical_grid_defined:
            capture("vertical_grid", self._vertical_grid_config.validate)

        # Validate temporal cycles
        for section, cycles in (
            ("temporal_cycles", self._temporal_cycles),
            ("temporal_profiles", self._temporal_profiles),
        ):
            for name, factors in cycles.items():
                if not name:
                    errors.append(f"{section} name cannot be empty")
                capture(
                    f"{section}.{name}",
                    lambda factors=factors: _validate_temporal_factors(factors),
                )

        available_cycles = set(self._temporal_cycles) | set(self._temporal_profiles)
        expected_lengths = {
            "diurnal_cycle": 24,
            "weekly_cycle": 7,
            "seasonal_cycle": 12,
        }
        for species_name, layers in self._species.items():
            for index, layer in enumerate(layers):
                for attribute, expected_length in expected_lengths.items():
                    cycle_name = getattr(layer, attribute)
                    if cycle_name is None:
                        continue
                    if not isinstance(cycle_name, str) or not cycle_name:
                        continue
                    if cycle_name not in available_cycles:
                        errors.append(
                            f"species.{species_name}[{index}].{attribute} references "
                            f"undefined cycle {cycle_name!r}"
                        )
                        continue
                    factors = self._temporal_cycles.get(
                        cycle_name, self._temporal_profiles.get(cycle_name, [])
                    )
                    if len(factors) != expected_length:
                        errors.append(
                            f"{attribute} {cycle_name!r} must have "
                            f"{expected_length} factors; "
                            f"got {len(factors)}"
                        )

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
            raise ImportError(
                "PyYAML is required for YAML serialization. Install with: pip install pyyaml"
            )

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
        result: Dict[str, Any] = {
            "species": {
                name: [
                    {
                        "field": layer.field_name,
                        "operation": layer.operation,
                        "scale": layer.scale,
                        "hierarchy": layer.hierarchy,
                        "category": layer.category,
                        "mask": layer.masks,
                        "scale_fields": layer.scale_fields,
                        "diurnal_cycle": layer.diurnal_cycle,
                        "weekly_cycle": layer.weekly_cycle,
                        "seasonal_cycle": layer.seasonal_cycle,
                        "vdist": {
                            "method": layer.vdist.method,
                            "layer_start": layer.vdist.layer_start,
                            "layer_end": layer.vdist.layer_end,
                            "p_start": layer.vdist.p_start,
                            "p_end": layer.vdist.p_end,
                            "h_start": layer.vdist.h_start,
                            "h_end": layer.vdist.h_end,
                        },
                    }
                    for layer in layers
                ]
                for name, layers in self._species.items()
            },
            "physics_schemes": [
                {
                    "name": scheme.name,
                    "language": scheme.language,
                    "options": scheme.options,
                    "refresh_interval_seconds": scheme.refresh_interval_seconds,
                }
                for scheme in self._physics_schemes
            ],
            "cece_data": {
                "streams": [
                    {
                        "name": stream.name,
                        "file": stream.file_paths[0]
                        if len(stream.file_paths) == 1
                        else stream.file_paths,
                        "variables": [
                            {
                                "file": variable.file,
                                "model": variable.model,
                                **(
                                    {"levels": variable.levels}
                                    if variable.levels is not None
                                    else {}
                                ),
                            }
                            for variable in stream.variables
                        ],
                        "taxmode": stream.taxmode,
                        "tintalgo": stream.tintalgo,
                        "mapalgo": stream.mapalgo,
                        "cadence": stream.cadence,
                        "time_label": stream.time_label,
                        "dtlimit": stream.dtlimit,
                        "yearFirst": stream.yearFirst,
                        "yearLast": stream.yearLast,
                        "yearAlign": stream.yearAlign,
                        "offset": stream.offset,
                        "meshfile": stream.meshfile,
                        "lev_dimname": stream.lev_dimname,
                        "time_var": stream.time_var,
                        "lon_var": stream.lon_var,
                        "lat_var": stream.lat_var,
                        "time_units": stream.time_units,
                        "calendar": stream.calendar,
                        "data_model": stream.data_model,
                        "refresh_interval_seconds": stream.refresh_interval_seconds,
                    }
                    for stream in self._cece_data.get("streams", [])
                ]
            },
            "temporal_cycles": self._temporal_cycles,
        }
        if self._cece_data.get("debug_level", 0):
            result["cece_data"]["debug_level"] = self._cece_data["debug_level"]
        if self._temporal_profiles:
            result["temporal_profiles"] = self._temporal_profiles
        if self._meteorology:
            result["meteorology"] = self._meteorology
        if self._scale_factors:
            result["scale_factors"] = self._scale_factors
        if self._masks:
            result["masks"] = self._masks
        if self._met_registry:
            result["met_registry"] = self._met_registry
        if self._vertical_grid_defined:
            result["vertical_grid"] = {
                "type": self._vertical_grid_config.type,
                "ak_field": self._vertical_grid_config.ak_field,
                "bk_field": self._vertical_grid_config.bk_field,
                "p_surf_field": self._vertical_grid_config.p_surf_field,
                "z_field": self._vertical_grid_config.z_field,
                "pbl_field": self._vertical_grid_config.pbl_field,
            }
        if self._diagnostics_config is not None:
            diagnostics = self._diagnostics_config
            result["diagnostics"] = (
                diagnostics.variables
                if diagnostics.sequence_form
                else {
                    "output_interval": diagnostics.output_interval,
                    "grid_type": diagnostics.grid_type,
                    "grid_file": diagnostics.grid_file,
                    "nx": diagnostics.nx,
                    "ny": diagnostics.ny,
                    "variables": diagnostics.variables,
                }
            )
        if self._output_config is not None:
            output = self._output_config
            result["output"] = {
                "enabled": output.enabled,
                "directory": output.directory,
                "filename_pattern": output.filename_pattern,
                "frequency_steps": output.frequency_steps,
                "diagnostics": output.diagnostics,
                "global_attributes": output.global_attributes,
                "fields": [
                    {"name": item.name, "attributes": item.attributes}
                    for item in output.fields
                ],
            }
            if output.amio_worker_threads is not None:
                result["output"]["amio_worker_threads"] = output.amio_worker_threads
        if self._driver_config is not None:
            driver = self._driver_config
            result["driver"] = {
                "start_time": driver.start_time,
                "end_time": driver.end_time,
                "timestep_seconds": driver.timestep_seconds,
                "log_file": driver.log_file,
                "gridspec_file": driver.gridspec_file,
                "grid": {
                    "grid_name": driver.grid.grid_name,
                    "nx": driver.grid.nx,
                    "ny": driver.grid.ny,
                    "nz": driver.grid.nz,
                    "lon_min": driver.grid.lon_min,
                    "lon_max": driver.grid.lon_max,
                    "lat_min": driver.grid.lat_min,
                    "lat_max": driver.grid.lat_max,
                },
                "stacking_refresh_interval_seconds": driver.stacking_refresh_interval_seconds,
                "amio_worker_threads": driver.amio_worker_threads,
                "amio_staging_buffer_count": driver.amio_staging_buffer_count,
                "amio_staging_buffer_capacity_bytes": driver.amio_staging_buffer_capacity_bytes,
                "amio_prefetch_depth": driver.amio_prefetch_depth,
            }
        return result

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
            raise ImportError(
                "PyYAML is required for YAML parsing. Install with: pip install pyyaml"
            )

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
        values = _require_mapping(config_dict, "configuration")
        errors: List[str] = []
        allowed = {
            "species",
            "meteorology",
            "scale_factors",
            "masks",
            "met_registry",
            "temporal_cycles",
            "temporal_profiles",
            "physics_schemes",
            "diagnostics",
            "vertical_grid",
            "cece_data",
            "output",
            "driver",
        }
        try:
            _reject_unknown_keys(values, allowed, "configuration")
        except ValueError as error:
            errors.append(str(error))

        def capture(where: str, callback: Any) -> Any:
            try:
                return callback()
            except (TypeError, ValueError) as error:
                errors.append(f"{where}: {error}")
                return None

        if "species" in values:
            species_data = capture(
                "species", lambda: _require_mapping(values["species"], "species")
            )
            if species_data is not None:
                for name, layers_data in species_data.items():
                    name = capture(
                        f"species.{name}",
                        lambda name=name: _validate_string(name, "species name"),
                    )
                    if name is None:
                        continue
                    if not isinstance(layers_data, list):
                        errors.append(f"species.{name} must be a list of layers")
                        continue
                    layers: List[EmissionLayer] = []
                    for index, layer_data in enumerate(layers_data):
                        layer = capture(
                            f"species.{name}[{index}]",
                            lambda layer_data=layer_data, name=name, index=index: (
                                EmissionLayer.from_dict(
                                    layer_data, f"species.{name}[{index}]"
                                )
                            ),
                        )
                        if layer is not None:
                            layers.append(layer)
                    self._species[name] = layers

        for key, attribute in (
            ("meteorology", "_meteorology"),
            ("scale_factors", "_scale_factors"),
            ("masks", "_masks"),
        ):
            if key in values:
                parsed = capture(
                    key, lambda key=key: _validate_string_mapping(values[key], key)
                )
                if parsed is not None:
                    setattr(self, attribute, parsed)

        if "met_registry" in values:
            registry = capture(
                "met_registry",
                lambda: _require_mapping(values["met_registry"], "met_registry"),
            )
            if registry is not None:
                for name, aliases in registry.items():
                    if isinstance(aliases, str):
                        aliases = [aliases]
                    parsed = capture(
                        f"met_registry.{name}",
                        lambda aliases=aliases, name=name: _validate_string_list(
                            aliases, f"met_registry.{name}"
                        ),
                    )
                    if parsed is not None:
                        self._met_registry[name] = parsed

        for key, target in (
            ("temporal_cycles", self._temporal_cycles),
            ("temporal_profiles", self._temporal_profiles),
        ):
            if key in values:
                cycles = capture(
                    key, lambda key=key: _require_mapping(values[key], key)
                )
                if cycles is not None:
                    for name, factors in cycles.items():
                        checked_name = capture(
                            f"{key}.{name}",
                            lambda name=name, key=key: _validate_string(
                                name, f"{key} name"
                            ),
                        )
                        if checked_name is None:
                            continue
                        parsed = capture(
                            f"{key}.{checked_name}",
                            lambda factors=factors: _parse_temporal_factors(factors),
                        )
                        if parsed is not None:
                            target[checked_name] = parsed

        if "physics_schemes" in values:
            schemes = values["physics_schemes"]
            if not isinstance(schemes, list):
                errors.append("physics_schemes must be a list")
            else:
                for index, scheme_data in enumerate(schemes):
                    scheme = capture(
                        f"physics_schemes[{index}]",
                        lambda scheme_data=scheme_data, index=index: (
                            PhysicsSchemeConfig.from_dict(
                                scheme_data, f"physics_schemes[{index}]"
                            )
                        ),
                    )
                    if scheme is not None:
                        self._physics_schemes.append(scheme)

        if "diagnostics" in values:
            self._diagnostics_config = capture(
                "diagnostics",
                lambda: DiagnosticsConfig.from_value(values["diagnostics"]),
            )
        if "vertical_grid" in values:
            vertical = capture(
                "vertical_grid",
                lambda: VerticalGridConfig.from_dict(values["vertical_grid"]),
            )
            if vertical is not None:
                self._vertical_grid_config = vertical
                self._vertical_grid_defined = True

        if "cece_data" in values:
            data = capture(
                "cece_data", lambda: _require_mapping(values["cece_data"], "cece_data")
            )
            if data is not None:
                try:
                    _reject_unknown_keys(data, {"debug_level", "streams"}, "cece_data")
                except ValueError as error:
                    errors.append(str(error))
                self._cece_data["debug_level"] = data.get("debug_level", 0)
                capture(
                    "cece_data.debug_level",
                    lambda: _validate_integer(
                        self._cece_data["debug_level"], "cece_data.debug_level"
                    ),
                )
                streams = data.get("streams", [])
                if not isinstance(streams, list):
                    errors.append("cece_data.streams must be a list")
                else:
                    for index, stream_data in enumerate(streams):
                        stream = capture(
                            f"cece_data.streams[{index}]",
                            lambda stream_data=stream_data, index=index: (
                                DataStreamConfig.from_dict(
                                    stream_data, f"cece_data.streams[{index}]"
                                )
                            ),
                        )
                        if stream is not None:
                            self._cece_data["streams"].append(stream)

        if "output" in values:
            self._output_config = capture(
                "output", lambda: OutputConfig.from_dict(values["output"])
            )
        if "driver" in values:
            self._driver_config = capture(
                "driver", lambda: DriverConfig.from_dict(values["driver"])
            )

        validation = self.validate()
        if not validation:
            errors.extend(validation.errors)
        if errors:
            raise ValueError(
                "Invalid configuration:\n"
                + "\n".join(f"  - {error}" for error in errors)
            )

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
        """VerticalDistributionConfig : Legacy default distribution settings."""
        return self._vertical_config

    @property
    def vertical_grid(self) -> VerticalGridConfig:
        return self._vertical_grid_config

    @property
    def driver(self) -> Optional[DriverConfig]:
        return self._driver_config

    @property
    def output(self) -> Optional[OutputConfig]:
        return self._output_config

    @property
    def diagnostics(self) -> Optional[DiagnosticsConfig]:
        return self._diagnostics_config

    @property
    def meteorology(self) -> Dict[str, str]:
        return self._meteorology

    @property
    def scale_factors(self) -> Dict[str, str]:
        return self._scale_factors

    @property
    def masks(self) -> Dict[str, str]:
        return self._masks

    @property
    def met_registry(self) -> Dict[str, List[str]]:
        return self._met_registry

    @property
    def temporal_cycles(self) -> Dict[str, List[float]]:
        return self._temporal_cycles

    @property
    def temporal_profiles(self) -> Dict[str, List[float]]:
        return self._temporal_profiles
