"""Provider-neutral access to a caller-supplied labeled dataset."""
from __future__ import annotations
from ..preparation import select_time


class DatasetSource:
    """Read selected variables at a bounded time from an xarray dataset.

    A source owns its open dataset until close. Explicit selection is required
    for ensemble dimensions; unresolved dimensions are never averaged.

    Parameters
    ----------
    dataset : xarray.Dataset
        Open dataset whose lifecycle is transferred to this source.
    variables : sequence of str
        Variables to expose on reads.
    tolerance : str, datetime.timedelta or pandas.Timedelta
        Maximum distance for nearest-time selection of timed data.
    """
    def __init__(self, dataset, *, variables, tolerance):
        self._dataset = dataset
        self.variables = tuple(variables)
        if not self.variables:
            raise ValueError('At least one source variable is required')
        self.tolerance = tolerance
        self._closed = False

    def open(self):
        """Return this source if it has not been closed.

        Returns
        -------
        DatasetSource
            The open source.
        """
        if self._closed:
            raise RuntimeError('Source is closed')
        return self

    def read(self, time, selection=None):
        """Select variables and a bounded time from the dataset.

        Parameters
        ----------
        time : datetime-like
            Requested valid time.
        selection : mapping, optional
            Coordinate labels passed to xarray selection; ensemble and
            recognized forecast dimensions must be selected explicitly.

        Returns
        -------
        xarray.Dataset
            Selected fields, retaining their metadata.

        Raises
        ------
        ValueError
            If selection is ambiguous or no time meets the tolerance.
        """
        self.open()
        data = self._dataset[list(self.variables)]
        selection = dict(selection or {})
        if 'time' in selection:
            raise ValueError('Select time using the model time and tolerance')
        for dimension in ('member', 'ensemble', 'number', 'realization',
                          'forecast_reference_time', 'cycle', 'step', 'lead_time'):
            if dimension in data.dims and dimension not in selection:
                raise ValueError(f'Explicit {dimension} selection is required')
        if selection:
            data = data.sel(selection)
        return select_time(data, time, tolerance=self.tolerance)

    def close(self):
        """Close the owned dataset; repeated calls have no effect."""
        if not self._closed:
            self._dataset.close()
            self._closed = True

    def __enter__(self):
        return self.open()

    def __exit__(self, *args):
        self.close()
