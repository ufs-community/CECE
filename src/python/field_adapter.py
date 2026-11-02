"""Convert named scientific fields into the existing CECE column/axis storage."""

from __future__ import annotations

import numpy as np

_ALIASES = {
    "dimensionless": "1",
    "none": "1",
    "m/s": "m s-1",
    "W/m2": "W m-2",
    "kg/m2/s": "kg m-2 s-1",
}


def canonical_field(value, spec, grid):
    """Validate first; return an owned float64 (column, axis) Fortran array.

    NumPy inputs use canonical units. Labeled inputs must declare units and
    match coordinates; this function never silently regrids or interpolates.
    """
    if hasattr(value, "dims") and hasattr(value, "attrs"):
        units = value.attrs.get("units")
        if units is None:
            raise ValueError(
                f"{spec.name}: units are required for labeled inputs (expected {spec.units})"
            )
        if spec.units != "caller" and _ALIASES.get(units, units) != spec.units:
            raise ValueError(
                f"{spec.name}: incompatible units {units!r}; expected {spec.units!r}"
            )
        extra = () if spec.axis == "surface" else (spec.axis,)
        expected = grid.dims + extra
        # A singleton surface/vertical axis may be implicit, but never invent
        # vegetation/biome layers or reinterpret them as atmospheric levels.
        if (
            spec.extent == 1
            and spec.axis != "biome"
            and set(value.dims) == set(grid.dims)
        ):
            expected = grid.dims
        if set(value.dims) != set(expected) or len(value.dims) != len(expected):
            raise ValueError(
                f"{spec.name}: dimensions {value.dims} must identify {expected}"
            )
        for axis, coords in grid.coordinates().items():
            if axis not in value.coords:
                continue
            target = np.asarray(coords[1] if isinstance(coords, tuple) else coords)
            actual = np.asarray(value.coords[axis].values)
            if actual.shape != target.shape or not np.allclose(
                actual, target, rtol=0, atol=1e-10
            ):
                raise ValueError(
                    f"{spec.name}: {axis} coordinates differ; explicitly prepare/regrid inputs first"
                )
        if spec.axis == "level" and "level" in value.coords:
            if not np.array_equal(value.level.values, grid.levels):
                raise ValueError(
                    f"{spec.name}: vertical coordinates differ; explicit interpolation is required"
                )
        value = value.transpose(*expected).values
    array = np.asarray(value, dtype=np.float64)
    shape = grid.shape if spec.axis == "surface" else grid.shape + (spec.extent,)
    if array.ndim == 0:
        if spec.axis == "biome":
            raise ValueError(f"{spec.name}: explicit biome fractions are required")
        array = np.full(shape, array.item())
    allowed = {shape, (grid.ncol, spec.extent)}
    if spec.extent == 1:
        allowed |= {grid.shape, (grid.ncol,)}
    if array.shape not in allowed:
        raise ValueError(f"{spec.name}: dimensions {array.shape} do not match {shape}")
    if not np.all(np.isfinite(array)):
        raise ValueError(f"{spec.name}: non-finite values are not allowed")
    if spec.name in ("solar_cosine", "solar_zenith_cosine") and np.any(
        (array < 0) | (array > 1)
    ):
        raise ValueError(f"{spec.name}: solar cosine must be in [0, 1]")
    return np.array(
        array.reshape(grid.ncol, spec.extent), dtype=np.float64, order="F", copy=True
    )
