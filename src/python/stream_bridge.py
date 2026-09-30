"""Bridge earthaccess xarray datasets into CECE import state per timestep."""

from __future__ import annotations

from datetime import datetime
from typing import TYPE_CHECKING, Any, List, Optional, Tuple

import numpy as np

# Support both package (relative) and direct-module import
try:
    from .earthaccess_resolver import EarthAccessStreamConfig, EarthAccessStreamResolver
except ImportError:
    from earthaccess_resolver import EarthAccessStreamConfig, EarthAccessStreamResolver  # type: ignore[no-redef]

if TYPE_CHECKING:
    # Avoid hard dependency on the pybind11 module at import time
    from ._cece_core import CeceImportState  # type: ignore[import]


def _variable_target_and_transform(mapping: Any) -> Tuple[str, Optional[str]]:
    if isinstance(mapping, str):
        return mapping, None
    if isinstance(mapping, dict):
        target = mapping.get("model") or mapping.get("field") or mapping.get("name")
        if not target:
            raise ValueError("earthaccess variable mapping dict must contain 'model'")
        return str(target), mapping.get("transform")
    raise TypeError("earthaccess variable mapping must be a string or mapping dict")


def _apply_transform(values: np.ndarray, transform: Optional[str]) -> np.ndarray:
    if transform is None or transform == "none":
        return values
    if transform == "cos_degrees":
        return np.clip(np.cos(np.deg2rad(values)), 0.0, 1.0)
    if transform == "cos_radians":
        return np.clip(np.cos(values), 0.0, 1.0)
    raise ValueError(f"Unsupported earthaccess variable transform: {transform}")


def _validate_injected_field(field_name: str, values: np.ndarray) -> None:
    if not np.all(np.isfinite(values)):
        raise ValueError(f"earthaccess field {field_name!r} contains non-finite values")
    if field_name == "solar_cosine" and (
        float(np.nanmin(values)) < -1.0e-12 or float(np.nanmax(values)) > 1.0 + 1.0e-12
    ):
        raise ValueError(
            "earthaccess field 'solar_cosine' must be in [0, 1]; "
            "use transform: cos_degrees or transform: cos_radians for solar zenith angle inputs"
        )


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
            for nasa_var, mapping in cfg.variable_map.items():
                cece_field, transform = _variable_target_and_transform(mapping)
                values = _apply_transform(ds_t[nasa_var].values, transform)
                arr = np.asfortranarray(values, dtype=np.float64)
                _validate_injected_field(cece_field, arr)
                import_state.set_field(cece_field, arr)
