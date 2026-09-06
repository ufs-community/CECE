#!/usr/bin/env python3
"""Compare three CECE MEGAN isoprene diagnostics on one identical grid.

Despite the historical filename, this script does not execute HEMCO and does
not establish HEMCO-versus-CECE runtime parity. The source-reference input is
the CECE ``megan_method: hemco_3_12_1`` stateless, source-derived calculation.
It is compared diagnostically with CECE native MEGAN and CECE MEGAN3.

All files must contain identically ordered longitude and latitude coordinates,
the same selected time/level, and an isoprene mass flux in kg m-2 s-1. The
script never reorders, regrids, or silently averages records.
"""

import argparse
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import matplotlib

matplotlib.use("Agg")
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
import netCDF4 as nc
import numpy as np

PLOT_UNITS = "mg C m-2 hr-1"
ISOPRENE_CARBON_FRACTION = 5.0 * 12.011 / 68.12


class ComparisonError(RuntimeError):
    """Raised when inputs cannot support an exact diagnostic comparison."""


@dataclass(frozen=True)
class Field:
    role: str
    path: Path
    variable: str
    data: np.ma.MaskedArray
    longitude: np.ndarray
    longitude_units: str
    latitude: np.ndarray
    latitude_units: str
    time_value: Optional[float]
    time_units: Optional[str]
    time_calendar: Optional[str]
    level_value: Optional[float]
    level_units: Optional[str]
    declared_units: str


def _text_attr(variable, name: str) -> Optional[str]:
    value = getattr(variable, name, None)
    return str(value).strip() if value is not None else None


def _compact_units(units: str) -> str:
    compact = units.lower().replace("−", "-").replace("²", "2")
    for token in (" ", "_", "^", "**", "(", ")"):
        compact = compact.replace(token, "")
    return compact


def _validate_flux_units(variable, path: Path) -> str:
    units = _text_attr(variable, "units")
    if units is None:
        raise ComparisonError(f"{path}: {variable.name!r} has no units attribute")
    accepted = {"kgm-2s-1", "kg/m2/s", "kgm-2/s", "kg/m2s"}
    if _compact_units(units) not in accepted:
        raise ComparisonError(
            f"{path}: {variable.name!r} units are {units!r}; expected isoprene mass flux in kg m-2 s-1"
        )
    return units


def _axis_kind(name: str, coordinate) -> Optional[str]:
    lowered = name.lower()
    standard_name = (_text_attr(coordinate, "standard_name") or "").lower()
    axis = (_text_attr(coordinate, "axis") or "").upper()
    units = (_text_attr(coordinate, "units") or "").lower()
    if (
        standard_name == "longitude"
        or axis == "X"
        or "degrees_east" in units
        or lowered in {"lon", "longitude"}
    ):
        return "longitude"
    if (
        standard_name == "latitude"
        or axis == "Y"
        or "degrees_north" in units
        or lowered in {"lat", "latitude"}
    ):
        return "latitude"
    if standard_name == "time" or axis == "T" or lowered == "time":
        return "time"
    if axis == "Z" or lowered in {"lev", "level", "levels", "z"}:
        return "level"
    return None


def _coordinate(ds, dimension: str, path: Path):
    if dimension not in ds.variables:
        raise ComparisonError(
            f"{path}: dimension {dimension!r} has no same-named coordinate variable"
        )
    variable = ds.variables[dimension]
    if tuple(variable.dimensions) != (dimension,):
        raise ComparisonError(
            f"{path}: coordinate {dimension!r} is not one-dimensional on its own dimension"
        )
    values = np.ma.asarray(variable[:], dtype=np.float64)
    if np.any(np.ma.getmaskarray(values)) or not np.all(np.isfinite(values.data)):
        raise ComparisonError(
            f"{path}: coordinate {dimension!r} contains missing or non-finite values"
        )
    return variable, np.asarray(values.data, dtype=np.float64)


def _select_index(
    kind: str, size: int, requested: Optional[int], path: Path, variable: str
) -> int:
    if size == 1 and requested is None:
        return 0
    if requested is None:
        raise ComparisonError(
            f"{path}: {variable!r} has {size} {kind} records; pass --{kind}-index explicitly (no averaging is done)"
        )
    index = requested if requested >= 0 else size + requested
    if index < 0 or index >= size:
        raise ComparisonError(
            f"{path}: --{kind}-index {requested} is outside 0..{size - 1}"
        )
    return index


