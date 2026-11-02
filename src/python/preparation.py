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
    """Explicitly regrid a two-dimensional field using the AXIS package.

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
        If the optional AXIS Python package is unavailable.

    Notes
    -----
    This function does not perform vertical interpolation, mask processing,
    satellite swath regridding, or conservative remapping. It creates a new
    weight plan for each call and does not run implicitly in ``Model.step``.
    """
    import numpy as np
    import xarray as xr
    from .grid import Grid

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
    values = np.asarray(source.values, dtype=float)
    if not np.all(np.isfinite(values)):
        raise ValueError('Masked or non-finite source values are not supported')
    if method == 'passthrough':
        source_lon, source_lat, target_lon, target_lat = coordinates
        if (source_lon.shape != target_lon.shape or source_lat.shape != target_lat.shape or
                not np.allclose((source_lon - target_lon + 180) % 360 - 180, 0, rtol=0, atol=1e-10) or
                not np.allclose(source_lat, target_lat, rtol=0, atol=1e-10)):
            raise ValueError('Passthrough requires identical source and target coordinates')
        result = values.copy()
    else:
        try:
            import axis
        except ImportError as exc:
            raise ImportError('Regridding requires the AXIS Python package from HELM') from exc
        target = xr.Dataset(coords=target_grid.coordinates())
        regridder = axis.Regridder(source.to_dataset(name='_cece_field'), target,
                                   method='nearest', unmapped='error', norm_type='dstarea')
        try:
            result = np.asarray(regridder(source).values).copy()
        finally:
            regridder.close()
    return xr.DataArray(result.reshape(target_grid.shape), dims=target_grid.dims,
                        coords=target_grid.coordinates(), name=field.name, attrs=dict(field.attrs))



def forecast_valid_time(dataset, *, lead='step'):
    """Convert explicitly identified forecast reference times to valid times.

    Parameters
    ----------
    dataset : xarray.Dataset
        Dataset with a one-dimensional datetime reference coordinate and a
        scalar timedelta lead coordinate.
    lead : str, optional
        Name of the scalar forecast lead coordinate.

    Returns
    -------
    xarray.Dataset
        Dataset indexed by valid time, with the original reference times saved
        in the ``forecast_reference_time`` coordinate.

    Raises
    ------
    ValueError
        If the coordinates are ambiguous, missing, or invalid.

    Notes
    -----
    Call this only when the named reference coordinate is known to represent
    forecast initialization time. Select ensemble and lead dimensions first.
    """
    import numpy as np

    reference = 'time'
    if (reference not in dataset.coords or
            dataset[reference].dims != (reference,) or lead not in dataset.coords or
            dataset[lead].ndim != 0 or 'forecast_reference_time' in dataset.variables or
            'valid_time' in dataset.variables):
        raise ValueError('Expected an unambiguous time reference axis and scalar lead')
    times = np.asarray(dataset[reference].values)
    offset = np.asarray(dataset[lead].values)
    if (not np.issubdtype(times.dtype, np.datetime64) or
            not np.issubdtype(offset.dtype, np.timedelta64) or
            np.any(np.isnat(times)) or np.isnat(offset)):
        raise ValueError('Forecast reference times and lead must be finite datetime and timedelta values')
    valid = times + offset
    if np.any(np.isnat(valid)) or len(np.unique(valid)) != len(valid):
        raise ValueError('Forecast valid times must be finite and unique')
    result = dataset.drop_vars(lead).assign_coords(forecast_reference_time=(reference, times))
    return result.assign_coords({reference: valid})
