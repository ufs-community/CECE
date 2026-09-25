#!/usr/bin/env python3
"""Prepare global annual pollen RF inputs from MERRA-2 and pollen observations."""

from __future__ import annotations

import argparse
import glob
import math
import os
import time
from datetime import UTC, datetime, timedelta
from pathlib import Path

import numpy as np
import pandas as pd
import requests
import xarray as xr


SURFACE_COLLECTION = "M2T1NXSLV"
FLUX_COLLECTION = "M2T1NXFLX"
RADIATION_COLLECTION = "M2T1NXRAD"
CONSTANT_COLLECTION = "M2C0NXASM"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    download = subparsers.add_parser(
        "download-merra2", help="Download one year from NASA Earthdata"
    )
    download.add_argument("--year", type=int, default=2025)
    download.add_argument(
        "--output-dir", type=Path, default=Path("data/pollen/merra2/2025")
    )

    ambee = subparsers.add_parser(
        "download-ambee", help="Download historical pollen at configured training sites"
    )
    ambee.add_argument(
        "sites_csv", type=Path, help="CSV with site_id, latitude, and longitude"
    )
    ambee.add_argument("output_csv", type=Path)
    ambee.add_argument("--year", type=int, default=2025)
    ambee.add_argument("--taxon", required=True)
    ambee.add_argument(
        "--records-path", default="data", help="Dot path to the response record list"
    )
    ambee.add_argument(
        "--timestamp-path", default="updatedAt", help="Dot path within each record"
    )
    ambee.add_argument(
        "--count-path",
        required=True,
        help=(
            "Dot path within each record to the numeric pollen count. "
            "Join multiple dot paths with '+' to sum group counts into an "
            "aggregate (e.g. 'Count.grass_pollen+Count.tree_pollen+Count.weed_pollen' "
            "for pollen_total)"
        ),
    )
    ambee.add_argument("--chunk-days", type=int, default=7)
    ambee.add_argument("--request-delay-seconds", type=float, default=0.25)

    aggregate = subparsers.add_parser(
        "aggregate-merra2", help="Aggregate downloaded hourly MERRA-2 files"
    )
    aggregate.add_argument("--year", type=int, default=2025)
    aggregate.add_argument(
        "--surface-glob", default="data/pollen/merra2/2025/MERRA2_*tavg1_2d_slv_Nx*.nc4"
    )
    aggregate.add_argument(
        "--flux-glob", default="data/pollen/merra2/2025/MERRA2_*tavg1_2d_flx_Nx*.nc4"
    )
    aggregate.add_argument(
        "--radiation-glob",
        default="data/pollen/merra2/2025/MERRA2_*tavg1_2d_rad_Nx*.nc4",
    )
    aggregate.add_argument(
        "--constant-glob",
        default="data/pollen/merra2/2025/MERRA2_*const_2d_asm_Nx*.nc4",
    )
    aggregate.add_argument(
        "--output",
        type=Path,
        default=Path("data/pollen/merra2_annual_predictors_2025.nc"),
    )
    aggregate.add_argument("--sunshine-threshold-wm2", type=float, default=120.0)

    training = subparsers.add_parser(
        "prepare-training", help="Join pollen observations to annual predictors"
    )
    training.add_argument(
        "pollen_csv", type=Path, help="Authorized historical pollen export"
    )
    training.add_argument("predictor_netcdf", type=Path)
    training.add_argument("output_csv", type=Path)
    training.add_argument("--year", type=int, default=2025)
    training.add_argument("--taxon", required=True)
    training.add_argument("--latitude-column", default="latitude")
    training.add_argument("--longitude-column", default="longitude")
    training.add_argument("--time-column", default="timestamp")
    training.add_argument("--taxon-column", default="taxon")
    training.add_argument("--count-column", default="pollen_count")
    training.add_argument(
        "--concentration-to-production",
        type=float,
        required=True,
        help="Calibrated conversion from annual concentration-days to grains m-2 yr-1",
    )
    training.add_argument("--minimum-coverage-fraction", type=float, default=0.75)
    training.add_argument("--site-id-column", default="site_id")
    return parser.parse_args()


