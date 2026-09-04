"""
Integration tests for earthaccess cloud-native NASA Earthdata streaming into
CECE BDSNP (soil NO) and MEGAN3 (biogenic isoprene) physics on a global
4°×5° grid (72 lon × 46 lat), matching the HEMCO parity grids from PRs #85
and #90.

Test classes
------------
TestEarthAccessStreamConfig
    Unit tests for EarthAccessStreamConfig dataclass construction and defaults.

TestParseEarthAccessStreams
    Unit tests for parse_earthaccess_streams() config-dict helper.

TestCeceConfigEarthAccessRouting
    Verifies that CeceConfig._from_dict routes source:earthaccess streams to
    earthaccess_streams and leaves AMIO streams on their original path.

TestEarthAccessStreamResolverMocked
    Tests EarthAccessStreamResolver.open_as_xarray() with earthaccess fully
    mocked — no EDL credentials required, runs in CI.

TestEarthAccessStreamBridgeMocked
    Tests EarthAccessStreamBridge.inject_at_time() end-to-end using a mocked
    xr.Dataset and a real _cece_core.CeceImportState if the pybind11 module
    is available, or a lightweight stub if not.

TestBDSNPFieldInjectionOnHemcoGrid
    Constructs synthetic SMAP-like soil temperature and moisture on the exact
    HEMCO 4°×5° 72×46 grid and asserts that inject_at_time delivers finite,
    physically plausible arrays in the import state.  Mirrors the scope of PR
    #85 (HEMCO 3.12.1 SoilNOx parity on the 4°×5° reference grid).

TestMEGAN3FieldInjectionOnHemcoGrid
    Constructs synthetic MODIS LAI, SMAP soil moisture, and CERES PAR on the
    same 72×46 grid and asserts that inject_at_time delivers all six MEGAN3
    import fields.  Mirrors the scope of PR #90 (HEMCO 3.12.1 MEGAN isoprene
    parity on the 4°×5° reference grid).

TestEarthAccessImportError
    Verifies that a helpful ImportError is raised when the cloud extras are
    absent, rather than a bare AttributeError at import time.

TestLiveEarthDataIntegration  (mark: live_earthdata)
    Skipped in CI — requires real EDL credentials stored in env vars or
    ~/.netrc.  Performs a small real CMR search and fsspec open against the
    SMAP SPL4SMGP collection as a smoke test.

Running
-------
# Fast (no credentials): all tests except live_earthdata
pytest tests/test_earthaccess_stream_bdsnp_megan3.py -v

# Include live credential test (requires EARTHDATA_USERNAME / EARTHDATA_TOKEN)
pytest tests/test_earthaccess_stream_bdsnp_megan3.py -v -m live_earthdata
"""

from __future__ import annotations

import os
import sys
from datetime import datetime, timedelta
from pathlib import Path
from unittest.mock import MagicMock, patch

import numpy as np
import pytest
import yaml

# ── Path bootstrap: prefer build output, fall back to source ──────────────────
_REPO_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(_REPO_ROOT / "build" / "src" / "python"))
sys.path.insert(0, str(_REPO_ROOT / "src" / "python"))

# Load earthaccess_resolver and stream_bridge directly from source so that
# their relative-import package context is not required in the test environment.
import importlib.util as _ilu

def _load_src(name: str):
    spec = _ilu.spec_from_file_location(
        name, str(_REPO_ROOT / "src" / "python" / f"{name}.py")
    )
    mod = _ilu.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod

_ea_resolver_mod = _load_src("earthaccess_resolver")
_stream_bridge_mod = _load_src("stream_bridge")

# config.py uses `from .earthaccess_resolver import ...`; inject the already-
# loaded module so the relative import resolves correctly when config is loaded.
sys.modules["earthaccess_resolver"] = _ea_resolver_mod

# ── Optional pybind11 import ──────────────────────────────────────────────────
try:
    sys.path.insert(
        0,
        str(_REPO_ROOT / "build" / "src" / "python" / "cece"),
    )
    import _cece_core

    _CECE_CORE_AVAILABLE = True
except ImportError:
    _cece_core = None  # type: ignore[assignment]
    _CECE_CORE_AVAILABLE = False

# ── Optional earthaccess / xarray imports ─────────────────────────────────────
try:
    import xarray as xr

    _XARRAY_AVAILABLE = True
except ImportError:
    xr = None  # type: ignore[assignment]
    _XARRAY_AVAILABLE = False

try:
    import earthaccess  # noqa: F401

    _EARTHACCESS_AVAILABLE = True
except ImportError:
    _EARTHACCESS_AVAILABLE = False

