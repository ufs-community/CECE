"""Python lifecycle for the shared standalone and NUOPC simulation facade."""
from __future__ import annotations

from datetime import datetime, timezone
import math
from pathlib import Path

import numpy as np
import yaml

from . import _cece_core
from .grid import Grid
from .model import _seconds


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
    currently supports rectilinear grids and one MPI rank. External Python
    input injection is not yet supported.
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
        self._native = _cece_simulation.Simulation(str(path))
        try:
            geo = self._native.geometry()
            levels = tuple(range(geo['nz']))
            if geo['topology'] != 0:
                raise ValueError('Python simulation currently requires a rectilinear grid')
            self._grid = Grid.structured(longitude=geo['longitude'], latitude=geo['latitude'], levels=levels)
            self._shape = (geo['nx'], geo['j1'] - geo['j0'], geo['nz'])
        except Exception:
            self._native.close()
            raise
        self._index = 0
        self._complete = False
        self._closed = False
        self._failed = False

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
