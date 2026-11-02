"""Provider-independent temporal selection for labeled data."""
from __future__ import annotations


def select_time(dataset, time, *, tolerance):
    """Select the nearest time within an explicit tolerance; leave static data alone.

    Forecast cycle, lead time, and ensemble selection must be resolved by the
    caller before this operation. Calendar and nonstandard time axes are not
    silently coerced.

    Parameters
    ----------
    dataset : xarray.Dataset
        Dataset with a selectable ``time`` coordinate, or a static dataset.
    time : datetime-like
        Requested time.
    tolerance : str, datetime.timedelta or pandas.Timedelta
        Maximum separation for nearest-time selection; required for timed data.

    Returns
    -------
    xarray.Dataset
        Selected dataset, or the original dataset if it has no time axis.

    Raises
    ------
    ValueError
        If the tolerance is invalid or no time falls within it.
    """
    if 'time' not in dataset.dims and 'time' not in dataset.coords:
        return dataset
    if tolerance is None:
        raise ValueError('An explicit nearest-time tolerance is required')
    import pandas as pd

    limit = pd.Timedelta(tolerance)
    if pd.isna(limit) or limit < pd.Timedelta(0):
        raise ValueError('Time tolerance must be finite and nonnegative')
    try:
        return dataset.sel(time=time, method='nearest', tolerance=limit)
    except KeyError as exc:
        raise ValueError(f'No time within tolerance {limit} of {time}') from exc


def regrid_rectilinear(field, target_grid, *, method):
    """Explicitly regrid a two-dimensional field using CECE's AXIS driver.

    Parameters
    ----------
    field : xarray.DataArray
        Finite values with one-dimensional ``latitude`` and ``longitude``
        coordinates in degrees. Each axis must be strictly monotonic.
    target_grid : cece.Grid
        Structured target grid with at least two points per axis.
    method : {'passthrough', 'nearest'}
        Passthrough requires identical coordinates; nearest uses AXIS weights.

    Returns
    -------
    xarray.DataArray
        An independent field on the target grid, retaining its name and attrs.

    Raises
    ------
    ValueError
        If geometry, method, values, or target coverage are unsupported.
    ImportError
        If the optional CECE driver regridding extension is unavailable.

    Notes
    -----
    This function does not perform vertical interpolation, mask processing,
    satellite swath regridding, or conservative remapping. It creates a new
    weight plan for each call and does not run implicitly in ``Model.step``.
    """
    import numpy as np
    import xarray as xr
    from .exceptions import CeceConfigError
    from .grid import Grid
    from . import _cece_core

    if not isinstance(field, xr.DataArray) or set(field.dims) != {'latitude', 'longitude'} or len(field.dims) != 2:
        raise ValueError('Field must have latitude and longitude dimensions')
    if not isinstance(target_grid, Grid) or target_grid.dims != ('latitude', 'longitude'):
        raise ValueError('Target grid must be structured latitude-longitude')
    if method not in ('passthrough', 'nearest'):
        raise ValueError('Unsupported regridding method; choose passthrough or nearest')
    if any(name not in field.coords or field.coords[name].dims != (name,) for name in ('latitude', 'longitude')):
        raise ValueError('Source requires one-dimensional latitude and longitude coordinates')
    source = field.transpose('latitude', 'longitude')
    coordinates = [np.asarray(source.longitude.values, dtype=float), np.asarray(source.latitude.values, dtype=float),
                   np.asarray(target_grid.longitude, dtype=float), np.asarray(target_grid.latitude, dtype=float)]
    for axis in coordinates:
        differences = np.diff(axis)
        if axis.size < 2 or not np.all(np.isfinite(axis)) or not (np.all(differences > 0) or np.all(differences < 0)):
            raise ValueError('Coordinate axes must be finite and strictly monotonic with at least two values')
    if any(np.any(np.abs(axis) > 90) for axis in (coordinates[1], coordinates[3])):
        raise ValueError('Latitude must be in [-90, 90] degrees')
    try:
        from . import _cece_regrid
    except ImportError as exc:
        raise ImportError('Regridding requires the CECE driver regridding extension') from exc
    _cece_core.initialize_kokkos()
    values = np.asarray(source.values, dtype=float)
    try:
        result = _cece_regrid.regrid_rectilinear(values.ravel(order='C').tolist(), *(axis.tolist() for axis in coordinates), method)
    except CeceConfigError as exc:
        raise ValueError(str(exc)) from exc
    return xr.DataArray(np.asarray(result).reshape(target_grid.shape), dims=target_grid.dims,
                        coords=target_grid.coordinates(), name=field.name, attrs=dict(field.attrs))