def read_field(
    role: str,
    path: Path,
    variable_name: str,
    time_index: Optional[int],
    level_index: Optional[int],
) -> Field:
    if not path.is_file():
        raise ComparisonError(f"{role}: file does not exist: {path}")
    with nc.Dataset(path) as ds:
        if variable_name not in ds.variables:
            raise ComparisonError(
                f"{path}: variable {variable_name!r} is absent; available variables: {', '.join(ds.variables)}"
            )
        variable = ds.variables[variable_name]
        units = _validate_flux_units(variable, path)
        dimensions = list(variable.dimensions)
        data = np.ma.asarray(variable[:], dtype=np.float64)

        axes = {}
        coordinates = {}
        for dimension in dimensions:
            coordinate, values = _coordinate(ds, dimension, path)
            kind = _axis_kind(dimension, coordinate)
            if kind is None:
                raise ComparisonError(
                    f"{path}: cannot classify dimension {dimension!r} of {variable_name!r}"
                )
            if kind in axes:
                raise ComparisonError(
                    f"{path}: multiple {kind} dimensions occur in {variable_name!r}"
                )
            axes[kind] = dimension
            coordinates[kind] = (coordinate, values)

        for required in ("longitude", "latitude"):
            if required not in axes:
                raise ComparisonError(
                    f"{path}: {variable_name!r} has no identifiable {required} dimension"
                )

        selected = {"time": (None, None), "level": (None, None)}
        for kind, requested in (("time", time_index), ("level", level_index)):
            if kind not in axes:
                if requested is not None:
                    raise ComparisonError(
                        f"{path}: --{kind}-index was supplied but {variable_name!r} has no {kind} dimension"
                    )
                continue
            dimension = axes[kind]
            coordinate, values = coordinates[kind]
            index = _select_index(kind, values.size, requested, path, variable_name)
            axis = dimensions.index(dimension)
            data = np.take(data, index, axis=axis)
            dimensions.pop(axis)
            selected[kind] = (float(values[index]), _text_attr(coordinate, "units"))

        if len(dimensions) != 2 or set(dimensions) != {
            axes["latitude"],
            axes["longitude"],
        }:
            raise ComparisonError(
                f"{path}: after time/level selection, dimensions are {dimensions!r}; expected only latitude and longitude"
            )
        if dimensions != [axes["latitude"], axes["longitude"]]:
            data = np.ma.transpose(
                data,
                (
                    dimensions.index(axes["latitude"]),
                    dimensions.index(axes["longitude"]),
                ),
            )

        longitude = coordinates["longitude"][1]
        latitude = coordinates["latitude"][1]
        expected_shape = (latitude.size, longitude.size)
        if data.shape != expected_shape:
            raise ComparisonError(
                f"{path}: field shape {data.shape} does not match coordinates {expected_shape}"
            )
        valid = np.asarray(data.compressed(), dtype=np.float64)
        if valid.size and not np.all(np.isfinite(valid)):
            raise ComparisonError(
                f"{path}: {variable_name!r} contains non-finite unmasked values"
            )
        if valid.size and np.any(valid < 0.0):
            raise ComparisonError(
                f"{path}: {variable_name!r} contains negative emission values"
            )

        longitude_units = _text_attr(coordinates["longitude"][0], "units")
        latitude_units = _text_attr(coordinates["latitude"][0], "units")
        if longitude_units is None or latitude_units is None:
            raise ComparisonError(
                f"{path}: longitude and latitude coordinates must declare units"
            )
        time_calendar = None
        if "time" in coordinates:
            time_calendar = _text_attr(coordinates["time"][0], "calendar") or "standard"

    return Field(
        role=role,
        path=path,
        variable=variable_name,
        data=data,
        longitude=longitude,
        longitude_units=longitude_units,
        latitude=latitude,
        latitude_units=latitude_units,
        time_value=selected["time"][0],
        time_units=selected["time"][1],
        time_calendar=time_calendar,
        level_value=selected["level"][0],
        level_units=selected["level"][1],
        declared_units=units,
    )


def _assert_same_grid(reference: Field, diagnostic: Field) -> None:
    for name in ("longitude", "latitude"):
        left = getattr(reference, name)
        right = getattr(diagnostic, name)
        if not np.array_equal(left, right):
            mismatch = (
                np.flatnonzero(left != right)
                if left.shape == right.shape
                else np.array([], dtype=int)
            )
            detail = (
                f"; first differing index {int(mismatch[0])}" if mismatch.size else ""
            )
            raise ComparisonError(
                f"{diagnostic.role}: ordered {name} coordinates differ from the source reference{detail}; no reordering is allowed"
            )
        if getattr(reference, f"{name}_units") != getattr(diagnostic, f"{name}_units"):
            raise ComparisonError(
                f"{diagnostic.role}: {name} coordinate units differ from the source reference"
            )
    for kind in ("time", "level"):
        if getattr(reference, f"{kind}_value") != getattr(diagnostic, f"{kind}_value"):
            raise ComparisonError(
                f"{diagnostic.role}: selected {kind} differs from the source reference"
            )
        if getattr(reference, f"{kind}_units") != getattr(diagnostic, f"{kind}_units"):
            raise ComparisonError(
                f"{diagnostic.role}: selected {kind} units differ from the source reference"
            )
    if reference.time_calendar != diagnostic.time_calendar:
        raise ComparisonError(
            f"{diagnostic.role}: selected time calendar differs from the source reference"
        )
    if not np.array_equal(
        np.ma.getmaskarray(reference.data), np.ma.getmaskarray(diagnostic.data)
    ):
        raise ComparisonError(
            f"{diagnostic.role}: missing-value mask differs from the source reference"
        )


