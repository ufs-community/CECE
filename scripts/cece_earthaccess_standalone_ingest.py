#!/usr/bin/env python3
"""Fetch one timestep of CECE earthaccess streams for the standalone driver.

This helper is invoked by the native C++ standalone driver when a YAML config
contains ``cece_data.streams`` entries with ``source: earthaccess``. It opens
NASA Earthdata granules through earthaccess/xarray, applies configured field
transforms, interpolates to the CECE target grid, and writes raw float64 arrays
that the C++ driver injects into the CECE import state.
"""

from __future__ import annotations

import argparse
import os
import re
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable, List, Optional, Tuple

import numpy as np
import yaml


def _load_coords(path: Path) -> np.ndarray:
    return np.asarray(
        [float(line.strip()) for line in path.read_text().splitlines() if line.strip()],
        dtype=np.float64,
    )


def _bounding_box_from_grid(grid: dict) -> Optional[Tuple[float, float, float, float]]:
    required = ("lon_min", "lon_max", "lat_min", "lat_max")
    if not all(key in grid for key in required):
        return None
    return (
        float(grid["lon_min"]),
        float(grid["lat_min"]),
        float(grid["lon_max"]),
        float(grid["lat_max"]),
    )


def _earthaccess_streams(config: dict) -> List[dict]:
    grid = config.get("driver", {}).get("grid", {}) or {}
    streams = []
    for stream in config.get("cece_data", {}).get("streams", []) or []:
        if stream.get("source") != "earthaccess":
            continue
        item = dict(stream)
        if item.get("bounding_box") is None:
            item["bounding_box"] = _bounding_box_from_grid(grid)
        streams.append(item)
    return streams


def _mapping_target_and_transform(mapping: Any) -> Tuple[str, Optional[str]]:
    if isinstance(mapping, str):
        return mapping, None
    if isinstance(mapping, dict):
        target = mapping.get("model") or mapping.get("field") or mapping.get("name")
        if not target:
            raise ValueError("earthaccess variable mapping dict must contain 'model'")
        return str(target), mapping.get("transform")
    raise TypeError("earthaccess variable mapping must be a string or mapping dict")


def _mapping_fill_value(mapping: Any) -> Optional[float]:
    if not isinstance(mapping, dict) or mapping.get("fill_value") is None:
        return None
    fill_value = float(mapping["fill_value"])
    if not np.isfinite(fill_value):
        raise ValueError("earthaccess variable fill_value must be finite")
    return fill_value


def _mapping_scale(mapping: Any) -> float:
    if not isinstance(mapping, dict) or mapping.get("scale") is None:
        return 1.0
    scale = float(mapping["scale"])
    if not np.isfinite(scale):
        raise ValueError("earthaccess variable scale must be finite")
    return scale


def _apply_transform(values: np.ndarray, transform: Optional[str]) -> np.ndarray:
    if transform is None or transform == "none":
        return values
    if transform == "cos_degrees":
        return np.clip(np.cos(np.deg2rad(values)), 0.0, 1.0)
    if transform == "cos_radians":
        return np.clip(np.cos(values), 0.0, 1.0)
    raise ValueError(f"Unsupported earthaccess variable transform: {transform}")


def _apply_fill_value(
    values: np.ndarray, fill_value: Optional[float], field_name: str
) -> np.ndarray:
    non_finite = ~np.isfinite(values)
    count = int(np.count_nonzero(non_finite))
    if count == 0 or fill_value is None:
        return values
    print(f"Replacing {count} non-finite value(s) in {field_name!r} with {fill_value}")
    return np.where(non_finite, fill_value, values)


def _solar_cosine(
    timestamp: datetime, target_lons: np.ndarray, target_lats: np.ndarray
) -> np.ndarray:
    day_of_year = timestamp.timetuple().tm_yday
    fractional_hour = (
        timestamp.hour + timestamp.minute / 60.0 + timestamp.second / 3600.0
    )
    fractional_year = (
        2.0 * np.pi / 365.0 * (day_of_year - 1 + (fractional_hour - 12.0) / 24.0)
    )
    equation_of_time = 229.18 * (
        0.000075
        + 0.001868 * np.cos(fractional_year)
        - 0.032077 * np.sin(fractional_year)
        - 0.014615 * np.cos(2.0 * fractional_year)
        - 0.040849 * np.sin(2.0 * fractional_year)
    )
    declination = (
        0.006918
        - 0.399912 * np.cos(fractional_year)
        + 0.070257 * np.sin(fractional_year)
        - 0.006758 * np.cos(2.0 * fractional_year)
        + 0.000907 * np.sin(2.0 * fractional_year)
        - 0.002697 * np.cos(3.0 * fractional_year)
        + 0.00148 * np.sin(3.0 * fractional_year)
    )

    solar_minutes = fractional_hour * 60.0 + equation_of_time + 4.0 * target_lons
    hour_angle = np.deg2rad(solar_minutes / 4.0 - 180.0)
    latitude = np.deg2rad(target_lats)[:, np.newaxis]
    cosine = (
        np.sin(latitude) * np.sin(declination)
        + np.cos(latitude) * np.cos(declination) * np.cos(hour_angle)[np.newaxis, :]
    )
    return np.clip(cosine, 0.0, 1.0)


