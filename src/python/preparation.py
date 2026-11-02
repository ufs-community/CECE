"""Provider-independent temporal selection for labeled data."""
from __future__ import annotations


def select_time(dataset, time, *, tolerance):
    """Select the nearest time within an explicit tolerance; leave static data alone.

    Forecast cycle, lead time, and ensemble selection must be resolved by the
    caller before this operation. Calendar and nonstandard time axes are not
    silently coerced.
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