# ── Project imports (pulled from the directly-loaded source modules) ──────────
EarthAccessStreamConfig  = _ea_resolver_mod.EarthAccessStreamConfig
EarthAccessStreamResolver = _ea_resolver_mod.EarthAccessStreamResolver
EarthAccessStreamBridge  = _stream_bridge_mod.EarthAccessStreamBridge
from config import CeceConfig, parse_earthaccess_streams

# ── Markers ───────────────────────────────────────────────────────────────────
live_earthdata = pytest.mark.skipif(
    not (os.getenv("EARTHDATA_USERNAME") and os.getenv("EARTHDATA_TOKEN")),
    reason="Live Earthdata credentials not available (set EARTHDATA_USERNAME + EARTHDATA_TOKEN)",
)

requires_xarray = pytest.mark.skipif(
    not _XARRAY_AVAILABLE,
    reason="xarray not installed — run: pip install 'cece-tools[cloud]'",
)

requires_cece_core = pytest.mark.skipif(
    not _CECE_CORE_AVAILABLE,
    reason="_cece_core pybind11 module not built — run cmake/make first",
)

# ── HEMCO 4°×5° grid constants (from PRs #85 and #90) ────────────────────────
# Global 72 × 46 grid, 5° lon spacing, nominal 4° lat spacing, ±89° polar centres
NX = 72   # longitude points (0°–360° or –180°–175°)
NY = 46   # latitude points  (−89° to +89°)
NZ = 1    # single-level surface emission layer

LON = np.linspace(-177.5, 177.5, NX, dtype=np.float64)      # 5° centres
LAT = np.concatenate([[-89.0], np.linspace(-86.0, 86.0, 44), [89.0]]).astype(np.float64)
TIME_COORD = np.array(
    [np.datetime64("2022-07-01T12:00:00"), np.datetime64("2022-07-02T12:00:00")],
    dtype="datetime64[ns]",
)

# ── Synthetic data factories ──────────────────────────────────────────────────

def _make_smap_dataset() -> "xr.Dataset":
    """Return a minimal SMAP-like xr.Dataset on the 72×46 HEMCO grid."""
    shape = (len(TIME_COORD), NY, NX)
    rng = np.random.default_rng(seed=42)
    soil_temp = rng.uniform(270.0, 310.0, shape).astype(np.float64)
    sm_root   = rng.uniform(0.05, 0.45, shape).astype(np.float64)
    sm_surf   = rng.uniform(0.03, 0.50, shape).astype(np.float64)
    return xr.Dataset(
        {
            "soil_temp_layer1": (["time", "lat", "lon"], soil_temp),
            "sm_rootzone":       (["time", "lat", "lon"], sm_root),
            "sm_surface":        (["time", "lat", "lon"], sm_surf),
        },
        coords={"time": TIME_COORD, "lat": LAT, "lon": LON},
    )


def _make_modis_lai_dataset() -> "xr.Dataset":
    """Return a MODIS MCD15A2H-like xr.Dataset (LAI only) on the 72×46 grid."""
    shape = (len(TIME_COORD), NY, NX)
    rng = np.random.default_rng(seed=7)
    lai = rng.uniform(0.0, 6.0, shape).astype(np.float64)
    return xr.Dataset(
        {"Lai_500m": (["time", "lat", "lon"], lai)},
        coords={"time": TIME_COORD, "lat": LAT, "lon": LON},
    )


def _make_ceres_par_dataset() -> "xr.Dataset":
    """Return a CERES SYN1deg-like xr.Dataset on the 72×46 grid."""
    shape = (len(TIME_COORD), NY, NX)
    rng = np.random.default_rng(seed=13)
    par_dir  = rng.uniform(0.0, 500.0, shape).astype(np.float64)
    par_dif  = rng.uniform(0.0, 200.0, shape).astype(np.float64)
    cos_sza  = rng.uniform(0.0, 1.0,   shape).astype(np.float64)
    return xr.Dataset(
        {
            "sfc_sw_down_dir_all_1h": (["time", "lat", "lon"], par_dir),
            "sfc_sw_down_dif_all_1h": (["time", "lat", "lon"], par_dif),
            "solar_zenith_angle":     (["time", "lat", "lon"], cos_sza),
        },
        coords={"time": TIME_COORD, "lat": LAT, "lon": LON},
    )


# ── ImportState stub (used when _cece_core is not built) ─────────────────────

class _StubImportState:
    """Minimal stand-in for _cece_core.CeceImportState."""

    def __init__(self):
        self._fields: dict = {}

    def set_field(self, name: str, arr: np.ndarray) -> None:
        self._fields[name] = arr

    def get_field_names(self) -> list:
        return list(self._fields.keys())


def _make_import_state():
    if _CECE_CORE_AVAILABLE:
        return _cece_core.CeceImportState()
    return _StubImportState()


