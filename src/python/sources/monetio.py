"""MONETIO v1 source; imports the optional dependency only on first use."""
from __future__ import annotations
from .base import DatasetSource


class MonetioSource:
    """Load a labeled source through the optional MONETIO v1 reader API.

    Parameters
    ----------
    source : str
        MONETIO source name, such as ``gfs`` or ``gefs``.
    files : str or sequence of str, optional
        Locations passed to ``monetio.load``.
    variables : sequence of str
        Dataset variables to expose on reads.
    tolerance : str, datetime.timedelta or pandas.Timedelta
        Maximum distance for nearest-time selection of timed data.
    **reader_options
        Source-specific options forwarded to ``monetio.load``.

    Notes
    -----
    The reader must return an xarray Dataset. Regridding, satellite quality
    screening, and retrieval-to-emission conversion are not performed here.
    """
    def __init__(self, source, files=None, *, variables, tolerance, **reader_options):
        self.source = source
        self.files = files
        self.variables = tuple(variables)
        if not self.variables:
            raise ValueError('At least one source variable is required')
        self.tolerance = tolerance
        self.reader_options = dict(reader_options)
        self._delegate = None
        self._closed = False

    def open(self):
        """Load the dataset on first use and return this source.

        Returns
        -------
        MonetioSource
            The open source.

        Raises
        ------
        ImportError
            If MONETIO is unavailable.
        TypeError
            If the reader does not return an xarray Dataset.
        """
        if self._closed:
            raise RuntimeError('Source is closed')
        if self._delegate is None:
            try:
                import monetio
            except ImportError as exc:
                raise ImportError('MONETIO source requires the optional monetio v1 dependency') from exc
            data = monetio.load(self.source, files=self.files, **self.reader_options)
            if not hasattr(data, 'data_vars'):
                raise TypeError('MONETIO reader must return an xarray Dataset for gridded input')
            self._delegate = DatasetSource(data, variables=self.variables, tolerance=self.tolerance)
        return self

    def read(self, time, selection=None):
        """Read selected variables at a bounded time.

        Parameters
        ----------
        time : datetime-like
            Requested valid time.
        selection : mapping, optional
            Explicit coordinate labels for ensemble and forecast dimensions.

        Returns
        -------
        xarray.Dataset
            Selected fields, retaining their metadata.
        """
        self.open()
        return self._delegate.read(time, selection)

    def close(self):
        """Close the loaded dataset; repeated calls have no effect."""
        if not self._closed:
            if self._delegate is not None:
                self._delegate.close()
            self._closed = True

    def __enter__(self):
        return self.open()

    def __exit__(self, *args):
        self.close()