def download_merra2(year: int, output_dir: Path) -> None:
    try:
        import earthaccess
    except ImportError as exc:
        raise RuntimeError(
            "Install CECE's pollen dependencies: python -m pip install -e '.[pollen]'"
        ) from exc

    output_dir.mkdir(parents=True, exist_ok=True)
    earthaccess.login()
    temporal = (f"{year}-01-01", f"{year}-12-31T23:59:59")
    for short_name in (SURFACE_COLLECTION, FLUX_COLLECTION, RADIATION_COLLECTION):
        results = earthaccess.search_data(short_name=short_name, temporal=temporal)
        if not results:
            raise RuntimeError(
                f"NASA Earthdata returned no {short_name} granules for {year}"
            )
        earthaccess.download(results, str(output_dir))
    constants = earthaccess.search_data(short_name=CONSTANT_COLLECTION)
    if not constants:
        raise RuntimeError(f"NASA Earthdata returned no {CONSTANT_COLLECTION} granule")
    earthaccess.download(constants, str(output_dir))


def nested_value(value: object, path: str) -> object:
    current = value
    for key in (part for part in path.split(".") if part):
        if not isinstance(current, dict) or key not in current:
            raise KeyError(f"JSON path '{path}' is missing component '{key}'")
        current = current[key]
    return current


def nested_count(record: object, count_path: str) -> float:
    """Resolve --count-path, summing '+'-joined dot paths for aggregate taxa (e.g. total pollen)."""
    return sum(float(nested_value(record, part)) for part in count_path.split("+"))


def download_ambee(args: argparse.Namespace) -> None:
    api_key = os.environ.get("AMBEE_API_KEY")
    if not api_key:
        raise RuntimeError(
            "Set AMBEE_API_KEY in the shell; never pass API keys as command arguments"
        )
    if args.chunk_days <= 0 or args.request_delay_seconds < 0.0:
        raise ValueError(
            "Ambee chunk days must be positive and request delay must be non-negative"
        )
    sites = pd.read_csv(args.sites_csv)
    required = {"site_id", "latitude", "longitude"}
    missing = sorted(required - set(sites.columns))
    if missing:
        raise KeyError(f"Site CSV is missing columns: {', '.join(missing)}")

    session = requests.Session()
    session.headers.update({"x-api-key": api_key, "Content-type": "application/json"})
    start = datetime(args.year, 1, 1, tzinfo=UTC)
    stop = datetime(args.year + 1, 1, 1, tzinfo=UTC)
    rows = []
    for site in sites.itertuples(index=False):
        chunk_start = start
        while chunk_start < stop:
            chunk_stop = min(chunk_start + timedelta(days=args.chunk_days), stop)
            response = session.get(
                "https://api.ambeedata.com/v3/pollen/history",
                params={
                    "lat": float(site.latitude),
                    "lng": float(site.longitude),
                    "from": chunk_start.strftime("%Y-%m-%d %H:%M:%S"),
                    "to": chunk_stop.strftime("%Y-%m-%d %H:%M:%S"),
                    "speciesRisk": "true",
                },
                timeout=60,
            )
            response.raise_for_status()
            payload = response.json()
            records = nested_value(payload, args.records_path)
            if not isinstance(records, list):
                raise TypeError(
                    f"Ambee --records-path '{args.records_path}' must resolve to a list"
                )
            for record in records:
                rows.append(
                    {
                        "site_id": site.site_id,
                        "latitude": float(site.latitude),
                        "longitude": float(site.longitude),
                        "timestamp": nested_value(record, args.timestamp_path),
                        "taxon": args.taxon,
                        "pollen_count": nested_count(record, args.count_path),
                    }
                )
            chunk_start = chunk_stop
            if args.request_delay_seconds:
                time.sleep(args.request_delay_seconds)

    output = pd.DataFrame(rows).drop_duplicates(
        subset=["site_id", "timestamp", "taxon"]
    )
    if output.empty:
        raise RuntimeError("Ambee returned no records for the requested sites and year")
    args.output_csv.parent.mkdir(parents=True, exist_ok=True)
    output.to_csv(args.output_csv, index=False)


def time_step_seconds(dataset: xr.Dataset) -> float:
    if dataset.sizes.get("time", 0) < 2:
        return 3600.0
    delta = (dataset.time.values[1] - dataset.time.values[0]) / np.timedelta64(1, "s")
    return float(delta)