def to_mg_carbon(field: Field) -> np.ma.MaskedArray:
    return field.data * 1.0e6 * 3600.0 * ISOPRENE_CARBON_FRACTION


def metrics(reference: np.ndarray, diagnostic: np.ndarray) -> dict:
    valid = np.isfinite(reference) & np.isfinite(diagnostic)
    ref = reference[valid]
    test = diagnostic[valid]
    if not ref.size:
        raise ComparisonError("no valid cells remain for statistics")
    difference = test - ref
    positive = ref > 0.0
    relative = (
        np.abs(difference[positive] / ref[positive])
        if np.any(positive)
        else np.array([])
    )
    correlation = (
        float(np.corrcoef(ref, test)[0, 1])
        if ref.size > 1 and np.std(ref) and np.std(test)
        else math.nan
    )
    return {
        "cells": int(ref.size),
        "mean_reference": float(np.mean(ref)),
        "mean_diagnostic": float(np.mean(test)),
        "bias": float(np.mean(difference)),
        "rmse": float(np.sqrt(np.mean(difference**2))),
        "mean_absolute_relative_difference_percent": float(np.mean(relative) * 100.0)
        if relative.size
        else math.nan,
        "correlation": correlation,
        "zero_mask_mismatches": int(np.count_nonzero((ref == 0.0) != (test == 0.0))),
    }


def _map(ax, data, longitude, latitude, title, limit, *, difference=False) -> None:
    if difference:
        image = ax.pcolormesh(
            longitude,
            latitude,
            data,
            shading="auto",
            cmap="RdBu_r",
            vmin=-limit,
            vmax=limit,
        )
    else:
        image = ax.pcolormesh(
            longitude,
            latitude,
            data,
            shading="auto",
            cmap="YlOrRd",
            vmin=0.0,
            vmax=limit,
        )
    ax.set(title=title, xlabel="Longitude [degrees]", ylabel="Latitude [degrees]")
    ax.figure.colorbar(
        image, ax=ax, orientation="horizontal", pad=0.14, label=PLOT_UNITS
    )


def create_plots(
    reference: Field, native: Field, megan3: Field, output_dir: Path
) -> None:
    fields = [to_mg_carbon(field) for field in (reference, native, megan3)]
    names = ["CECE HEMCO-source reference", "CECE native MEGAN", "CECE MEGAN3 ISOP"]
    absolute_values = np.ma.concatenate(
        [field.ravel() for field in fields]
    ).compressed()
    if not absolute_values.size:
        raise ComparisonError("no valid cells remain for absolute maps")
    absolute_limit = float(np.percentile(absolute_values, 99.0))
    absolute_limit = absolute_limit if absolute_limit > 0.0 else 1.0

    fig, axes = plt.subplots(1, 3, figsize=(18, 5), constrained_layout=True)
    for ax, data, title in zip(axes, fields, names):
        _map(ax, data, reference.longitude, reference.latitude, title, absolute_limit)
    fig.suptitle("CECE isoprene diagnostic comparison (not HEMCO runtime parity)")
    fig.savefig(output_dir / "global_isop_diagnostic_maps.png", dpi=180)
    plt.close(fig)

    differences = [data - fields[0] for data in fields[1:]]
    difference_values = np.ma.concatenate(
        [np.ma.abs(data).ravel() for data in differences]
    ).compressed()
    difference_limit = (
        float(np.percentile(difference_values, 99.0)) if difference_values.size else 0.0
    )
    difference_limit = difference_limit if difference_limit > 0.0 else 1.0
    fig, axes = plt.subplots(1, 2, figsize=(13, 5), constrained_layout=True)
    for ax, data, title in zip(axes, differences, names[1:]):
        _map(
            ax,
            data,
            reference.longitude,
            reference.latitude,
            f"{title} minus source reference",
            difference_limit,
            difference=True,
        )
    fig.savefig(output_dir / "global_isop_diagnostic_differences.png", dpi=180)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 5), constrained_layout=True)
    for data, title in zip(fields, names):
        ax.plot(np.ma.mean(data, axis=1), reference.latitude, label=title)
    ax.set(
        title="Zonal-mean isoprene diagnostic",
        xlabel=PLOT_UNITS,
        ylabel="Latitude [degrees]",
    )
    ax.grid(alpha=0.3)
    ax.legend()
    fig.savefig(output_dir / "isop_diagnostic_zonal_mean.png", dpi=180)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(12, 5), constrained_layout=True)
    baseline = fields[0].filled(np.nan)
    for ax, data, title in zip(axes, fields[1:], names[1:]):
        diagnostic = data.filled(np.nan)
        valid = np.isfinite(baseline) & np.isfinite(diagnostic) & (baseline > 0.0)
        x, y = baseline[valid], diagnostic[valid]
        if not x.size:
            raise ComparisonError(
                f"{title}: no positive source-reference cells for scatter plot"
            )
        histogram = ax.hist2d(x, y, bins=60, norm=mcolors.LogNorm(), cmap="viridis")
        limit = float(max(np.max(x), np.max(y)) * 1.05)
        ax.plot([0.0, limit], [0.0, limit], "k--", linewidth=1, label="1:1")
        ax.set(
            title=title,
            xlabel=f"CECE HEMCO-source reference [{PLOT_UNITS}]",
            ylabel=f"Diagnostic [{PLOT_UNITS}]",
        )
        ax.legend()
        fig.colorbar(histogram[3], ax=ax, label="Cell count")
    fig.savefig(output_dir / "isop_diagnostic_scatter.png", dpi=180)
    plt.close(fig)


