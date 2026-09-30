#!/usr/bin/env python3
"""Stage CECE EarthAccess stream inputs for offline compute-node runs.

Run this script on a login or data-transfer node with outbound network access.
It writes the same ``step_N/manifest.txt`` plus raw ``*.f64`` files that the
native CECE driver consumes at runtime when ``CECE_EARTHACCESS_STAGE_DIR`` is
set in the compute job.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any, Dict, Iterable, Iterator, List, Optional, Tuple

import numpy as np
import yaml


_DAAC_ALIASES = {
    "LPDAAC_ECS": "LPCLOUD",
}


def _parse_datetime(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00")).replace(tzinfo=None)


def _iter_step_times(
    start_time: datetime, end_time: datetime, timestep_seconds: int
) -> Iterator[Tuple[int, datetime]]:
    if timestep_seconds <= 0:
        raise ValueError("driver.timestep_seconds must be positive")
    step_time = start_time
    step_index = 0
    delta = timedelta(seconds=timestep_seconds)
    while step_time < end_time:
        yield step_index, step_time
        step_index += 1
        step_time += delta


def _require_mapping(config: Any) -> Dict[str, Any]:
    if not isinstance(config, dict):
        raise ValueError("CECE config must be a YAML mapping")
    return config


def _bounding_box_from_grid(
    grid: Dict[str, Any],
) -> Optional[Tuple[float, float, float, float]]:
    required = ("lon_min", "lon_max", "lat_min", "lat_max")
    if not all(key in grid for key in required):
        return None
    return (
        float(grid["lon_min"]),
        float(grid["lat_min"]),
        float(grid["lon_max"]),
        float(grid["lat_max"]),
    )


def _grid_coordinates(config: Dict[str, Any]) -> Tuple[np.ndarray, np.ndarray]:
    driver = config.get("driver") or {}
    grid = driver.get("grid") or {}
    if not isinstance(grid, dict):
        raise ValueError("driver.grid must be a YAML mapping")

    grid_name = str(grid.get("grid_name") or "")
    if grid_name == "HEMCO_4x5":
        return (
            np.linspace(-177.5, 177.5, 72, dtype=np.float64),
            np.concatenate([[-89.0], np.linspace(-86.0, 86.0, 44), [89.0]]).astype(
                np.float64
            ),
        )

    grid_number = None
    if len(grid_name) > 1 and grid_name[0] in {"F", "R"} and grid_name[1:].isdigit():
        grid_number = int(grid_name[1:])

    nx = int(grid.get("nx") or (4 * grid_number if grid_number is not None else 0))
    ny = int(grid.get("ny") or (2 * grid_number if grid_number is not None else 0))
    if nx <= 0 or ny <= 0:
        raise ValueError(
            "driver.grid must define positive nx/ny or a supported grid_name"
        )

    lon_min = float(grid.get("lon_min", -180.0))
    lon_max = float(grid.get("lon_max", 180.0))
    lat_min = float(grid.get("lat_min", -90.0))
    lat_max = float(grid.get("lat_max", 90.0))
    longitudes = lon_min + ((lon_max - lon_min) / nx) * (
        np.arange(nx, dtype=np.float64) + 0.5
    )
    latitudes = lat_min + ((lat_max - lat_min) / ny) * (
        np.arange(ny, dtype=np.float64) + 0.5
    )
    return longitudes, latitudes


def _write_vector(path: Path, values: Iterable[float]) -> None:
    path.write_text("".join(f"{value:.17g}\n" for value in values))


def _resolve_helper(config_path: Path, config: Dict[str, Any], repo_root: Path) -> Path:
    driver = config.get("driver") or {}
    helper_value = driver.get("earthaccess_helper")
    if helper_value:
        helper_path = Path(str(helper_value))
        if not helper_path.is_absolute():
            helper_path = config_path.parent / helper_path
        return helper_path.resolve()
    return repo_root / "scripts" / "cece_earthaccess_standalone_ingest.py"


def _load_config(path: Path) -> Dict[str, Any]:
    return _require_mapping(yaml.safe_load(path.read_text()))


def _earthaccess_streams(config: Dict[str, Any]) -> List[Dict[str, Any]]:
    grid = config.get("driver", {}).get("grid", {}) or {}
    streams = []
    for stream in config.get("cece_data", {}).get("streams", []) or []:
        if not isinstance(stream, dict) or stream.get("source") != "earthaccess":
            continue
        item = dict(stream)
        if item.get("bounding_box") is None:
            item["bounding_box"] = _bounding_box_from_grid(grid)
        streams.append(item)
    return streams


def _effective_provider(stream: Dict[str, Any]) -> Any:
    daac = stream.get("daac")
    if stream.get("cloud_hosted", True) and daac == "LPDAAC_ECS":
        return _DAAC_ALIASES.get(daac, daac)
    return daac


def _prepare_auth_environment(strategy: str) -> None:
    if strategy == "netrc":
        os.environ.pop("EARTHDATA_TOKEN", None)


def _search_granules(
    earthaccess: Any, stream: Dict[str, Any], provider: Any
) -> List[Any]:
    try:
        return earthaccess.search_data(
            short_name=stream["short_name"],
            temporal=(stream["temporal_start"], stream["temporal_end"]),
            bounding_box=stream.get("bounding_box"),
            version=stream.get("version"),
            cloud_hosted=stream.get("cloud_hosted", True),
            provider=provider,
            count=1,
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


def _raise_if_unsupported_granule_format(
    stream: Dict[str, Any], granules: List[Any]
) -> None:
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


def _validate_downloaded_variables(stream: Dict[str, Any], paths: List[Any]) -> None:
    try:
        import h5py  # noqa: F401
    except ImportError as exc:
        raise RuntimeError(
            "The h5netcdf backend requires h5py to read staged NetCDF4 files. "
            "Install or refresh CECE cloud dependencies with: "
            "python -m pip install -e '.[cloud,test]'"
        ) from exc

    import xarray as xr

    if not paths:
        raise RuntimeError(
            "EarthAccess returned no downloaded files for schema validation"
        )

    with xr.open_dataset(paths[0], engine="h5netcdf") as dataset:
        configured = set((stream.get("variables") or {}).keys())
        missing = sorted(configured.difference(dataset.variables))
        if missing:
            available = sorted(str(name) for name in dataset.variables)
            raise RuntimeError(
                "Downloaded EarthAccess sample is missing configured variables for stream "
                f"{stream.get('name', '<unnamed>')!r}: {missing}. "
                f"Available variables: {available}"
            )


def _preflight_streams(
    config: Dict[str, Any], auth_strategy: str, check_download_access: bool
) -> None:
    import earthaccess
    from earthaccess.exceptions import EulaNotAccepted

    _prepare_auth_environment(auth_strategy)
    for stream in _earthaccess_streams(config):
        provider = _effective_provider(stream)
        granules = _search_granules(earthaccess, stream, provider)
        if not granules:
            raise RuntimeError(
                "No earthaccess granules found during preflight for stream "
                f"{stream.get('name', '<unnamed>')!r} "
                f"(short_name={stream.get('short_name')!r}, version={stream.get('version')!r}, "
                f"provider={provider!r}, configured_daac={stream.get('daac')!r}, "
                f"cloud_hosted={stream.get('cloud_hosted', True)!r}, "
                f"temporal=({stream.get('temporal_start')!r}, {stream.get('temporal_end')!r}), "
                f"bounding_box={stream.get('bounding_box')!r})"
            )
        _raise_if_unsupported_granule_format(stream, granules)
        if check_download_access:
            try:
                earthaccess.login(strategy=auth_strategy)
                with tempfile.TemporaryDirectory(
                    prefix="cece-earthaccess-preflight-"
                ) as temp_dir:
                    paths = earthaccess.download(
                        granules,
                        local_path=temp_dir,
                        provider=provider,
                        threads=1,
                        show_progress=False,
                    )
                    if not paths:
                        raise RuntimeError("EarthAccess returned no downloaded files")
                    _validate_downloaded_variables(stream, paths)
            except EulaNotAccepted as exc:
                protected_url = _first_data_link(granules)
                raise RuntimeError(
                    "NASA Earthdata search succeeded, but protected download authorization "
                    f"failed for stream {stream.get('name', '<unnamed>')!r}. In a web browser, "
                    "sign in to the same Earthdata account used by ~/.netrc, open the protected "
                    "file URL below, follow the redirect to authorize GES DISC and accept any "
                    "displayed terms, then retry with --auth-strategy netrc. Verify the app at "
                    "https://urs.earthdata.nasa.gov/profile under Authorized Apps. "
                    f"protected_url={protected_url!r}, provider={provider!r}, error={exc}"
                ) from exc
        print(
            f"preflight ok: {stream.get('name', '<unnamed>')} "
            f"({len(granules)} sample granule, download_access={check_download_access})"
        )


def _copy_stage_metadata(stage_dir: Path, config_path: Path, step_count: int) -> None:
    metadata = {
        "config": str(config_path),
        "step_count": step_count,
        "usage": "Set CECE_EARTHACCESS_STAGE_DIR to this directory in the compute job.",
    }
    (stage_dir / "metadata.yaml").write_text(yaml.safe_dump(metadata, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        required=True,
        type=Path,
        help="CECE YAML config with source: earthaccess streams",
    )
    parser.add_argument(
        "--stage-dir",
        required=True,
        type=Path,
        help="Output directory visible to compute nodes",
    )
    parser.add_argument("--start-time", help="Override driver.start_time")
    parser.add_argument("--end-time", help="Override driver.end_time")
    parser.add_argument(
        "--timestep-seconds", type=int, help="Override driver.timestep_seconds"
    )
    parser.add_argument(
        "--max-steps", type=int, help="Only stage the first N model steps"
    )
    parser.add_argument(
        "--overwrite", action="store_true", help="Replace existing step_N directories"
    )
    parser.add_argument(
        "--preflight-only",
        action="store_true",
        help="Validate EarthAccess searches and exit without staging files",
    )
    parser.add_argument(
        "--auth-strategy",
        default=os.getenv("CECE_EARTHACCESS_AUTH_STRATEGY", "all"),
        help="EarthAccess login strategy: all, environment, netrc, or interactive",
    )
    parser.add_argument(
        "--check-download-access",
        action="store_true",
        help="During preflight, download one granule per stream to verify EULA/provider authorization",
    )
    parser.add_argument(
        "--download-dir",
        type=Path,
        help="Directory where raw EarthAccess granules are downloaded/cached (default: <stage-dir>/granules)",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    config_path = args.config.resolve()
    stage_dir = args.stage_dir.resolve()
    download_dir = (args.download_dir or (stage_dir / "granules")).resolve()
    config = _load_config(config_path)
    driver = config.get("driver") or {}

    if args.preflight_only:
        _preflight_streams(config, args.auth_strategy, args.check_download_access)
        return 0

    start_time = _parse_datetime(args.start_time or str(driver["start_time"]))
    end_time = _parse_datetime(args.end_time or str(driver["end_time"]))
    timestep_seconds = args.timestep_seconds or int(driver["timestep_seconds"])
    target_lons, target_lats = _grid_coordinates(config)
    helper_path = _resolve_helper(config_path, config, repo_root)
    if not helper_path.exists():
        raise FileNotFoundError(f"EarthAccess helper not found: {helper_path}")

    stage_dir.mkdir(parents=True, exist_ok=True)
    staged_steps: List[int] = []
    for step_index, step_time in _iter_step_times(
        start_time, end_time, timestep_seconds
    ):
        if args.max_steps is not None and len(staged_steps) >= args.max_steps:
            break

        step_dir = stage_dir / f"step_{step_index}"
        if step_dir.exists():
            if not args.overwrite:
                raise FileExistsError(
                    f"Refusing to overwrite existing stage directory: {step_dir}"
                )
            shutil.rmtree(step_dir)
        step_dir.mkdir(parents=True)

        lon_file = step_dir / "target_lons.txt"
        lat_file = step_dir / "target_lats.txt"
        _write_vector(lon_file, target_lons)
        _write_vector(lat_file, target_lats)

        helper_env = os.environ.copy()
        helper_env["CECE_EARTHACCESS_AUTH_STRATEGY"] = args.auth_strategy
        if args.auth_strategy == "netrc":
            helper_env.pop("EARTHDATA_TOKEN", None)
        try:
            subprocess.run(
                [
                    sys.executable,
                    str(helper_path),
                    "--config",
                    str(config_path),
                    "--time",
                    step_time.isoformat(),
                    "--output-dir",
                    str(step_dir),
                    "--lon-file",
                    str(lon_file),
                    "--lat-file",
                    str(lat_file),
                    "--download-dir",
                    str(download_dir),
                ],
                check=True,
                env=helper_env,
            )
        except Exception:
            shutil.rmtree(step_dir, ignore_errors=True)
            raise
        staged_steps.append(step_index)

    _copy_stage_metadata(stage_dir, config_path, len(staged_steps))
    print(f"Staged {len(staged_steps)} EarthAccess step(s) in {stage_dir}")
    print(f"Compute jobs should export CECE_EARTHACCESS_STAGE_DIR={stage_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
