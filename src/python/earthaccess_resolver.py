"""Resolve CECE data streams directly from NASA Earthdata via earthaccess."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

try:
    import earthaccess
    import xarray as xr

    _EARTHACCESS_AVAILABLE = True
except ImportError:
    _EARTHACCESS_AVAILABLE = False


def _require_earthaccess() -> None:
    if not _EARTHACCESS_AVAILABLE:
        raise ImportError(
            "earthaccess cloud streaming requires the 'cloud' extras. "
            "Install with: pip install 'cece-tools[cloud]'"
        )


@dataclass
class EarthAccessStreamConfig:
    """CMR search parameters for a single NASA Earthdata granule collection.

    Parameters
    ----------
    name : str
        Stream identifier (matches cece_data stream name in YAML).
    short_name : str
        NASA CMR dataset short name (e.g. ``"MCD15A2H"``).
    temporal_start : str
        ISO-8601 start date, e.g. ``"2022-07-01"``.
    temporal_end : str
        ISO-8601 end date, e.g. ``"2022-07-03"``.
    variable_map : dict
        Mapping of NASA variable name -> CECE import state field name.
    bounding_box : tuple or None
        ``(west, south, east, north)`` in decimal degrees, or ``None`` for global.
    version : str or None
        CMR dataset version string, or ``None`` to use the latest.
    cloud_hosted : bool
        When ``True``, restrict search to Earthdata Cloud granules.
    daac : str or None
        DAAC short name (e.g. ``"NSIDC"``, ``"LPDAAC_ECS"``), or ``None``.
    """

    name: str
    short_name: str
    temporal_start: str
    temporal_end: str
    variable_map: dict = field(default_factory=dict)
    bounding_box: Optional[tuple] = None
    version: Optional[str] = None
    cloud_hosted: bool = True
    daac: Optional[str] = None


class EarthAccessStreamResolver:
    """Opens remote NASA granules as fsspec streams without local download.

    Authenticates once via NASA Earthdata Login (EDL) using the strategy
    specified at construction time, then exposes ``open_as_xarray`` to
    build a lazily-loaded ``xr.Dataset`` backed by remote S3/HTTPS objects.

    Authentication order for the default ``"all"`` strategy:

    1. ``EARTHDATA_TOKEN`` or ``EARTHDATA_USERNAME`` / ``EARTHDATA_PASSWORD``
       environment variables.
    2. ``~/.netrc`` (or ``NETRC`` env var).
    3. Interactive prompt (suitable for notebooks/REPL only).

    Parameters
    ----------
    auth_strategy : str
        Passed directly to ``earthaccess.login(strategy=...)``.
        One of ``"all"``, ``"environment"``, ``"netrc"``, ``"interactive"``.
    """

    def __init__(self, auth_strategy: str = "all") -> None:
        _require_earthaccess()
        self._auth = earthaccess.login(strategy=auth_strategy)

    def open_as_xarray(self, cfg: EarthAccessStreamConfig) -> xr.Dataset:
        """Search CMR, open matching granules as fsspec streams, return Dataset.

        No bytes are written to local disk; ``h5netcdf`` reads directly from
        the remote file objects returned by ``earthaccess.open()``.

        Parameters
        ----------
        cfg : EarthAccessStreamConfig
            Search parameters and variable mapping for this stream.

        Returns
        -------
        xr.Dataset
            Lazily-loaded dataset combining all matched granules.

        Raises
        ------
        RuntimeError
            If no granules are found for the given search criteria.
        ImportError
            If the ``cloud`` optional dependencies are not installed.
        """
        granules = earthaccess.search_data(
            short_name=cfg.short_name,
            temporal=(cfg.temporal_start, cfg.temporal_end),
            bounding_box=cfg.bounding_box,
            version=cfg.version,
            cloud_hosted=cfg.cloud_hosted,
            daac=cfg.daac,
            count=-1,
        )

        if not granules:
            raise RuntimeError(
                f"No earthaccess granules found: short_name={cfg.short_name!r} "
                f"temporal=({cfg.temporal_start!r}, {cfg.temporal_end!r})"
            )

        file_objs = earthaccess.open(granules)
        return xr.open_mfdataset(
            file_objs,
            combine="by_coords",
            # h5netcdf reads HDF5/NetCDF4 directly from file-like objects
            engine="h5netcdf",
        )
