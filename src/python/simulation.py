"""Python lifecycle for the shared standalone and NUOPC simulation facade."""
from __future__ import annotations

from collections.abc import Mapping
from datetime import datetime, timezone
import math
from pathlib import Path

import numpy as np
import yaml

from . import _cece_core
from .grid import Grid
from .model import _seconds

_UNIT_ALIASES = {
    'dimensionless': '1',
    'none': '1',
    'm/s': 'm s-1',
    'W/m2': 'W m-2',
    'kg/m2/s': 'kg m-2 s-1',
}


def _configured_import_contract(config, levels):
    """Collect field names and known units consumed by the CECE execution."""
    fields = set()
    units = {}

    for layers in config.get('species', {}).values():
        for layer in layers or []:
            if not isinstance(layer, Mapping):
                continue
            if layer.get('field'):
                fields.add(layer['field'])
            fields.update(layer.get('scale_fields') or [])
            fields.update(layer.get('masks') or [])

    for section in ('meteorology', 'scale_factors', 'masks'):
        mapping = config.get(section, {}) or {}
        if isinstance(mapping, Mapping):
            for source, target in mapping.items():
                fields.add(source)
                fields.add(target)

    for stream in (config.get('cece_data', {}) or {}).get('streams', []):
        variables = stream.get('variables')
        if isinstance(variables, Mapping):
            fields.update(variables.values())
        elif variables:
            for variable in variables:
                fields.add(variable if isinstance(variable, str) else variable.get('model', ''))
        elif stream.get('name'):
            fields.add(stream['name'])

    for scheme in config.get('physics_schemes', []) or []:
        name = scheme.get('name')
        if not name:
            continue
        options = dict(scheme.get('options') or {})
        if name == 'megan3':
            data = Path(__file__).parent / 'data' / 'speciation'
            options.setdefault('mechanism_file', str(data / 'spc_cb6.yaml'))
            options.setdefault('speciation_file', str(data / 'map_cb6.yaml'))
        try:
            descriptor = _cece_core.describe_scheme(name, yaml.safe_dump(options), levels)
        except (ValueError, RuntimeError):
            # Registered schemes without a reviewed generic field descriptor
            # remain usable by the native simulation, but cannot be inferred
            # as external Python imports here.
            continue
        for spec in descriptor.fields:
            if not spec.output:
                fields.add(spec.name)
                units[spec.name] = spec.units

    import_fields = (config.get('nuopc', {}) or {}).get('import_fields', {}) or {}
    for name, definition in import_fields.items():
        fields.add(name)
        if isinstance(definition, Mapping) and definition.get('units'):
            units[name] = definition['units']

    for section in ('meteorology', 'scale_factors', 'masks'):
        mapping = config.get(section, {}) or {}
        if isinstance(mapping, Mapping):
            for source, target in mapping.items():
                if target in units:
                    units[source] = units[target]
                elif source in units:
                    units[target] = units[source]

    return {name for name in fields if isinstance(name, str) and name}, units