def _fractional_day_of_year(timestamp: datetime, nx: int, ny: int) -> np.ndarray:
    fraction = (
        timestamp.hour * 3600.0 + timestamp.minute * 60.0 + timestamp.second
    ) / 86400.0
    return np.full((ny, nx), timestamp.timetuple().tm_yday + fraction)


def _sunshine_hours(
    timestamp: datetime, target_lats: np.ndarray, nx: int
) -> np.ndarray:
    day_angle = 2.0 * np.pi * (timestamp.timetuple().tm_yday - 1) / 365.0
    declination = 0.006918 - 0.399912 * np.cos(day_angle) + 0.070257 * np.sin(day_angle)
    latitude = np.deg2rad(target_lats)
    sunset_argument = np.clip(-np.tan(latitude) * np.tan(declination), -1.0, 1.0)
    daylight = 24.0 * np.arccos(sunset_argument) / np.pi
    return np.broadcast_to(daylight[:, np.newaxis], (target_lats.size, nx)).copy()


def _relative_humidity_percent(
    temperature_k: np.ndarray, specific_humidity: np.ndarray, pressure_pa: np.ndarray
) -> np.ndarray:
    temperature_c = temperature_k - 273.15
    vapor_pressure = (
        specific_humidity * pressure_pa / (0.622 + 0.378 * specific_humidity)
    )
    saturation_pressure = 611.2 * np.exp(
        17.67 * temperature_c / (temperature_c + 243.5)
    )
    return np.clip(100.0 * vapor_pressure / saturation_pressure, 0.0, 100.0)


def _validate_field(field_name: str, values: np.ndarray) -> None:
    if not np.all(np.isfinite(values)):
        raise ValueError(f"earthaccess field {field_name!r} contains non-finite values")
    if field_name == "solar_cosine" and (
        float(np.nanmin(values)) < -1.0e-12 or float(np.nanmax(values)) > 1.0 + 1.0e-12
    ):
        raise ValueError(
            "earthaccess field 'solar_cosine' must be in [0, 1]; "
            "use transform: cos_degrees or transform: cos_radians for solar zenith angle inputs"
        )


def _coord_name(data_array: Any, candidates: Iterable[str]) -> Optional[str]:
    names = set(data_array.coords) | set(data_array.dims)
    for candidate in candidates:
        if candidate in names:
            return candidate
    lowered = {name.lower(): name for name in names}
    for candidate in candidates:
        match = lowered.get(candidate.lower())
        if match is not None:
            return match
    return None


def _normalize_longitudes(lon: np.ndarray) -> np.ndarray:
    return ((lon + 180.0) % 360.0) - 180.0


def _select_time(data_array: Any, timestamp: np.datetime64) -> Any:
    if "time" in data_array.coords or "time" in data_array.dims:
        return data_array.sel(time=timestamp, method="nearest")
    return data_array


def _interp_to_target(
    data_array: Any, target_lons: np.ndarray, target_lats: np.ndarray
) -> np.ndarray:
    lon_name = _coord_name(
        data_array, ("lon", "longitude", "LON", "grid_lon", "grid_lont", "x")
    )
    lat_name = _coord_name(
        data_array, ("lat", "latitude", "LAT", "grid_lat", "grid_latt", "y")
    )

    data_array = data_array.squeeze(drop=True)
    if lon_name is None or lat_name is None:
        values = np.asarray(data_array.values, dtype=np.float64).squeeze()
        if values.shape == (target_lats.size, target_lons.size):
            return values
        if values.shape == (target_lons.size, target_lats.size):
            return values.T
        raise ValueError(
            f"Cannot identify latitude/longitude coordinates for variable {data_array.name!r}"
        )

    if lon_name in data_array.coords:
        lon_values = np.asarray(data_array[lon_name].values, dtype=np.float64)
        if lon_values.ndim == 1:
            data_array = data_array.assign_coords(
                {lon_name: _normalize_longitudes(lon_values)}
            )
            data_array = data_array.sortby(lon_name)

    if lat_name in data_array.coords:
        lat_values = np.asarray(data_array[lat_name].values, dtype=np.float64)
        if lat_values.ndim == 1:
            data_array = data_array.sortby(lat_name)

    interpolated = data_array.sel(
        {lon_name: target_lons, lat_name: target_lats}, method="nearest"
    )
    values = np.asarray(interpolated.values, dtype=np.float64).squeeze()

    expected = (target_lats.size, target_lons.size)
    if values.shape == expected:
        return values
    transposed = (target_lons.size, target_lats.size)
    if values.shape == transposed:
        return values.T
    raise ValueError(
        f"Interpolated field {data_array.name!r} has shape {values.shape}, expected {expected}"
    )