# =============================================================================
# 1. EarthAccessStreamConfig dataclass unit tests
# =============================================================================

class TestEarthAccessStreamConfig:
    """Unit tests for EarthAccessStreamConfig construction and defaults."""

    def test_required_fields(self):
        cfg = EarthAccessStreamConfig(
            name="smap_soil",
            short_name="SPL4SMGP",
            temporal_start="2022-07-01",
            temporal_end="2022-07-03",
        )
        assert cfg.short_name == "SPL4SMGP"
        assert cfg.temporal_start == "2022-07-01"
        assert cfg.temporal_end == "2022-07-03"

    def test_defaults(self):
        cfg = EarthAccessStreamConfig(
            name="x", short_name="Y", temporal_start="2022-01-01", temporal_end="2022-01-02"
        )
        assert cfg.variable_map == {}
        assert cfg.bounding_box is None
        assert cfg.version is None
        assert cfg.cloud_hosted is True
        assert cfg.daac is None

    def test_variable_map_stored(self):
        cfg = EarthAccessStreamConfig(
            name="smap",
            short_name="SPL4SMGP",
            temporal_start="2022-07-01",
            temporal_end="2022-07-03",
            variable_map={"sm_rootzone": "soil_moisture_root", "sm_surface": "soil_moisture"},
        )
        assert cfg.variable_map["sm_rootzone"] == "soil_moisture_root"
        assert cfg.variable_map["sm_surface"] == "soil_moisture"

    def test_bounding_box_optional(self):
        cfg = EarthAccessStreamConfig(
            name="x",
            short_name="Y",
            temporal_start="2022-07-01",
            temporal_end="2022-07-03",
            bounding_box=(-180.0, -90.0, 180.0, 90.0),
        )
        assert cfg.bounding_box == (-180.0, -90.0, 180.0, 90.0)


# =============================================================================
# 2. parse_earthaccess_streams helper
# =============================================================================

class TestParseEarthAccessStreams:
    """Unit tests for the parse_earthaccess_streams() module-level function."""

    def _make_raw_cfg(self, *streams):
        return {"cece_data": {"streams": list(streams)}}

    def test_empty_streams(self):
        result = parse_earthaccess_streams({"cece_data": {"streams": []}})
        assert result == []

    def test_skips_amio_streams(self):
        raw = self._make_raw_cfg(
            {"name": "local_nc", "file_paths": ["foo.nc"], "variables": {}}
        )
        result = parse_earthaccess_streams(raw)
        assert result == []

    def test_extracts_earthaccess_stream(self):
        raw = self._make_raw_cfg(
            {
                "name": "smap_soil",
                "source": "earthaccess",
                "short_name": "SPL4SMGP",
                "temporal_start": "2022-07-01",
                "temporal_end": "2022-07-03",
                "variables": {"sm_rootzone": "soil_moisture_root"},
                "daac": "NSIDC_CPRD",
                "cloud_hosted": True,
            }
        )
        result = parse_earthaccess_streams(raw)
        assert len(result) == 1
        cfg = result[0]
        assert cfg.name == "smap_soil"
        assert cfg.short_name == "SPL4SMGP"
        assert cfg.daac == "NSIDC_CPRD"
        assert cfg.variable_map == {"sm_rootzone": "soil_moisture_root"}

    def test_mixed_streams_only_earthaccess_extracted(self):
        raw = self._make_raw_cfg(
            {"name": "local", "file_paths": ["a.nc"], "variables": {}},
            {
                "name": "remote",
                "source": "earthaccess",
                "short_name": "MCD15A2H",
                "temporal_start": "2022-07-01",
                "temporal_end": "2022-07-03",
                "variables": {"Lai_500m": "leaf_area_index"},
            },
        )
        result = parse_earthaccess_streams(raw)
        assert len(result) == 1
        assert result[0].short_name == "MCD15A2H"


# =============================================================================
# 3. CeceConfig routing of source:earthaccess vs AMIO streams
# =============================================================================

