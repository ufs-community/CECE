"""All Python configuration forms must use the complete native interpretation."""

import cece
import numpy as np
from cece import _cece_core


def rich_config():
    return {
        "meteorology": {"temperature": "T"},
        "scale_factors": {"factor": "FACTOR"},
        "masks": {"mask": "MASK"},
        "temporal_cycles": {"day": [1.0] * 24},
        "physics_schemes": [{"name": "native_example", "options": {"multiplier": 3.0}}],
        "species": {
            "CO": [
                {
                    "field": "source",
                    "operation": "add",
                    "scale": 2.0,
                    "hierarchy": 4,
                    "mask": ["mask"],
                    "scale_fields": ["factor"],
                    "diurnal_cycle": "day",
                }
            ]
        },
    }


def test_python_conversion_preserves_native_configuration(tmp_path):
    cfg = cece.CeceConfig.from_dict(rich_config())
    path = tmp_path / "config.yaml"
    path.write_text(cfg.to_yaml())
    expected = _cece_core.ParseConfig(str(path))
    actual = cece._build_cpp_config(cfg)
    assert actual.met_mapping == expected.met_mapping
    assert actual.scale_factor_mapping == expected.scale_factor_mapping
    assert actual.mask_mapping == expected.mask_mapping
    layer = actual.species_layers["CO"][0]
    reference = expected.species_layers["CO"][0]
    for attr in (
        "field_name",
        "scale",
        "hierarchy",
        "masks",
        "scale_fields",
        "diurnal_cycle",
    ):
        assert getattr(layer, attr) == getattr(reference, attr)
    assert [s.name for s in actual.physics_schemes] == [
        s.name for s in expected.physics_schemes
    ]


def test_compute_override_preserves_scale_field():
    _cece_core.initialize_kokkos()
    cfg = {
        "scale_factors": {"factor": "FACTOR"},
        "species": {
            "CO": [{"field": "source", "operation": "add", "scale_fields": ["factor"]}]
        },
    }
    state = cece.CeceState(2, 3, 1)
    state.add_import_field("source", np.full((2, 3, 1), 2.0))
    state.add_import_field("FACTOR", np.full((2, 3, 1), 3.0))
    cece.initialize(cfg)
    try:
        cece.compute(state, cfg)
        np.testing.assert_allclose(state.get_export_field("CO"), 6.0)
    finally:
        cece.finalize()


def test_long_yaml_string_is_not_interpreted_as_a_filesystem_path():
    config = cece.CeceConfig.from_dict(rich_config())
    text = config.to_yaml()
    assert len(text) > 255
    cece.initialize(text)
    try:
        assert cece._cpp_config.met_mapping == {"temperature": "T"}
        assert cece._cpp_config.mask_mapping == {"mask": "MASK"}
    finally:
        cece.finalize()