def _safe_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name)


def _write_field(
    output_dir: Path, field_name: str, values: np.ndarray, nx: int, ny: int
) -> str:
    values = np.asarray(values, dtype=np.float64)
    _validate_field(field_name, values)
    flat = np.ascontiguousarray(values.reshape(-1))
    binary_path = output_dir / f"{_safe_name(field_name)}.f64"
    flat.tofile(binary_path)
    return f"{field_name} {nx} {ny} 1 {binary_path.name} {float(np.min(flat)):.17g} {float(np.max(flat)):.17g}"


def _effective_provider(stream: dict) -> Any:
    daac = stream.get("daac")
    if stream.get("cloud_hosted", True) and daac == "LPDAAC_ECS":
        return "LPCLOUD"
    return daac


def _auth_strategy() -> str:
    return os.getenv("CECE_EARTHACCESS_AUTH_STRATEGY", "all")


def _prepare_auth_environment(strategy: str) -> None:
    if strategy == "netrc":
        os.environ.pop("EARTHDATA_TOKEN", None)


def _search_granules(earthaccess: Any, stream: dict, provider: Any) -> List[Any]:
    try:
        return earthaccess.search_data(
            short_name=stream["short_name"],
            temporal=(stream["temporal_start"], stream["temporal_end"]),
            bounding_box=stream.get("bounding_box"),
            version=stream.get("version"),
            cloud_hosted=stream.get("cloud_hosted", True),
            provider=provider,
            count=-1,
        )
    except RuntimeError as exc:
        if "Token does not exist" in str(exc):
            raise RuntimeError(
                "NASA CMR rejected a stale or invalid EARTHDATA_TOKEN. Unset "
                "EARTHDATA_TOKEN and use --auth-strategy netrc with a ~/.netrc entry "
                "whose password field contains the Earthdata account password, or export "
                "a newly generated valid Earthdata token."
            ) from exc
        raise


def _granule_data_links(granule: Any) -> List[str]:
    data_links = getattr(granule, "data_links", None)
    if callable(data_links):
        return [str(link) for link in data_links()]
    return [str(link) for link in getattr(granule, "data_links", []) or []]


def _first_data_link(granules: List[Any]) -> Optional[str]:
    for granule in granules:
        for link in _granule_data_links(granule):
            lowered = link.lower()
            if lowered.startswith(("http://", "https://", "s3://")):
                return link
    return None


def _is_hdf_eos_link(link: Optional[str]) -> bool:
    if not link:
        return False
    lowered = link.lower().split("?", 1)[0]
    return lowered.endswith(".hdf")


def _raise_if_unsupported_granule_format(stream: dict, granules: List[Any]) -> None:
    first_link = _first_data_link(granules)
    if not _is_hdf_eos_link(first_link):
        return

    raise RuntimeError(
        "EarthAccess stream "
        f"{stream.get('name', '<unnamed>')!r} returned HDF-EOS/HDF4 granules, "
        "which CECE's current cloud extras cannot read with xarray+h5netcdf. "
        f"Example data link: {first_link}. Use a NetCDF4/HDF5-compatible Earthdata "
        "collection for this field, or pre-convert the HDF-EOS granules to NetCDF "
        "on a login/data-transfer node and configure CECE to read those local files."
    )