def relative_humidity_percent(
    temperature: xr.DataArray, specific_humidity: xr.DataArray, pressure: xr.DataArray
) -> xr.DataArray:
    temperature_c = temperature - 273.15
    vapor_pressure = specific_humidity * pressure / (0.622 + 0.378 * specific_humidity)
    saturation_pressure = 611.2 * np.exp(
        17.67 * temperature_c / (temperature_c + 243.5)
    )
    return (100.0 * vapor_pressure / saturation_pressure).clip(0.0, 100.0)


def aggregate_merra2(
    surface_glob: str,
    flux_glob: str,
    radiation_glob: str,
    constant_glob: str,
    output: Path,
    year: int,
    sunshine_threshold: float,
) -> None:
    surface_files = sorted(Path(path) for path in glob.glob(surface_glob))
    flux_files = sorted(Path(path) for path in glob.glob(flux_glob))
    radiation_files = sorted(Path(path) for path in glob.glob(radiation_glob))
    constant_files = sorted(Path(path) for path in glob.glob(constant_glob))
    if not surface_files or not flux_files or not radiation_files or not constant_files:
        raise FileNotFoundError(
            "MERRA-2 surface, flux, radiation, and constant files are required; run download-merra2 first"
        )

    surface = xr.open_mfdataset(
        surface_files, combine="by_coords", chunks={"time": 24 * 7}
    )
    flux = xr.open_mfdataset(flux_files, combine="by_coords", chunks={"time": 24 * 7})
    radiation = xr.open_mfdataset(
        radiation_files, combine="by_coords", chunks={"time": 24 * 7}
    )
    constants = xr.open_dataset(constant_files[0])
    required_surface = {"T2M", "QV2M", "PS", "U10M", "V10M"}
    missing = sorted(required_surface - set(surface.variables))
    other_missing = (
        ([] if "PRECTOTCORR" in flux else ["PRECTOTCORR"])
        + ([] if "SWGDN" in radiation else ["SWGDN"])
        + ([] if "PHIS" in constants else ["PHIS"])
    )
    if missing or other_missing:
        raise KeyError(
            f"Missing MERRA-2 variables: {', '.join(missing + other_missing)}"
        )

    seconds = time_step_seconds(surface)
    rh = relative_humidity_percent(surface.T2M, surface.QV2M, surface.PS)
    predictors = xr.Dataset(
        {
            "temperature_avg": surface.T2M.mean("time"),
            "temperature_max": surface.T2M.max("time"),
            "temperature_min": surface.T2M.min("time"),
            "wind_speed": np.hypot(surface.U10M, surface.V10M).mean("time"),
            "precipitation": (flux.PRECTOTCORR * seconds).sum("time"),
            "relative_humidity": rh.mean("time"),
            "sunshine_hours": (
                (radiation.SWGDN >= sunshine_threshold).sum("time")
                * time_step_seconds(radiation)
                / 3600.0
            ),
            "pressure": surface.PS.mean("time"),
            "altitude": constants.PHIS.squeeze(drop=True) / 9.80665,
        }
    ).compute()
    predictors = predictors.rename(
        {name: name.lower() for name in ("lat", "lon") if name in predictors.dims}
    )
    predictors.attrs.update(
        title=f"MERRA-2 annual pollen RF predictors for {year}",
        source=f"NASA MERRA-2 {SURFACE_COLLECTION}, {RADIATION_COLLECTION}, and {CONSTANT_COLLECTION}",
        year=year,
    )
    predictors.temperature_avg.attrs["units"] = "K"
    predictors.temperature_max.attrs["units"] = "K"
    predictors.temperature_min.attrs["units"] = "K"
    predictors.wind_speed.attrs["units"] = "m s-1"
    predictors.precipitation.attrs["units"] = "mm yr-1"
    predictors.relative_humidity.attrs["units"] = "%"
    predictors.sunshine_hours.attrs["units"] = "h yr-1"
    predictors.pressure.attrs["units"] = "Pa"
    predictors.altitude.attrs["units"] = "m"
    output.parent.mkdir(parents=True, exist_ok=True)
    predictors.to_netcdf(output)


