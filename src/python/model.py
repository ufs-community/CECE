"""Persistent execution of existing CECE physics on columns or structured grids."""

from __future__ import annotations

import math
from collections.abc import Mapping
from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import yaml

from . import _cece_core
from .exceptions import CeceConfigError
from .field_adapter import canonical_field
from .grid import Grid


def _seconds(time):
    if isinstance(time, str):
        time = datetime.fromisoformat(time.replace("Z", "+00:00"))
    if isinstance(time, datetime):
        if time.tzinfo is None:
            time = time.replace(tzinfo=timezone.utc)
        return time.timestamp()
    if isinstance(time, np.datetime64):
        if np.isnat(time):
            raise ValueError("NaT is not a model time")
        return float(
            (time - np.datetime64("1970-01-01T00:00:00")) / np.timedelta64(1, "s")
        )
    value = float(time)
    if not math.isfinite(value):
        raise ValueError("Time must be finite")
    return value


def _validated_call(function, *args):
    # Legacy native translation classifies invalid_argument as CeceConfigError.
    # The new public model consistently reports invalid user inputs as ValueError.
    try:
        return function(*args)
    except CeceConfigError as error:
        raise ValueError(str(error)) from error


class Model:
    """Run explicitly selected, reviewed native physics schemes.

    ``step(time, dt)`` describes [time, time+dt), with UTC epoch seconds or an
    ISO timestamp. NumPy inputs are canonical-unit values; xarray fields carry
    explicit units/dimensions. Results are independent snapshots. No remote
    readers, regridding, stacking, or simulation writer runs implicitly.
    """

    def __init__(self, schemes, *, grid: Grid, options=None):
        if isinstance(schemes, str):
            schemes = [schemes]
        self._schemes = tuple(schemes)
        if not isinstance(grid, Grid):
            raise TypeError("grid must be a cece.Grid")
        if not self._schemes or len(set(self._schemes)) != len(self._schemes):
            raise ValueError("Select at least one scheme, without duplicates")
        self._grid = grid
        self._options = deepcopy(options or {})
        unknown = set(self._options) - set(self._schemes)
        if unknown:
            raise ValueError(
                f"Options supplied for unselected schemes: {sorted(unknown)}"
            )
        # Packaged scientific configuration travels with the Python package.
        data = Path(__file__).parent / "data" / "speciation"
        if "megan3" in self._schemes:
            config = self._options.setdefault("megan3", {})
            config.setdefault("mechanism_file", str(data / "spc_cb6.yaml"))
            config.setdefault("speciation_file", str(data / "map_cb6.yaml"))
        self._requests = [
            (name, yaml.safe_dump(self._options.get(name, {})))
            for name in self._schemes
        ]
        _cece_core.initialize_kokkos()
        self._runtime = _validated_call(
            _cece_core.SchemeRuntime, self._requests, grid.ncol, grid.nlev
        )
        self._specs = {}
        self._outputs = {}
        for descriptor in self._runtime.descriptors:
            for spec in descriptor.fields:
                if spec.output:
                    self._outputs[spec.name] = spec
                else:
                    self._specs[spec.name] = spec
        self._inputs = {}
        self._input_units = {}
        self._closed = False
        self._sources = []

    @property
    def options(self):
        """An independent snapshot; scientific configuration is fixed per model."""
        return deepcopy(self._options)

    @property
    def grid(self):
        return self._grid

    @property
    def schemes(self):
        return self._schemes

    def _open(self):
        if self._closed:
            raise RuntimeError("CECE Model is closed")

    def required_inputs(self):
        """Return canonical requirements not supplied by an earlier scheme."""
        self._open()
        return {
            name: {"units": spec.units, "axis": spec.axis, "extent": spec.extent}
            for name, spec in self._specs.items()
            if spec.required and name not in self._outputs
        }

    def _prepare(self, data, mapping=None):
        if not isinstance(data, Mapping) and not hasattr(data, "data_vars"):
            raise TypeError("Inputs must be a mapping or xarray Dataset")
        mapping = mapping or {}
        prepared = {}
        for source, value in data.items():
            target = mapping.get(source, source)
            if target in self._outputs:
                raise ValueError(f"Cannot replace scheme-owned output: {target}")
            if target not in self._specs:
                raise ValueError(
                    f"Unknown input {target!r}; available fields: {sorted(self._specs)}"
                )
            if target in prepared:
                raise ValueError(f"Multiple input producers map to {target!r}")
            prepared[target] = canonical_field(value, self._specs[target], self._grid)
        return prepared

    def validate_inputs(self, data, mapping=None):
        """Validate a prospective complete input set without changing the model."""
        self._open()
        prepared = self._prepare(data, mapping)
        missing = set(self.required_inputs()) - set(prepared)
        if missing:
            raise ValueError(f"Missing required inputs: {sorted(missing)}")
        return True

    def set_inputs(self, data, mapping=None):
        """Update forcing; validate the whole update before copying any fields."""
        self._open()
        prepared = self._prepare(data, mapping)
        owned = {target for _, source_mapping, _ in self._sources for target in source_mapping.values()}
        conflict = owned.intersection(prepared)
        if conflict:
            raise ValueError(f"Cannot update source-owned fields: {sorted(conflict)}")
        self._apply_inputs(prepared, data, mapping)

    def _apply_inputs(self, prepared, data, mapping=None):
        for name, values in prepared.items():
            self._runtime.set_field(name, values)
        self._inputs.update(prepared)
        for source, value in data.items():
            target = (mapping or {}).get(source, source)
            if target in prepared and self._specs[target].units == "caller":
                self._input_units[target] = getattr(value, "attrs", {}).get("units", "unknown")

    def add_source(self, source, *, mapping, selection=None):
        """Attach a provider; the model closes it when the model closes."""
        self._open()
        if not mapping or len(set(mapping.values())) != len(mapping):
            raise ValueError("Source mapping must name distinct destination fields")
        owned = {target for _, old_mapping, _ in self._sources for target in old_mapping.values()}
        duplicates = owned.intersection(mapping.values())
        if duplicates:
            raise ValueError(f"Multiple sources own fields: {sorted(duplicates)}")
        conflict = self._inputs.keys() & mapping.values()
        if conflict:
            raise ValueError(f"Fields already have explicit inputs: {sorted(conflict)}")
        unknown = set(mapping.values()) - self._specs.keys()
        if unknown or set(mapping.values()) & self._outputs.keys():
            raise ValueError(f"Invalid source destination fields: {sorted(unknown)}")
        source.open()
        self._sources.append((source, dict(mapping), dict(selection or {})))

    def step(self, *, time, dt):
        """Compute fluxes for a contiguous interval; retain prior results safely."""
        self._open()
        start, duration = _seconds(time), float(dt)
        if not math.isfinite(duration) or duration <= 0:
            raise ValueError("dt must be finite and positive")
        next_time = self._runtime.next_time
        if next_time is not None and abs(start - next_time) > 1.e-6:
            raise ValueError("Timesteps must be contiguous")
        updates = {}
        for source, mapping, selection in self._sources:
            data = source.read(np.datetime64(int(start * 1e6), "us"), selection=selection)
            for name, target in mapping.items():
                if name not in data:
                    raise ValueError(f"Source is missing variable: {name}")
                updates[target] = data[name]
        if updates:
            self._apply_inputs(self._prepare(updates), updates)
        missing = set(self.required_inputs()) - self._inputs.keys()
        if missing:
            raise ValueError(f"Missing required inputs: {sorted(missing)}")
        _validated_call(self._runtime.step, start, duration)
        values = {}
        for name, spec in self._outputs.items():
            shape = self._grid.shape + (
                () if spec.axis == "surface" else (spec.extent,)
            )
            values[name] = self._runtime.get_field(name).reshape(shape).copy()
        try:
            import xarray as xr
        except ImportError:
            return values
        fields = {
            name: (
                self._grid.dims + (() if spec.axis == "surface" else (spec.axis,)),
                values[name],
                {"units": self._input_units.get("base_anthropogenic_nox", "unknown") if spec.units == "caller" else spec.units},
            )
            for name, spec in self._outputs.items()
        }
        coordinates = self._grid.coordinates()
        if any(spec.axis == "level" for spec in self._outputs.values()):
            coordinates["level"] = list(self._grid.levels)
        result = xr.Dataset(fields, coords=coordinates)
        result.attrs.update(
            interval_start_seconds=start,
            interval_end_seconds=start + duration,
            time_reference="seconds since 1970-01-01T00:00:00Z",
        )
        return result

    def state_dict(self):
        """JSON-serializable restart for reviewed stateless/effective-history kernels."""
        self._open()
        if self._sources:
            raise ValueError("Restart with attached sources is not supported; detach and reconstruct forcing explicitly")
        return {
            "version": 1,
            "schemes": list(self._schemes),
            "options": deepcopy(self._options),
            "grid": self._grid.to_dict(),
            "next_time": self._runtime.next_time,
            "input_units": dict(self._input_units),
            "inputs": {name: value.tolist() for name, value in self._inputs.items()},
        }

    def load_state_dict(self, state):
        """Restore validated forcing/configuration/time by replacing the executor."""
        self._open()
        if self._sources:
            raise ValueError("Cannot restore a restart while sources are attached")
        if (
            state.get("version") != 1
            or state.get("schemes") != list(self._schemes)
            or state.get("grid") != self._grid.to_dict()
            or state.get("options") != self._options
        ):
            raise ValueError(
                "Restart schema, schemes, geometry or options do not match this model"
            )
        prepared = self._prepare(state["inputs"])
        replacement = _validated_call(
            _cece_core.SchemeRuntime, self._requests, self._grid.ncol, self._grid.nlev
        )
        try:
            for name, values in prepared.items():
                replacement.set_field(name, values)
            replacement.restore_time(state.get("next_time"))
        except Exception:
            replacement.close()
            raise
        self._runtime.close()
        self._runtime = replacement
        self._inputs = prepared
        self._input_units = dict(state.get("input_units", {}))

    def close(self):
        """Release this model's schemes/fields; leave embedding runtimes alone."""
        if not self._closed:
            error = None
            try:
                for source, _, _ in self._sources:
                    try:
                        source.close()
                    except Exception as exc:
                        if error is None:
                            error = exc
            finally:
                self._runtime.close()
                self._closed = True
            if error is not None:
                raise error

    def __enter__(self):
        self._open()
        return self

    def __exit__(self, *args):
        self.close()