def _download_granules(
    granules: List[Any], download_dir: Path, provider: Any
) -> List[Path]:
    import earthaccess
    from earthaccess.exceptions import EulaNotAccepted

    download_dir.mkdir(parents=True, exist_ok=True)
    try:
        paths = earthaccess.download(
            granules,
            local_path=download_dir,
            provider=provider,
            threads=int(os.getenv("CECE_EARTHACCESS_DOWNLOAD_THREADS", "8")),
            show_progress=False,
        )
    except EulaNotAccepted as exc:
        protected_url = _first_data_link(granules)
        raise RuntimeError(
            "NASA Earthdata denied this protected download because the account has not "
            "accepted the required EULA or authorized the provider application. In a web "
            "browser, sign in to the same Earthdata account used by ~/.netrc, open the "
            "protected file URL below, follow the redirect to authorize GES DISC and accept "
            "any displayed terms, then retry with --auth-strategy netrc. Also verify the "
            "application appears at https://urs.earthdata.nasa.gov/profile under Authorized "
            "Apps. "
            f"protected_url={protected_url!r}, "
            f"provider={provider!r}, download_dir={str(download_dir)!r}, error={exc}"
        ) from exc
    except Exception as exc:
        raise RuntimeError(
            "EarthAccess granule download failed. Confirm the active Earthdata credentials "
            "are authorized for this provider and collection. "
            f"provider={provider!r}, download_dir={str(download_dir)!r}, "
            f"error={exc.__class__.__name__}: {exc}"
        ) from exc
    return [Path(path) for path in paths]


def _open_dataset(stream: dict, download_dir: Optional[Path] = None) -> Any:
    import earthaccess
    import xarray as xr

    try:
        import h5py  # noqa: F401
    except ImportError as exc:
        raise RuntimeError(
            "The h5netcdf backend requires h5py to read EarthAccess NetCDF4 files. "
            "Install or refresh CECE cloud dependencies with: "
            "python -m pip install -e '.[cloud,test]'"
        ) from exc

    strategy = _auth_strategy()
    _prepare_auth_environment(strategy)
    provider = _effective_provider(stream)
    granules = _search_granules(earthaccess, stream, provider)
    if not granules:
        raise RuntimeError(
            "No earthaccess granules found for stream "
            f"{stream.get('name', '<unnamed>')!r} "
            f"(short_name={stream.get('short_name')!r}, version={stream.get('version')!r}, "
            f"provider={provider!r}, configured_daac={stream.get('daac')!r}, "
            f"cloud_hosted={stream.get('cloud_hosted', True)!r}, "
            f"temporal=({stream.get('temporal_start')!r}, {stream.get('temporal_end')!r}), "
            f"bounding_box={stream.get('bounding_box')!r})"
        )
    _raise_if_unsupported_granule_format(stream, granules)

    open_kwargs = {}
    if stream.get("block_size") is not None:
        open_kwargs["block_size"] = stream["block_size"]
    if stream.get("cache_type") is not None:
        open_kwargs["cache_type"] = stream["cache_type"]

    dataset_kwargs = {"engine": "h5netcdf", "combine": "by_coords"}

    earthaccess.login(strategy=strategy)
    if download_dir is not None:
        local_paths = _download_granules(granules, download_dir, provider)
        return xr.open_mfdataset(local_paths, **dataset_kwargs)

    try:
        file_objs = (
            earthaccess.open(granules, **open_kwargs)
            if open_kwargs
            else earthaccess.open(granules)
        )
    except TypeError:
        file_objs = earthaccess.open(granules)

    return xr.open_mfdataset(file_objs, **dataset_kwargs)


def _is_transient_remote_error(exc: BaseException) -> bool:
    transient_statuses = {429, 500, 502, 503, 504}
    transient_types = {
        "ClientConnectionError",
        "ServerDisconnectedError",
        "TimeoutError",
    }
    visited = set()
    current: Optional[BaseException] = exc
    while current is not None and id(current) not in visited:
        visited.add(id(current))
        if getattr(current, "status", None) in transient_statuses:
            return True
        if current.__class__.__name__ in transient_types:
            return True
        current = current.__cause__ or current.__context__
    return False


