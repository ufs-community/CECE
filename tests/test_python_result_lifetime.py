"""High-level results are snapshots; low-level views retain their allocation."""
import gc
import numpy as np
import cece
from cece import _cece_core


def test_export_view_retains_allocation_after_state_destruction():
    _cece_core.initialize_kokkos()
    state = _cece_core.CeceExportState()
    state.set_field("field", np.full((9, 7, 2), 17.0, order="F"))
    result = state.get_field("field")
    del state
    gc.collect()
    allocations = [np.full(result.shape, -99.0, order="F") for _ in range(100)]
    np.testing.assert_array_equal(result, 17.0)


def test_high_level_result_is_owning_snapshot_after_close():
    cfg = {"species": {"CO": [{"field": "source", "operation": "add"}]}}
    state = cece.CeceState(2, 3, 1)
    state.add_import_field("source", np.full((2, 3, 1), 2.0))
    cece.initialize(cfg)
    try:
        cece.compute(state)
        result = state.get_export_field("CO")
        assert result.flags.owndata
        state.add_import_field("source", np.full((2, 3, 1), 5.0))
        cece.compute(state)
        np.testing.assert_array_equal(result, 2.0)
    finally:
        cece.finalize()
    gc.collect()
    np.testing.assert_array_equal(result, 2.0)
