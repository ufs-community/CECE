"""Resolve CECE data streams directly from NASA Earthdata via earthaccess."""

from __future__ import annotations

import warnings
from dataclasses import dataclass, field
from typing import List, Optional

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
    block_size : int or None
        Optional fsspec read block size (bytes) forwarded to ``earthaccess.open``.
    cache_type : str or None
        Optional fsspec cache strategy (e.g. ``"readahead"``, ``"blockcache"``)
        forwarded to ``earthaccess.open``.
    use_virtual : bool
        When ``True``, prefer ``earthaccess.open_virtual_mfdataset`` (VirtualiZarr /
        DMR++ backed) over ``xr.open_mfdataset`` for large granule counts, falling
        back automatically when unsupported. See
        :class:`EarthAccessStreamResolver` for the tradeoffs.
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
    block_size: Optional[int] = None
    cache_type: Optional[str] = None
    use_virtual: bool = False


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

    Notes
    -----
    ``earthaccess.virtualize()`` / ``open_virtual_mfdataset`` (VirtualiZarr,
    DMR++ sidecar indices) was evaluated as an alternative to
    ``xr.open_mfdataset`` for large granule counts. It is not the default here
    because DMR++ sidecars are not guaranteed to exist for every DAAC/collection
    this module targets (SMAP SPL4SMGP, MODIS MCD15A2H, CERES SYN1deg), and
    falling back silently would hide a real configuration problem. It is
    available opt-in via ``EarthAccessStreamConfig.use_virtual=True``
    (``virtual: true`` in YAML); ``open_as_xarray`` falls back to
    ``open_mfdataset`` with a warning if the installed earthaccess version or
    collection does not support it.
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

        if cfg.use_virtual:
            dataset = self._try_open_virtual(cfg, granules)
            if dataset is not None:
                return dataset

        fs_kwargs = {}
        if cfg.block_size is not None:
            fs_kwargs["block_size"] = cfg.block_size
        if cfg.cache_type is not None:
            fs_kwargs["cache_type"] = cfg.cache_type

        try:
            file_objs = (
                earthaccess.open(granules, **fs_kwargs)
                if fs_kwargs
                else earthaccess.open(granules)
            )
        except TypeError:
            warnings.warn(
                f"Installed earthaccess version does not accept block_size/cache_type "
                f"kwargs for stream {cfg.name!r}; opening without fsspec tuning.",
                UserWarning,
                stacklevel=2,
            )
            file_objs = earthaccess.open(granules)

        return xr.open_mfdataset(
            file_objs,
            combine="by_coords",
            # h5netcdf reads HDF5/NetCDF4 directly from file-like objects
            engine="h5netcdf",
        )

    def _try_open_virtual(
        self, cfg: EarthAccessStreamConfig, granules
    ) -> Optional[xr.Dataset]:
        """Attempt VirtualiZarr/DMR++-backed access; return None to fall back."""
        opener = getattr(earthaccess, "open_virtual_mfdataset", None)
        if opener is None:
            warnings.warn(
                f"Stream {cfg.name!r} requested use_virtual=True, but the installed "
                "earthaccess version has no open_virtual_mfdataset; falling back to "
                "open_mfdataset.",
                UserWarning,
                stacklevel=2,
            )
            return None
        try:
            return opener(granules, access="indirect", load=False)
        except Exception as exc:  # collection lacks DMR++ sidecars, API mismatch, etc.
            warnings.warn(
                f"earthaccess.open_virtual_mfdataset failed for stream {cfg.name!r} "
                f"({exc.__class__.__name__}: {exc}); falling back to open_mfdataset.",
                UserWarning,
                stacklevel=2,
            )
            return None


def validate_short_name(cfg: EarthAccessStreamConfig) -> bool:
    """Check whether ``cfg.short_name`` resolves to a real CMR collection.

    Intended to be called at config-parse time so a typo'd or renamed
    ``short_name`` is reported before the run starts, rather than surfacing as
    an empty-granule ``RuntimeError`` at the first timestep. Never raises:
    network failures, missing credentials, or a missing ``earthaccess``
    install all degrade to a warning instead of blocking config parsing.

    Returns
    -------
    bool
        ``False`` only when CMR was successfully queried and returned no
        matching collection (a `UserWarning` is also emitted in that case).
        ``True`` otherwise, including when validation could not be performed.
    """
    if not _EARTHACCESS_AVAILABLE:
        warnings.warn(
            f"Skipping CMR short_name validation for stream {cfg.name!r} "
            f"({cfg.short_name!r}): earthaccess is not installed.",
            UserWarning,
            stacklevel=2,
        )
        return True
    try:
        datasets = earthaccess.search_datasets(short_name=cfg.short_name, count=1)
    except Exception as exc:  # network/auth failure, CMR outage, API change, etc.
        warnings.warn(
            f"Could not validate short_name {cfg.short_name!r} for stream {cfg.name!r} "
            f"against CMR ({exc.__class__.__name__}: {exc}); continuing without validation.",
            UserWarning,
            stacklevel=2,
        )
        return True
    if not datasets:
        warnings.warn(
            f"CMR has no collection matching short_name={cfg.short_name!r} "
            f"(stream {cfg.name!r}, daac={cfg.daac!r}, version={cfg.version!r}); "
            "this stream will fail at the first timestep unless short_name is corrected.",
            UserWarning,
            stacklevel=2,
        )
        return False
    return True


def validate_short_names(configs: List[EarthAccessStreamConfig]) -> List[str]:
    """Validate every config's short_name against CMR.

    Returns
    -------
    list of str
        Names of streams whose short_name failed CMR validation. Never raises.
    """
    return [cfg.name for cfg in configs if not validate_short_name(cfg)]