def prepare_training(args: argparse.Namespace) -> None:
    if (
        not math.isfinite(args.concentration_to_production)
        or args.concentration_to_production <= 0.0
    ):
        raise ValueError(
            "--concentration-to-production must be a positive calibrated value"
        )
    if not 0.0 < args.minimum_coverage_fraction <= 1.0:
        raise ValueError("--minimum-coverage-fraction must be in (0, 1]")
    observations = pd.read_csv(args.pollen_csv)
    required = {
        args.latitude_column,
        args.longitude_column,
        args.time_column,
        args.taxon_column,
        args.count_column,
    }
    missing = sorted(required - set(observations.columns))
    if missing:
        raise KeyError(f"Pollen CSV is missing columns: {', '.join(missing)}")

    observations[args.time_column] = pd.to_datetime(
        observations[args.time_column], utc=True
    )
    observations = observations[
        (observations[args.time_column].dt.year == args.year)
        & (
            observations[args.taxon_column].astype(str).str.casefold()
            == args.taxon.casefold()
        )
    ].copy()
    if observations.empty:
        raise ValueError(f"No {args.taxon} observations found for {args.year}")
    observations[args.count_column] = pd.to_numeric(
        observations[args.count_column], errors="coerce"
    )
    observations = observations.dropna(
        subset=[args.count_column, args.latitude_column, args.longitude_column]
    )

    if args.site_id_column not in observations:
        observations[args.site_id_column] = (
            observations[args.latitude_column].round(4).astype(str)
            + ":"
            + observations[args.longitude_column].round(4).astype(str)
        )
    year_hours = 8760.0 + (
        24.0 if pd.Timestamp(args.year, 12, 31).dayofyear == 366 else 0.0
    )
    summaries = []
    for site_id, group in observations.groupby(args.site_id_column):
        group = group.sort_values(args.time_column)
        intervals = group[args.time_column].diff().dt.total_seconds().dropna() / 3600.0
        intervals = intervals[intervals > 0.0]
        if intervals.empty:
            continue
        cadence_hours = float(intervals.median())
        coverage_fraction = min(1.0, len(group) * cadence_hours / year_hours)
        summaries.append(
            {
                args.site_id_column: site_id,
                "latitude": group[args.latitude_column].mean(),
                "longitude": group[args.longitude_column].mean(),
                "annual_concentration_days": group[args.count_column].sum()
                * cadence_hours
                / 24.0,
                "observation_count": len(group),
                "cadence_hours": cadence_hours,
                "coverage_fraction": coverage_fraction,
            }
        )
    annual = pd.DataFrame(summaries)
    if annual.empty:
        raise ValueError("Each pollen site needs at least two valid timestamps")
    annual = annual[annual.coverage_fraction >= args.minimum_coverage_fraction].copy()
    if annual.empty:
        raise ValueError("No pollen sites meet --minimum-coverage-fraction")
    annual["annual_pollen_production"] = (
        annual.annual_concentration_days * args.concentration_to_production
    )

    predictors = xr.open_dataset(args.predictor_netcdf)
    if "lat" not in predictors.coords or "lon" not in predictors.coords:
        raise KeyError("Predictor NetCDF must provide lat and lon coordinates")
    feature_names = list(DEFAULT_FEATURES)
    missing_features = sorted(set(feature_names) - set(predictors.variables))
    if missing_features:
        raise KeyError(f"Predictor NetCDF is missing: {', '.join(missing_features)}")
    sampled = predictors[feature_names].sel(
        lat=xr.DataArray(annual.latitude.to_numpy(), dims="sample"),
        lon=xr.DataArray(annual.longitude.to_numpy(), dims="sample"),
        method="nearest",
    )
    for feature in feature_names:
        annual[feature] = sampled[feature].to_numpy()
    annual["year"] = args.year
    annual["taxon"] = args.taxon
    annual["production_calibration"] = args.concentration_to_production
    args.output_csv.parent.mkdir(parents=True, exist_ok=True)
    annual.to_csv(args.output_csv, index=False)


DEFAULT_FEATURES = (
    "temperature_avg",
    "temperature_max",
    "temperature_min",
    "wind_speed",
    "precipitation",
    "relative_humidity",
    "sunshine_hours",
    "altitude",
    "pressure",
)


def main() -> None:
    args = parse_args()
    if args.command == "download-merra2":
        download_merra2(args.year, args.output_dir)
    elif args.command == "download-ambee":
        download_ambee(args)
    elif args.command == "aggregate-merra2":
        aggregate_merra2(
            args.surface_glob,
            args.flux_glob,
            args.radiation_glob,
            args.constant_glob,
            args.output,
            args.year,
            args.sunshine_threshold_wm2,
        )
    else:
        prepare_training(args)


if __name__ == "__main__":
    main()