def write_report(reference: Field, diagnostics: list, output_dir: Path) -> None:
    reference_values = to_mg_carbon(reference).filled(np.nan)
    lines = [
        "CECE MEGAN isoprene diagnostic comparison",
        "===========================================",
        "Baseline: CECE hemco_3_12_1 stateless source-derived output",
        "Claim: diagnostic source comparison only; not executed HEMCO runtime parity",
        "Grid handling: exact ordered-coordinate equality; no sorting or regridding",
        f"Selected time: {reference.time_value!r} {reference.time_units or ''}".rstrip(),
        f"Selected level: {reference.level_value!r} {reference.level_units or ''}".rstrip(),
        f"Shape: {reference.data.shape[0]} latitude x {reference.data.shape[1]} longitude",
        f"Plot/statistics units: {PLOT_UNITS}",
        "",
    ]
    for field in diagnostics:
        result = metrics(reference_values, to_mg_carbon(field).filled(np.nan))
        lines.extend(
            [f"{field.role} ({field.variable}; declared {field.declared_units})"]
        )
        lines.extend(f"  {key}: {value}" for key, value in result.items())
        lines.append("")
    (output_dir / "diagnostic_summary.txt").write_text(
        "\n".join(lines), encoding="utf-8"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-reference",
        "--hemco",
        dest="source_reference",
        type=Path,
        required=True,
    )
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--megan3", type=Path, required=True)
    parser.add_argument(
        "--source-reference-var",
        "--hemco-var",
        "--hemco_var",
        dest="source_reference_var",
        default="isoprene_hemco_source_reference",
    )
    parser.add_argument(
        "--native-var", "--native_var", dest="native_var", default="isoprene_native"
    )
    parser.add_argument(
        "--megan3-var", "--megan3_var", dest="megan3_var", default="MEGAN_ISOP"
    )
    parser.add_argument(
        "--time-index",
        "--time_index",
        dest="time_index",
        type=int,
        help="record index; required when a field has multiple time records",
    )
    parser.add_argument(
        "--level-index",
        "--level_index",
        dest="level_index",
        type=int,
        help="level index; required when a field has multiple levels",
    )
    parser.add_argument("--outdir", type=Path, default=Path("plots"))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    fields = [
        read_field(
            "source reference",
            args.source_reference,
            args.source_reference_var,
            args.time_index,
            args.level_index,
        ),
        read_field(
            "CECE native MEGAN",
            args.native,
            args.native_var,
            args.time_index,
            args.level_index,
        ),
        read_field(
            "CECE MEGAN3",
            args.megan3,
            args.megan3_var,
            args.time_index,
            args.level_index,
        ),
    ]
    for field in fields[1:]:
        _assert_same_grid(fields[0], field)

    args.outdir.mkdir(parents=True, exist_ok=False)
    create_plots(*fields, args.outdir)
    write_report(fields[0], fields[1:], args.outdir)
    print(f"PASS: diagnostic comparison written to {args.outdir}")
    print("SCOPE=CECE source-reference comparison; not HEMCO runtime parity")


if __name__ == "__main__":
    try:
        main()
    except ComparisonError as error:
        raise SystemExit(f"ERROR: {error}") from error