class TestCeceConfigEarthAccessRouting:
    """Verifies _from_dict routes streams by source key."""

    def _bdsnp_raw_config(self):
        return {
            "cece_data": {
                "streams": [
                    {
                        "name": "smap_soil",
                        "source": "earthaccess",
                        "short_name": "SPL4SMGP",
                        "temporal_start": "2022-07-01",
                        "temporal_end": "2022-07-03",
                        "variables": {"sm_rootzone": "soil_moisture_root"},
                        "daac": "NSIDC_CPRD",
                    },
                    {
                        "name": "local_bdsnp_biome",
                        "file_paths": ["/data/biome.nc"],
                        "variables": {"biome_frac": "land_use_type"},
                    },
                ]
            },
            "physics_schemes": [{"name": "bdsnp"}],
            "species": {},
        }

    def test_earthaccess_stream_goes_to_property(self):
        config = CeceConfig.from_dict(self._bdsnp_raw_config())
        ea_streams = config.earthaccess_streams
        assert len(ea_streams) == 1
        assert ea_streams[0].short_name == "SPL4SMGP"

    def test_amio_stream_stays_in_cece_data(self):
        config = CeceConfig.from_dict(self._bdsnp_raw_config())
        amio_streams = [
            s for s in config.cece_data.get("streams", [])
            if not isinstance(s, EarthAccessStreamConfig)
        ]
        # The local stream should be present; earthaccess one should not
        names = [s.name for s in amio_streams]
        assert "local_bdsnp_biome" in names
        assert "smap_soil" not in names

    def test_no_earthaccess_streams_gives_empty_list(self):
        config = CeceConfig.from_dict({
            "cece_data": {"streams": [
                {"name": "x", "file_paths": ["x.nc"], "variables": {}}
            ]},
            "physics_schemes": [],
            "species": {},
        })
        assert config.earthaccess_streams == []


# =============================================================================
# 4. EarthAccessStreamResolver — mocked earthaccess
# =============================================================================

@requires_xarray
class TestEarthAccessStreamResolverMocked:
    """Tests EarthAccessStreamResolver with earthaccess fully mocked."""

    def _make_resolver_with_mock(self, dataset):
        """Patch earthaccess and return a resolver whose open_as_xarray returns *dataset*."""
        with (
            patch("earthaccess_resolver.earthaccess") as mock_ea,
            patch("earthaccess_resolver._EARTHACCESS_AVAILABLE", True),
        ):
            mock_ea.login.return_value = MagicMock()
            mock_ea.search_data.return_value = [MagicMock()]
            mock_ea.open.return_value = [MagicMock()]

            with patch("earthaccess_resolver.xr") as mock_xr:
                mock_xr.open_mfdataset.return_value = dataset
                resolver = EarthAccessStreamResolver.__new__(EarthAccessStreamResolver)
                resolver._auth = MagicMock()

                cfg = EarthAccessStreamConfig(
                    name="smap_soil",
                    short_name="SPL4SMGP",
                    temporal_start="2022-07-01",
                    temporal_end="2022-07-03",
                    variable_map={"sm_rootzone": "soil_moisture_root"},
                )
                with (
                    patch.object(resolver, "__class__", EarthAccessStreamResolver),
                    patch("earthaccess_resolver.earthaccess", mock_ea),
                    patch("earthaccess_resolver.xr", mock_xr),
                ):
                    result = mock_xr.open_mfdataset.return_value
                return result, cfg

    def test_resolver_returns_dataset(self):
        ds = _make_smap_dataset()
        mock_ea = MagicMock()
        mock_ea.login.return_value = MagicMock()
        mock_ea.search_data.return_value = [MagicMock()]
        mock_ea.open.return_value = [MagicMock()]

        mock_xr = MagicMock()
        mock_xr.open_mfdataset.return_value = ds

        # Inject mocks as module attributes so patch() can find them
        _ea_resolver_mod.earthaccess = mock_ea
        _ea_resolver_mod.xr = mock_xr
        _ea_resolver_mod._EARTHACCESS_AVAILABLE = True

        try:
            resolver = EarthAccessStreamResolver.__new__(EarthAccessStreamResolver)
            resolver._auth = MagicMock()

            cfg = EarthAccessStreamConfig(
                name="smap_soil",
                short_name="SPL4SMGP",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
            )
            result = resolver.open_as_xarray(cfg)
        finally:
            # Restore original module state
            _ea_resolver_mod._EARTHACCESS_AVAILABLE = _EARTHACCESS_AVAILABLE
            for attr in ("earthaccess", "xr"):
                if not _EARTHACCESS_AVAILABLE:
                    _ea_resolver_mod.__dict__.pop(attr, None)

        assert result is ds

    def test_resolver_raises_on_no_granules(self):
        mock_ea = MagicMock()
        mock_ea.login.return_value = MagicMock()
        mock_ea.search_data.return_value = []  # nothing found

        _ea_resolver_mod.earthaccess = mock_ea
        _ea_resolver_mod._EARTHACCESS_AVAILABLE = True

        try:
            resolver = EarthAccessStreamResolver.__new__(EarthAccessStreamResolver)
            resolver._auth = MagicMock()

            cfg = EarthAccessStreamConfig(
                name="bad",
                short_name="DOES_NOT_EXIST",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
            )
            with pytest.raises(RuntimeError, match="No earthaccess granules"):
                resolver.open_as_xarray(cfg)
        finally:
            _ea_resolver_mod._EARTHACCESS_AVAILABLE = _EARTHACCESS_AVAILABLE
            if not _EARTHACCESS_AVAILABLE:
                _ea_resolver_mod.__dict__.pop("earthaccess", None)


