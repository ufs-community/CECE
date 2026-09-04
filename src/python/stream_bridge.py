"""Bridge earthaccess xarray datasets into CECE import state per timestep."""

from __future__ import annotations

from datetime import datetime
from typing import TYPE_CHECKING, List

import numpy as np

# Support both package (relative) and direct-module import
try:
    from .earthaccess_resolver import EarthAccessStreamConfig, EarthAccessStreamResolver
except ImportError:
    from earthaccess_resolver import EarthAccessStreamConfig, EarthAccessStreamResolver  # type: ignore[no-redef]

if TYPE_CHECKING:
    # Avoid hard dependency on the pybind11 module at import time
    from ._cece_core import CeceImportState  # type: ignore[import]


class EarthAccessStreamBridge:
    """Inject NASA Earthdata fields into CeceImportState at each model timestep.

    Opens all configured streams once at construction via fsspec (no local
    download), then slices the remote datasets by nearest time match on each
    call to :meth:`inject_at_time`.

    Parameters
    ----------
    configs : list of EarthAccessStreamConfig
        One entry per ``source: earthaccess`` stream declared in the YAML.
    auth_strategy : str
        Earthdata Login strategy forwarded to :class:`EarthAccessStreamResolver`.
    """

    def __init__(
        self,
        configs: List[EarthAccessStreamConfig],
        auth_strategy: str = "all",
    ) -> None:
        resolver = EarthAccessStreamResolver(auth_strategy=auth_strategy)
        self._datasets = [resolver.open_as_xarray(c) for c in configs]
        self._configs = configs

    def inject_at_time(self, import_state: "CeceImportState", t: datetime) -> None:
        """Slice remote datasets at time *t* and push arrays into import state.

        Each field is cast to float64 and, when 2-D, shaped to ``(ny, nx)``
        matching CECE's Fortran-contiguous (LayoutLeft) Kokkos convention.

        Parameters
        ----------
        import_state : CeceImportState
            The pybind11-wrapped CECE import state for the current timestep.
        t : datetime
            Model wall-clock time; granules are selected by nearest match.
        """
        for ds, cfg in zip(self._datasets, self._configs):
            ds_t = ds.sel(time=t, method="nearest")
            for nasa_var, cece_field in cfg.variable_map.items():
                arr = np.asfortranarray(ds_t[nasa_var].values, dtype=np.float64)
                import_state.set_field(cece_field, arr)
