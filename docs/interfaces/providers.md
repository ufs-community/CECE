# Python data sources

`DatasetSource` accepts a caller-supplied xarray Dataset. `MonetioSource`
uses `monetio.load(source, files=..., **reader_options)` from the MONETIO v1
revision identified in [compatibility.md](compatibility.md). MONETIO and its
reader-specific dependencies are optional; the adapter imports it when opened.

```python
from cece.sources import DatasetSource

source = DatasetSource(dataset, variables=['forcing'], tolerance='30min')
with model:
    model.add_source(source, mapping={'forcing': 'base_anthropogenic_nox'},
                     selection={'member': 0})
    result = model.step(time='2020-01-01T00:00:00Z', dt=3600)
```

A custom provider can implement `open()`, `read(time, selection)` returning a
mapping of labeled fields, and `close()`. A model owns attached providers and
closes them. Each destination field has one owner; an attached source cannot
replace an explicitly set input. Restart serialization with attached providers
is currently unsupported. Restarting a source-driven run requires the caller to
reconstruct its model and forcing explicitly.

Time selection is nearest with an explicit tolerance. Ensemble members and
recognized forecast cycle and lead dimensions require explicit selection.
Forecast lead coordinates must be resolved into a valid-time axis and removed
before using `DatasetSource`: the pinned MONETIO NCEP reader can discard a
separate `valid_time` coordinate when a `time` coordinate is present.
Inputs must already have the model's dimensions, coordinates, and units; there
is no implicit regridding or retrieval-to-emission conversion. For two-dimensional
rectilinear fields, `cece.preparation.regrid_rectilinear` explicitly uses the
same AXIS regridding path as the native driver; it currently supports only
passthrough and nearest-neighbor methods, with finite unmasked fields and full
target coverage. GFS, GEFS and
satellite readers can be requested through MONETIO, but their individual file
formats, forecast conventions, swath geometry, quality screening and coverage
have not yet been validated by this adapter. Prepare such data explicitly and
check geometry and provenance before supplying it to a model.

The optional regridding extension links the CECE driver and can be disabled at
build time with `-DCECE_PYTHON_REGRID=OFF` for physics-only Python builds.