# =============================================================================
# 5. EarthAccessStreamBridge field injection
# =============================================================================

@requires_xarray
class TestEarthAccessStreamBridgeMocked:
    """Tests EarthAccessStreamBridge.inject_at_time() without real earthaccess calls."""

    def _make_bridge(self, configs, datasets):
        """Return a bridge whose internal datasets are pre-set to *datasets*."""
        bridge = EarthAccessStreamBridge.__new__(EarthAccessStreamBridge)
        bridge._datasets = datasets
        bridge._configs  = configs
        return bridge

    def test_inject_at_time_populates_import_state(self):
        smap_ds = _make_smap_dataset()
        cfg = EarthAccessStreamConfig(
            name="smap",
            short_name="SPL4SMGP",
            temporal_start="2022-07-01",
            temporal_end="2022-07-03",
            variable_map={"sm_rootzone": "soil_moisture_root", "sm_surface": "soil_moisture"},
        )
        bridge = self._make_bridge([cfg], [smap_ds])
        state  = _make_import_state()

        t = datetime(2022, 7, 1, 12, 0, 0)
        bridge.inject_at_time(state, t)

        names = state.get_field_names()
        assert "soil_moisture_root" in names
        assert "soil_moisture" in names

    def test_injected_array_is_fortran_contiguous(self):
        smap_ds = _make_smap_dataset()
        cfg = EarthAccessStreamConfig(
            name="smap",
            short_name="SPL4SMGP",
            temporal_start="2022-07-01",
            temporal_end="2022-07-03",
            variable_map={"sm_rootzone": "soil_moisture_root"},
        )
        bridge = self._make_bridge([cfg], [smap_ds])
        state  = _StubImportState()

        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))

        arr = state._fields["soil_moisture_root"]
        assert arr.flags["F_CONTIGUOUS"], "Bridge must deliver Fortran-contiguous arrays"

    def test_injected_array_is_float64(self):
        smap_ds = _make_smap_dataset()
        cfg = EarthAccessStreamConfig(
            name="smap",
            short_name="SPL4SMGP",
            temporal_start="2022-07-01",
            temporal_end="2022-07-03",
            variable_map={"sm_rootzone": "soil_moisture_root"},
        )
        bridge = self._make_bridge([cfg], [smap_ds])
        state  = _StubImportState()

        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))

        arr = state._fields["soil_moisture_root"]
        assert arr.dtype == np.float64

    def test_multiple_streams_injected(self):
        smap_ds = _make_smap_dataset()
        lai_ds  = _make_modis_lai_dataset()
        cfgs = [
            EarthAccessStreamConfig(
                name="smap",
                short_name="SPL4SMGP",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={"sm_rootzone": "soil_moisture_root"},
            ),
            EarthAccessStreamConfig(
                name="modis_lai",
                short_name="MCD15A2H",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={"Lai_500m": "leaf_area_index"},
            ),
        ]
        bridge = self._make_bridge(cfgs, [smap_ds, lai_ds])
        state  = _StubImportState()

        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))

        assert "soil_moisture_root" in state.get_field_names()
        assert "leaf_area_index"    in state.get_field_names()


# =============================================================================
# 6. BDSNP field injection on the HEMCO 4°×5° grid  (mirrors PR #85)
# =============================================================================

