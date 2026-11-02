"""MONETIO v1 source; imports the optional dependency only on first use."""
from __future__ import annotations
from .base import DatasetSource


class MonetioSource:
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
        self.open()
        return self._delegate.read(time, selection)

    def close(self):
        if not self._closed:
            if self._delegate is not None:
                self._delegate.close()
            self._closed = True

    def __enter__(self):
        return self.open()

    def __exit__(self, *args):
        self.close()