def _read_stream(
    stream: dict,
    download_dir: Optional[Path],
    timestamp: np.datetime64,
    timestamp_datetime: datetime,
    target_lons: np.ndarray,
    target_lats: np.ndarray,
    output_dir: Path,
) -> List[str]:
    dataset = _open_dataset(stream, download_dir)
    manifest_lines = []
    try:
        for nasa_var, mapping in (stream.get("variables") or {}).items():
            field_name, transform = _mapping_target_and_transform(mapping)
            fill_value = _mapping_fill_value(mapping)
            scale = _mapping_scale(mapping)
            if nasa_var not in dataset:
                raise KeyError(
                    f"Variable {nasa_var!r} not found in earthaccess stream {stream.get('name', '<unnamed>')!r}"
                )
            selected = _select_time(dataset[nasa_var], timestamp)
            values = _interp_to_target(selected, target_lons, target_lats)
            values = _apply_transform(values, transform) * scale
            values = _apply_fill_value(values, fill_value, field_name)
            manifest_lines.append(
                _write_field(
                    output_dir,
                    field_name,
                    values,
                    target_lons.size,
                    target_lats.size,
                )
            )

        for field_name, specification in (
            stream.get("derived_variables") or {}
        ).items():
            method = (
                specification.get("method")
                if isinstance(specification, dict)
                else specification
            )
            if method == "solar_cosine":
                values = _solar_cosine(timestamp_datetime, target_lons, target_lats)
            elif method == "day_of_year":
                values = _fractional_day_of_year(
                    timestamp_datetime, target_lons.size, target_lats.size
                )
            elif method == "sunshine_hours":
                values = _sunshine_hours(
                    timestamp_datetime, target_lats, target_lons.size
                )
            elif method == "relative_humidity":
                if not isinstance(specification, dict):
                    raise ValueError(
                        "relative_humidity derived variable requires a mapping"
                    )
                source_names = {
                    "temperature": specification.get("temperature", "T2M"),
                    "specific_humidity": specification.get("specific_humidity", "QV2M"),
                    "pressure": specification.get("pressure", "PS"),
                }
                source_values = {}
                for source, nasa_var in source_names.items():
                    if nasa_var not in dataset:
                        raise KeyError(
                            f"Variable {nasa_var!r} required for relative_humidity is missing"
                        )
                    selected = _select_time(dataset[nasa_var], timestamp)
                    source_values[source] = _interp_to_target(
                        selected, target_lons, target_lats
                    )
                values = _relative_humidity_percent(
                    source_values["temperature"],
                    source_values["specific_humidity"],
                    source_values["pressure"],
                )
            else:
                raise ValueError(
                    f"Unsupported earthaccess derived variable method: {method}"
                )
            manifest_lines.append(
                _write_field(
                    output_dir,
                    field_name,
                    values,
                    target_lons.size,
                    target_lats.size,
                )
            )
        return manifest_lines
    finally:
        dataset.close()


def _read_stream_with_retries(*args: Any, **kwargs: Any) -> List[str]:
    attempts = max(1, int(os.getenv("CECE_EARTHACCESS_STREAM_ATTEMPTS", "4")))
    initial_delay = max(
        0.0, float(os.getenv("CECE_EARTHACCESS_RETRY_DELAY_SECONDS", "2"))
    )
    stream = args[0] if args else kwargs["stream"]
    for attempt in range(1, attempts + 1):
        try:
            return _read_stream(*args, **kwargs)
        except Exception as exc:
            if attempt == attempts or not _is_transient_remote_error(exc):
                raise
            delay = initial_delay * (2 ** (attempt - 1))
            print(
                f"Transient EarthAccess failure for stream {stream.get('name', '<unnamed>')!r} "
                f"(attempt {attempt}/{attempts}, {exc.__class__.__name__}: {exc}); "
                f"reopening in {delay:g} seconds"
            )
            time.sleep(delay)

    raise AssertionError("unreachable")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--time", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--lon-file", required=True, type=Path)
    parser.add_argument("--lat-file", required=True, type=Path)
    parser.add_argument(
        "--download-dir",
        type=Path,
        help="Optional directory where protected EarthAccess granules are downloaded before xarray opens them",
    )
    args = parser.parse_args()

    config = yaml.safe_load(args.config.read_text())
    if not isinstance(config, dict):
        raise ValueError("CECE config must be a YAML mapping")

    streams = _earthaccess_streams(config)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    target_lons = _load_coords(args.lon_file)
    target_lats = _load_coords(args.lat_file)
    timestamp_datetime = datetime.fromisoformat(
        args.time.replace("Z", "+00:00")
    ).replace(tzinfo=None)
    timestamp = np.datetime64(timestamp_datetime)

    manifest_lines = []
    for stream in streams:
        stream_download_dir = None
        if args.download_dir is not None:
            stream_download_dir = args.download_dir / _safe_name(
                stream.get("name", "stream")
            )
        manifest_lines.extend(
            _read_stream_with_retries(
                stream,
                stream_download_dir,
                timestamp,
                timestamp_datetime,
                target_lons,
                target_lats,
                args.output_dir,
            )
        )

    (args.output_dir / "manifest.txt").write_text(
        "\n".join(manifest_lines) + ("\n" if manifest_lines else "")
    )
    print(f"Wrote {len(manifest_lines)} earthaccess field(s) to {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