@requires_xarray
class TestBDSNPFieldInjectionOnHemcoGrid:
    """BDSNP soil-NO inputs streamed on the exact HEMCO 72×46 4°×5° grid.

    Mirrors the reference grid from PR #85 (HEMCO 3.12.1 SoilNOx parity).
    Validates that earthaccess bridge produces arrays of the correct shape,
    dtype, memory order, and physically plausible value range before they
    would reach the BDSNP C++ kernel.
    """

    @pytest.fixture
    def smap_bridge_and_state(self):
        smap_ds = _make_smap_dataset()
        cfgs = [
            EarthAccessStreamConfig(
                name="smap_soiltemp",
                short_name="SPL4SMGP",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={"soil_temp_layer1": "soil_temperature"},
            ),
            EarthAccessStreamConfig(
                name="smap_soilmoist",
                short_name="SPL4SMGP",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={
                    "sm_rootzone": "soil_moisture_root",
                    "sm_surface":  "soil_moisture",
                },
            ),
        ]
        bridge = EarthAccessStreamBridge.__new__(EarthAccessStreamBridge)
        bridge._datasets = [smap_ds, smap_ds]
        bridge._configs  = cfgs
        state = _StubImportState()
        return bridge, state

    def test_bdsnp_fields_injected(self, smap_bridge_and_state):
        bridge, state = smap_bridge_and_state
        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))
        for field in ("soil_temperature", "soil_moisture_root", "soil_moisture"):
            assert field in state.get_field_names(), f"Missing BDSNP field: {field}"

    def test_soil_temperature_shape_matches_hemco_grid(self, smap_bridge_and_state):
        bridge, state = smap_bridge_and_state
        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))
        arr = state._fields["soil_temperature"]
        # shape is (NY, NX) after sel() drops time dimension
        assert arr.shape == (NY, NX), (
            f"Expected HEMCO 4x5 shape ({NY}, {NX}), got {arr.shape}"
        )

    def test_soil_temperature_physically_plausible(self, smap_bridge_and_state):
        bridge, state = smap_bridge_and_state
        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))
        T = state._fields["soil_temperature"]
        # SMAP SPL4SMGP soil temperature range: 200–340 K
        assert np.all(np.isfinite(T)), "Soil temperature contains non-finite values"
        assert T.min() >= 200.0,       "Soil temperature below 200 K is non-physical"
        assert T.max() <= 340.0,       "Soil temperature above 340 K is non-physical"

    def test_soil_moisture_range(self, smap_bridge_and_state):
        bridge, state = smap_bridge_and_state
        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))
        sm = state._fields["soil_moisture"]
        # Volumetric soil moisture fraction: 0–1
        assert np.all(np.isfinite(sm)), "Soil moisture contains non-finite values"
        assert sm.min() >= 0.0,         "Soil moisture below 0 is non-physical"
        assert sm.max() <= 1.0,         "Soil moisture above 1 is non-physical"

    def test_timestep_advance_uses_nearest_granule(self, smap_bridge_and_state):
        """Advancing the timestep by 24 h must still resolve to a valid granule."""
        bridge, state1 = smap_bridge_and_state
        state2 = _StubImportState()
        bridge.inject_at_time(state1, datetime(2022, 7, 1, 12, 0, 0))
        bridge.inject_at_time(state2, datetime(2022, 7, 2, 12, 0, 0))
        for f in ("soil_temperature", "soil_moisture"):
            assert f in state2.get_field_names()

    @requires_cece_core
    def test_set_field_accepted_by_cece_import_state(self, smap_bridge_and_state):
        """Fields produced by the bridge are accepted by the real CeceImportState."""
        bridge, _ = smap_bridge_and_state
        real_state = _cece_core.CeceImportState()
        bridge.inject_at_time(real_state, datetime(2022, 7, 1, 12, 0, 0))
        names = real_state.get_field_names()
        assert "soil_temperature" in names
        assert "soil_moisture"    in names


# =============================================================================
# 7. MEGAN3 field injection on the HEMCO 4°×5° grid  (mirrors PR #90)
# =============================================================================

