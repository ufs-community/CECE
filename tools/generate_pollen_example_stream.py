#!/usr/bin/env python3
"""Generate a synthetic CF-compliant pollen stream for the CECE example."""

from __future__ import annotations

import argparse
from pathlib import Path

import netCDF4
import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/pollen/pollen_rf_phenology_2020.nc"),
    )
    return parser.parse_args()


def _build_fields(nx: int, ny: int, nt: int):
    lon = np.linspace(115.05, 117.95, nx)
    lat = np.linspace(39.05, 41.45, ny)
    hours = np.arange(nt, dtype=np.float64)
    lon_grid, lat_grid = np.meshgrid(lon, lat)
    spatial_peak = np.exp(
        -(((lon_grid - 116.5) / 0.9) ** 2 + ((lat_grid - 40.2) / 0.7) ** 2)
    )

    c3_fraction = np.clip(0.12 + 0.45 * spatial_peak, 0.0, 1.0)
    grass_fraction = np.clip(c3_fraction + 0.18, 0.0, 1.0)
    artemisia_pannual = 0.8e6 + 0.8e6 * spatial_peak
    chenopod_pannual = 0.5e6 + 0.9e6 * (
        1.0 - (lat_grid - lat.min()) / (lat.max() - lat.min())
    )
    total_pannual = 6.5e6 + 2.5e6 * spatial_peak

    local_hour = hours[:, None, None] % 24.0
    diurnal = np.sin(2.0 * np.pi * (local_hour - 8.0) / 24.0)
    temperature = 293.15 + 7.0 * diurnal + np.zeros((nt, ny, nx))
    wind_speed = 3.5 + 1.5 * np.maximum(diurnal, 0.0) + np.zeros((nt, ny, nx))
    convective_velocity = 0.2 + 0.8 * np.maximum(diurnal, 0.0) + np.zeros((nt, ny, nx))
    relative_humidity = 68.0 - 18.0 * diurnal + np.zeros((nt, ny, nx))
    precipitation = np.zeros((nt, ny, nx))
    precipitation[15:18, :, :] = 0.6
    sunshine_hours = np.full((nt, ny, nx), 8.0)
    day_of_year = 238.0 + hours[:, None, None] / 24.0 + np.zeros((nt, ny, nx))

    def repeat(field: np.ndarray) -> np.ndarray:
        return np.broadcast_to(field, (nt, ny, nx))

    fields = {
        "artemisia_pannual": repeat(artemisia_pannual),
        "chenopod_pannual": repeat(chenopod_pannual),
        "total_pannual": repeat(total_pannual),
        "c3_fraction": repeat(c3_fraction),
        "grass_fraction": repeat(grass_fraction),
        "artemisia_sdoy": repeat(np.full((ny, nx), 222.0)),
        "artemisia_edoy": repeat(np.full((ny, nx), 268.0)),
        "chenopod_sdoy": repeat(np.full((ny, nx), 220.0)),
        "chenopod_edoy": repeat(np.full((ny, nx), 270.0)),
        "total_sdoy": repeat(np.full((ny, nx), 215.0)),
        "total_edoy": repeat(np.full((ny, nx), 280.0)),
        "day_of_year": day_of_year,
        "temperature_2m": temperature,
        "wind_speed_10m": wind_speed,
        "convective_velocity": convective_velocity,
        "precipitation_interval": precipitation,
        "relative_humidity_2m": relative_humidity,
        "sunshine_hours": sunshine_hours,
    }
    return lon, lat, hours, fields


def _write_dataset(path: Path, lon, lat, hours, fields) -> None:
    nt = len(hours)
    ny = len(lat)
    nx = len(lon)
    with netCDF4.Dataset(path, "w", format="NETCDF4_CLASSIC") as dataset:
        dataset.title = "Synthetic phenology and RF pollen example input"
        dataset.source = "CECE demonstration generator; not trained RF production data"
        dataset.Conventions = "CF-1.8"
        dataset.createDimension("time", nt)
        dataset.createDimension("lat", ny)
        dataset.createDimension("lon", nx)

        time_var = dataset.createVariable("time", "f8", ("time",))
        time_var.units = "hours since 2020-08-25 00:00:00"
        time_var.calendar = "gregorian"
        time_var.standard_name = "time"
        time_var[:] = hours

        lat_var = dataset.createVariable("lat", "f8", ("lat",))
        lat_var.units = "degrees_north"
        lat_var.standard_name = "latitude"
        lat_var[:] = lat

        lon_var = dataset.createVariable("lon", "f8", ("lon",))
        lon_var.units = "degrees_east"
        lon_var.standard_name = "longitude"
        lon_var[:] = lon

        def write(name: str, values: np.ndarray, units: str, long_name: str) -> None:
            variable = dataset.createVariable(
                name, "f8", ("time", "lat", "lon"), zlib=True
            )
            variable.units = units
            variable.long_name = long_name
            variable.coordinates = "lat lon"
            variable[:] = values

        metadata = {
            "artemisia_pannual": (
                "grains m-2 yr-1",
                "synthetic annual Artemisia pollen production",
            ),
            "chenopod_pannual": (
                "grains m-2 yr-1",
                "synthetic annual chenopod pollen production",
            ),
            "total_pannual": (
                "grains m-2 yr-1",
                "synthetic annual total pollen production",
            ),
            "c3_fraction": ("1", "C3 plant functional type fraction"),
            "grass_fraction": ("1", "C3 and C4 grass fraction"),
            "artemisia_sdoy": ("day_of_year", "Artemisia season start"),
            "artemisia_edoy": ("day_of_year", "Artemisia season end"),
            "chenopod_sdoy": ("day_of_year", "chenopod season start"),
            "chenopod_edoy": ("day_of_year", "chenopod season end"),
            "total_sdoy": ("day_of_year", "total pollen season start"),
            "total_edoy": ("day_of_year", "total pollen season end"),
            "day_of_year": ("day_of_year", "fractional day of year"),
            "temperature_2m": ("K", "2 m air temperature"),
            "wind_speed_10m": ("m s-1", "10 m wind speed"),
            "convective_velocity": ("m s-1", "convective velocity scale"),
            "precipitation_interval": (
                "mm",
                "precipitation accumulated over the one-hour interval",
            ),
            "relative_humidity_2m": ("%", "2 m relative humidity"),
            "sunshine_hours": ("h", "daily sunshine duration"),
        }
        for name, values in fields.items():
            units, long_name = metadata[name]
            write(name, values, units, long_name)


def main() -> None:
    args = parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lon, lat, hours, fields = _build_fields(nx=30, ny=25, nt=25)
    _write_dataset(args.output, lon, lat, hours, fields)

    print(args.output)


if __name__ == "__main__":
    main()
