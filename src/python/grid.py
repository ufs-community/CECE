"""Explicit column geometry and reversible structured-grid indexing."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass(frozen=True)
class Grid:
    """A fixed domain with structured cells flattened longitude-fastest.

    Parameters
    ----------
    shape : tuple of int
        Horizontal dimension sizes.
    dims : tuple of str
        Horizontal dimension names.
    longitude, latitude : tuple of float
        Geographic coordinates in degrees, per column or structured axis.
    levels : tuple of float, optional
        Explicit level coordinates; defaults to a single level at zero.
    """

    shape: tuple[int, ...]
    dims: tuple[str, ...]
    longitude: tuple[float, ...]
    latitude: tuple[float, ...]
    levels: tuple[float, ...] = (0.0,)

    def __post_init__(self):
        if (
            not self.shape
            or any(n <= 0 for n in self.shape)
            or len(self.shape) != len(self.dims)
        ):
            raise ValueError(
                "Grid requires positive shape and matching dimension names"
            )
        if len(set(self.dims)) != len(self.dims) or any(not name for name in self.dims):
            raise ValueError("Grid dimension names must be nonempty and unique")
        if not self.levels or not np.all(np.isfinite(self.levels)):
            raise ValueError("Grid requires finite level coordinates")
        expected_lon = (
            self.shape[-1] if self.dims == ("latitude", "longitude") else self.ncol
        )
        expected_lat = (
            self.shape[0] if self.dims == ("latitude", "longitude") else self.ncol
        )
        if len(self.longitude) != expected_lon or len(self.latitude) != expected_lat:
            raise ValueError("Grid coordinate counts do not match geometry")
        if not np.all(np.isfinite(self.longitude)) or not np.all(
            np.isfinite(self.latitude)
        ):
            raise ValueError("Grid coordinates must be finite")
        if any(abs(lat) > 90 for lat in self.latitude):
            raise ValueError("Latitude must be in [-90, 90]")

    @property
    def ncol(self):
        """int: Number of horizontal columns."""
        return int(np.prod(self.shape))

    @property
    def nlev(self):
        """int: Number of levels per column."""
        return len(self.levels)

    @classmethod
    def point(cls, longitude, latitude, levels=None):
        """Construct a single column.

        Parameters
        ----------
        longitude, latitude : float
            Point coordinates in degrees.
        levels : sequence of float, optional
            Level coordinates; defaults to one level.

        Returns
        -------
        Grid
            Single-column geometry.
        """
        return cls.columns([longitude], [latitude], levels=levels)

    @classmethod
    def columns(cls, longitude, latitude, levels=None):
        """Construct independent columns from paired coordinates.

        Parameters
        ----------
        longitude, latitude : array_like
            Matching one-dimensional coordinates in degrees.
        levels : sequence of float, optional
            Level coordinates shared by all columns.

        Returns
        -------
        Grid
            Column geometry.
        """
        lon = np.asarray(longitude, dtype=float)
        lat = np.asarray(latitude, dtype=float)
        if lon.ndim != 1 or lat.shape != lon.shape:
            raise ValueError("Column longitude and latitude must be matching vectors")
        return cls(
            (lon.size,),
            ("column",),
            tuple(lon),
            tuple(lat),
            (0.0,) if levels is None else tuple(levels),
        )

    @classmethod
    def structured(cls, *, longitude, latitude, levels=None):
        """Construct a rectilinear latitude-longitude grid.

        Parameters
        ----------
        longitude, latitude : array_like
            One-dimensional longitude and latitude axes in degrees.
        levels : sequence of float, optional
            Level coordinates shared by all cells.

        Returns
        -------
        Grid
            Structured geometry, ordered latitude then longitude.
        """
        lon = np.asarray(longitude, dtype=float)
        lat = np.asarray(latitude, dtype=float)
        if lon.ndim != 1 or lat.ndim != 1:
            raise ValueError("Structured longitude and latitude must be vectors")
        return cls(
            (lat.size, lon.size),
            ("latitude", "longitude"),
            tuple(lon),
            tuple(lat),
            (0.0,) if levels is None else tuple(levels),
        )

    def coordinates(self):
        """Return xarray-compatible coordinates for the horizontal geometry.

        Returns
        -------
        dict
            Coordinate names and values or ``(dimension, values)`` pairs.
        """
        if self.dims == ("latitude", "longitude"):
            return {"latitude": list(self.latitude), "longitude": list(self.longitude)}
        return {
            "column": np.arange(self.ncol),
            "longitude": ("column", list(self.longitude)),
            "latitude": ("column", list(self.latitude)),
        }

    def to_dict(self):
        """Return JSON-serializable geometry.

        Returns
        -------
        dict
            Shape, dimensions, coordinates, and levels.
        """
        return {
            "shape": [int(n) for n in self.shape],
            "dims": list(self.dims),
            "longitude": [float(x) for x in self.longitude],
            "latitude": [float(x) for x in self.latitude],
            "levels": [float(x) for x in self.levels],
        }