@requires_xarray
class TestMEGAN3FieldInjectionOnHemcoGrid:
    """MEGAN3 isoprene inputs streamed on the exact HEMCO 72×46 4°×5° grid.

    Mirrors the reference grid from PR #90 (HEMCO 3.12.1 MEGAN isoprene
    parity).  Validates that all six MEGAN3 import fields are delivered with
    correct shape, dtype, memory order, and plausible physical ranges.
    """

    MEGAN3_FIELDS = {
        # field name in CECE import state : (min_val, max_val, description)
        "leaf_area_index":      (0.0, 10.0,  "LAI [m2/m2]"),
        "soil_moisture_root":   (0.0,  1.0,  "root-zone volumetric soil moisture"),
        "par_direct":           (0.0, 600.0, "direct PAR [W/m2]"),
        "par_diffuse":          (0.0, 300.0, "diffuse PAR [W/m2]"),
        "solar_cosine":         (0.0,  1.0,  "cosine of solar zenith angle"),
    }

    @pytest.fixture
    def megan3_bridge_and_state(self):
        lai_ds  = _make_modis_lai_dataset()
        smap_ds = _make_smap_dataset()
        par_ds  = _make_ceres_par_dataset()

        cfgs = [
            EarthAccessStreamConfig(
                name="modis_lai",
                short_name="MCD15A2H",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={"Lai_500m": "leaf_area_index"},
            ),
            EarthAccessStreamConfig(
                name="smap_soil",
                short_name="SPL4SMGP",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={
                    "sm_rootzone": "soil_moisture_root",
                    "sm_surface":  "soil_moisture",
                },
            ),
            EarthAccessStreamConfig(
                name="ceres_par",
                short_name="CER_SYN1deg-Day_Terra-Aqua-MODIS_Edition4A",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={
                    "sfc_sw_down_dir_all_1h": "par_direct",
                    "sfc_sw_down_dif_all_1h": "par_diffuse",
                    "solar_zenith_angle":     "solar_cosine",
                },
            ),
        ]
        bridge = EarthAccessStreamBridge.__new__(EarthAccessStreamBridge)
        bridge._datasets = [lai_ds, smap_ds, par_ds]
        bridge._configs  = cfgs
        state = _StubImportState()
        return bridge, state

    def test_all_megan3_fields_injected(self, megan3_bridge_and_state):
        bridge, state = megan3_bridge_and_state
        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))
        for field in self.MEGAN3_FIELDS:
            assert field in state.get_field_names(), f"Missing MEGAN3 field: {field}"

    @pytest.mark.parametrize("field,bounds", [
        (f, b[:2]) for f, b in MEGAN3_FIELDS.items()
    ])
    def test_megan3_field_physical_range(self, megan3_bridge_and_state, field, bounds):
        bridge, state = megan3_bridge_and_state
        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))
        arr = state._fields[field]
        lo, hi = bounds
        assert np.all(np.isfinite(arr)),   f"{field}: non-finite values found"
        assert arr.min() >= lo,             f"{field}: min {arr.min():.4f} < {lo}"
        assert arr.max() <= hi,             f"{field}: max {arr.max():.4f} > {hi}"

    def test_lai_shape_matches_hemco_grid(self, megan3_bridge_and_state):
        bridge, state = megan3_bridge_and_state
        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))
        arr = state._fields["leaf_area_index"]
        assert arr.shape == (NY, NX), (
            f"Expected HEMCO 4x5 shape ({NY}, {NX}), got {arr.shape}"
        )

    def test_all_megan3_arrays_fortran_contiguous(self, megan3_bridge_and_state):
        bridge, state = megan3_bridge_and_state
        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))
        for field in self.MEGAN3_FIELDS:
            arr = state._fields[field]
            assert arr.flags["F_CONTIGUOUS"], (
                f"{field}: not Fortran-contiguous (Kokkos LayoutLeft required)"
            )

    def test_timestep_advance_nearest_time_match(self, megan3_bridge_and_state):
        bridge, _ = megan3_bridge_and_state
        for dt_hours in (0, 6, 12, 18, 24):
            state = _StubImportState()
            t = datetime(2022, 7, 1, 0, 0, 0) + timedelta(hours=dt_hours)
            bridge.inject_at_time(state, t)
            assert "leaf_area_index" in state.get_field_names()

    @requires_cece_core
    def test_set_field_accepted_by_cece_import_state(self, megan3_bridge_and_state):
        """All MEGAN3 fields produced by the bridge are accepted by real CeceImportState."""
        bridge, _ = megan3_bridge_and_state
        real_state = _cece_core.CeceImportState()
        bridge.inject_at_time(real_state, datetime(2022, 7, 1, 12, 0, 0))
        names = real_state.get_field_names()
        for field in self.MEGAN3_FIELDS:
            assert field in names, f"CeceImportState missing MEGAN3 field: {field}"


# =============================================================================
# 8. Combined BDSNP + MEGAN3 simultaneous injection
# =============================================================================

@requires_xarray
class TestCombinedBdsnpMegan3Injection:
    """Simultaneous BDSNP + MEGAN3 stream injection (all nine fields at once)."""

    ALL_FIELDS = {
        # BDSNP
        "soil_temperature",
        "soil_moisture_root",
        "soil_moisture",
        # MEGAN3
        "leaf_area_index",
        "par_direct",
        "par_diffuse",
        "solar_cosine",
    }

    def test_all_fields_available_after_combined_inject(self):
        smap_ds = _make_smap_dataset()
        lai_ds  = _make_modis_lai_dataset()
        par_ds  = _make_ceres_par_dataset()

        cfgs = [
            EarthAccessStreamConfig(
                name="smap",
                short_name="SPL4SMGP",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={
                    "soil_temp_layer1": "soil_temperature",
                    "sm_rootzone":      "soil_moisture_root",
                    "sm_surface":       "soil_moisture",
                },
            ),
            EarthAccessStreamConfig(
                name="modis_lai",
                short_name="MCD15A2H",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={"Lai_500m": "leaf_area_index"},
            ),
            EarthAccessStreamConfig(
                name="ceres_par",
                short_name="CER_SYN1deg-Day_Terra-Aqua-MODIS_Edition4A",
                temporal_start="2022-07-01",
                temporal_end="2022-07-03",
                variable_map={
                    "sfc_sw_down_dir_all_1h": "par_direct",
                    "sfc_sw_down_dif_all_1h": "par_diffuse",
                    "solar_zenith_angle":     "solar_cosine",
                },
            ),
        ]

        bridge = EarthAccessStreamBridge.__new__(EarthAccessStreamBridge)
        bridge._datasets = [smap_ds, lai_ds, par_ds]
        bridge._configs  = cfgs
        state = _StubImportState()

        bridge.inject_at_time(state, datetime(2022, 7, 1, 12, 0, 0))

        injected = set(state.get_field_names())
        missing  = self.ALL_FIELDS - injected
        assert not missing, f"Missing fields after combined injection: {missing}"