class Simulation:
    """Run a configured, single-rank CECE simulation through the shared facade.

    Parameters
    ----------
    config : str or pathlib.Path
        Path to a CECE driver YAML file. The native driver controls ingestion,
        output writing, and scheduling according to this configuration.

    Notes
    -----
    Only one full simulation may be active per process. Python simulation
    currently supports rectilinear grids and one MPI rank. External imports
    accept surface fields and profiles matching the configured vertical grid.
    """

    def __init__(self, config):
        from . import _cece_simulation

        path = Path(config)
        with path.open(encoding='utf-8') as stream:
            config_data = yaml.safe_load(stream)
        clock = config_data['driver']
        self._next = _seconds(clock['start_time'])
        self._end = _seconds(clock['end_time'])
        self._dt = float(clock['timestep_seconds'])
        if not math.isfinite(self._dt) or self._dt <= 0 or self._end <= self._next:
            raise ValueError('Simulation requires a positive timestep and end time after start')
        _cece_core.initialize_kokkos()
        if not _cece_simulation.kokkos_initialized():
            raise RuntimeError('CECE simulation and physics extensions require the same shared Kokkos runtime')
        self._native = _cece_simulation.Simulation(str(path))
        try:
            geo = self._native.geometry()
            levels = tuple(range(geo['nz']))
            if geo['topology'] != 0:
                raise ValueError('Python simulation currently requires a rectilinear grid')
            self._grid = Grid.structured(longitude=geo['longitude'], latitude=geo['latitude'], levels=levels)
            self._shape = (geo['nx'], geo['j1'] - geo['j0'], geo['nz'])
            self._configured_import_fields, self._configured_import_units = _configured_import_contract(
                config_data, geo['nz']
            )
        except Exception:
            self._native.close()
            raise
        self._index = 0
        self._complete = False
        self._closed = False
        self._failed = False
        self._config_data = config_data

    @property
    def grid(self):
        """Grid: Resolved global geometry."""
        return self._grid

    @property
    def complete(self):
        """bool: Whether the configured simulation has completed."""
        return self._complete

    def step(self, *, time, dt):
        """Advance a configured interval and return owned output snapshots.

        Parameters
        ----------
        time : str, datetime.datetime, numpy.datetime64 or float
            Interval start, as UTC or epoch seconds.
        dt : float
            Interval duration in seconds; must equal the configured timestep.

        Returns
        -------
        xarray.Dataset
            Configured output fields at interval end, with independent arrays.
        """
        if self._closed:
            raise RuntimeError('CECE simulation is closed')
        if self._failed:
            raise RuntimeError('CECE simulation failed; create a new instance')
        start = _seconds(time)
        duration = float(dt)
        if (self._complete or abs(start - self._next) > 1e-6 or duration != self._dt or
                start != int(start) or duration != int(duration)):
            raise ValueError('Simulation steps must follow the configured clock')
        end = start + duration
        if end > self._end + 1e-6:
            raise ValueError('Simulation step exceeds configured end time')
        def iso(value):
            return datetime.fromtimestamp(value, timezone.utc).strftime('%Y-%m-%dT%H:%M:%S')
        try:
            self._complete = self._native.step(iso(start), iso(end), self._index)
        except Exception:
            self._failed = True
            raise
        self._next = end
        self._index += 1
        import xarray as xr
        output = {}
        for name in self._native.output_names():
            values = np.asarray(self._native.snapshot(name), dtype=float).reshape(self._shape, order='F').copy()
            output[name] = (('longitude', 'latitude', 'level'), values)
        return xr.Dataset(output, coords={'longitude': list(self._grid.longitude),
                                          'latitude': list(self._grid.latitude),
                                          'level': list(self._grid.levels)})

    def set_inputs(self, data, mapping=None):
        """Stage external surface or profile fields for the next and later steps.

        Parameters
        ----------
        data : mapping or xarray.Dataset
            Fields in canonical CECE units. NumPy fields use
            ``(longitude, latitude)`` for surfaces or
            ``(longitude, latitude, level)`` for profiles. Labeled fields must
            declare units, may reorder these named dimensions, and must carry
            coordinates matching this simulation grid. Units are checked when
            the configured scheme declares a concrete field unit; source-contract
            fields require unit metadata but leave its interpretation to the source.
        mapping : mapping, optional
            Rename input data variables to CECE import field names.

        Raises
        ------
        ValueError
            If a field has invalid dimensions or coordinates, maps to a
            duplicate producer, is not consumed by the configured run, or is
            also produced by a configured stream.
        TypeError
            If ``data`` is not a mapping or xarray Dataset.

        Notes
        -----
        Updates are copied into native-owned storage and applied after file
        ingestion, immediately before the physics step. A repeated call
        replaces only the named fields; other staged values persist.
        """
        if self._closed or self._failed:
            raise RuntimeError('CECE simulation is closed or failed')
        if not isinstance(data, Mapping) and not hasattr(data, 'data_vars'):
            raise TypeError('Inputs must be a mapping or xarray Dataset')
        if mapping is None:
            mapping = {}
        elif not isinstance(mapping, Mapping):
            raise TypeError('mapping must be a mapping of source names to CECE field names')
        else:
            mapping = dict(mapping)
        variables = list(data.data_vars) if hasattr(data, 'data_vars') else list(data)
        prepared = {}
        nx, ny, grid_nz = self._shape
        lon = np.asarray(self._grid.longitude, dtype=float)
        lat = np.asarray(self._grid.latitude, dtype=float)
        for source_name in variables:
            name = mapping.get(source_name, source_name)
            if not isinstance(name, str) or not name:
                raise ValueError('Input mappings must resolve to non-empty field names')
            if name not in self._configured_import_fields:
                raise ValueError(f"unknown configured input {name!r}")
            if name in prepared:
                raise ValueError(f'Multiple external inputs map to {name!r}')
            if hasattr(data, 'data_vars'):
                value = data[source_name]
                units = value.attrs.get('units')
                if not isinstance(units, str) or not units.strip():
                    raise ValueError(f"External field {source_name!r} units are required")
                expected_units = self._configured_import_units.get(name)
                if (
                    expected_units
                    and expected_units not in ('caller', 'source_contract')
                    and _UNIT_ALIASES.get(units, units) != expected_units
                ):
                    raise ValueError(
                        f"External field {source_name!r} has units {units!r}; expected {expected_units!r}"
                    )
                if set(value.dims) not in ({'longitude', 'latitude'}, {'longitude', 'latitude', 'level'}):
                    raise ValueError(
                        f"External field {source_name!r} must have longitude, latitude, and optional level dimensions"
                    )
                for coordinate in ('longitude', 'latitude'):
                    if coordinate not in value.coords:
                        raise ValueError(
                            f"External field {source_name!r}: {coordinate} coordinates are required"
                        )
                if not np.array_equal(np.asarray(value['longitude']), lon) or not np.array_equal(np.asarray(value['latitude']), lat):
                    raise ValueError(
                        f"External field {source_name!r} coordinates do not match the simulation grid"
                    )
                dims = ('longitude', 'latitude', 'level') if 'level' in value.dims else ('longitude', 'latitude')
                if 'level' in value.dims:
                    if 'level' not in value.coords:
                        raise ValueError(f"External field {source_name!r}: level coordinates are required")
                    if not np.array_equal(np.asarray(value['level']), np.asarray(self._grid.levels)):
                        raise ValueError(
                            f"External field {source_name!r} level coordinates do not match the simulation grid"
                        )
                array = np.asarray(value.transpose(*dims).values, dtype=float)
            else:
                array = np.asarray(data[source_name], dtype=float)
            if array.shape not in ((nx, ny), (nx, ny, grid_nz)):
                raise ValueError(f"External field {name!r} has shape {array.shape}; expected {(nx, ny)} or {(nx, ny, grid_nz)}")
            if not np.isfinite(array).all():
                raise ValueError(f"External field {name!r} contains non-finite values")
            prepared[name] = np.asfortranarray(array)

        file_targets = set()
        stream_config = self._config_data.get('cece_data', {})
        for stream in stream_config.get('streams', []):
            variables_config = stream.get('variables')
            if not variables_config:
                variables_config = [stream.get('name', '')]
            if isinstance(variables_config, Mapping):
                targets = variables_config.values()
            else:
                targets = [item if isinstance(item, str) else item.get('model', '') for item in variables_config]
            file_targets.update(targets)
        config_mapping = {}
        for key in ('meteorology', 'scale_factors', 'masks'):
            config_mapping.update(self._config_data.get(key, {}) or {})
        normalized_files = {config_mapping.get(name, name) for name in file_targets}
        conflicts = normalized_files.intersection(config_mapping.get(name, name) for name in prepared)
        if conflicts:
            raise ValueError(f'External inputs conflict with configured file streams: {sorted(conflicts)}')

        # Validate the entire update above before changing the native model.
        for name, array in prepared.items():
            nz = array.shape[2] if array.ndim == 3 else 1
            self._native.set_input(name, array.ravel(order='F').tolist(), nx, ny, nz)

    def close(self):
        """Release simulation resources without finalizing the host runtime."""
        if not self._closed:
            rc = self._native.close()
            self._closed = True
            if rc:
                raise RuntimeError('CECE simulation teardown reported an error; see native log')

    def __enter__(self):
        if self._closed:
            raise RuntimeError('CECE simulation is closed')
        return self

    def __exit__(self, *args):
        self.close()