# =============================================================================
# 9. ImportError guard when cloud extras are absent
# =============================================================================

class TestEarthAccessImportError:
    """Verifies that missing cloud extras produce a helpful ImportError."""

    def test_resolver_raises_on_missing_earthaccess(self):
        with patch("earthaccess_resolver._EARTHACCESS_AVAILABLE", False):
            with pytest.raises(ImportError, match="cece-tools\\[cloud\\]"):
                EarthAccessStreamResolver(auth_strategy="all")


# =============================================================================
# 10. cece_config_earthaccess_4x5_test.yaml round-trip
# =============================================================================

class TestExampleConfigParsing:
    """Checks that the 4x5 test YAML parses correctly via CeceConfig."""

    _YAML_PATH = Path(__file__).parent / "cece_config_earthaccess_4x5_test.yaml"

    def test_yaml_file_exists(self):
        assert self._YAML_PATH.exists(), (
            f"Test config not found: {self._YAML_PATH}"
        )

    def test_yaml_parses_without_error(self):
        raw = yaml.safe_load(self._YAML_PATH.read_text())
        config = CeceConfig.from_dict(raw)
        assert config is not None

    def test_yaml_yields_earthaccess_streams(self):
        raw = yaml.safe_load(self._YAML_PATH.read_text())
        config = CeceConfig.from_dict(raw)
        assert len(config.earthaccess_streams) > 0, (
            "Expected at least one earthaccess stream in test config"
        )

    def test_yaml_contains_bdsnp_and_megan3_schemes(self):
        raw = yaml.safe_load(self._YAML_PATH.read_text())
        config = CeceConfig.from_dict(raw)
        scheme_names = {s.name for s in config.physics_schemes}
        assert "bdsnp"  in scheme_names, "bdsnp scheme missing from test config"
        assert "megan3" in scheme_names, "megan3 scheme missing from test config"


# =============================================================================
# 11. Live Earthdata smoke test  (skipped in CI)
# =============================================================================

@live_earthdata
@requires_xarray
class TestLiveEarthDataIntegration:
    """Smoke test against real NASA Earthdata — requires EDL credentials.

    Set EARTHDATA_USERNAME and EARTHDATA_TOKEN before running:
        pytest -m live_earthdata tests/test_earthaccess_stream_bdsnp_megan3.py
    """

    def test_smap_granule_search_returns_results(self):
        """CMR search for one SMAP SPL4SMGP granule must return ≥1 result."""
        import earthaccess

        earthaccess.login(strategy="environment")
        granules = earthaccess.search_data(
            short_name="SPL4SMGP",
            temporal=("2022-07-01", "2022-07-02"),
            cloud_hosted=True,
            count=1,
        )
        assert len(granules) >= 1, "CMR returned no SMAP SPL4SMGP granules"

    def test_modis_lai_granule_search_returns_results(self):
        """CMR search for one MODIS MCD15A2H granule must return ≥1 result."""
        import earthaccess

        earthaccess.login(strategy="environment")
        granules = earthaccess.search_data(
            short_name="MCD15A2H",
            version="061",
            temporal=("2022-07-01", "2022-07-08"),
            cloud_hosted=True,
            count=1,
        )
        assert len(granules) >= 1, "CMR returned no MODIS MCD15A2H granules"

    def test_smap_granule_can_be_opened_as_fsspec_stream(self):
        """At least one SMAP granule must open as a readable fsspec file object."""
        import earthaccess

        earthaccess.login(strategy="environment")
        granules = earthaccess.search_data(
            short_name="SPL4SMGP",
            temporal=("2022-07-01", "2022-07-02"),
            cloud_hosted=True,
            count=1,
        )
        assert granules, "No SMAP granules to open"
        fobjs = earthaccess.open(granules)
        assert fobjs, "earthaccess.open returned empty list"
        # Read first 512 bytes to confirm the object is readable
        chunk = fobjs[0].read(512)
        assert len(chunk) > 0, "fsspec file object returned zero bytes"


if __name__ == "__main__":
    pytest.main([__file__, "-v", "--tb=short"])
