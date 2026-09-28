import importlib.util
import sys
from pathlib import Path
from textwrap import dedent

import pytest


CONFIG_PATH = Path(__file__).parents[1] / "src" / "python" / "config.py"
SPEC = importlib.util.spec_from_file_location("cece_config_schema", CONFIG_PATH)
config = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = config
SPEC.loader.exec_module(config)


def valid_config():
    return {
        "species": {
            "NOx": [
                {
                    "field": "NOX_EMIS",
                    "operation": "add",
                    "mask": "land_mask",
                    "category": "anthropogenic",
                    "hierarchy": 2,
                    "scale_fields": ["temperature"],
                    "diurnal_cycle": "traffic",
                    "weekly_cycle": "weekday",
                    "seasonal_cycle": "seasonal",
                    "vdist": {
                        "method": "height",
                        "h_start": 100.0,
                        "h_end": 500.0,
                    },
                }
            ]
        },
        "temporal_profiles": {
            "traffic": [1.0] * 24,
            "weekday": [1.0] * 7,
            "seasonal": [1.0] * 12,
        },
        "meteorology": {"temperature": "T2M"},
        "scale_factors": {"monthly": "MONTHLY_FACTOR"},
        "masks": {"land_mask": "LAND_FRACTION"},
        "met_registry": {"temperature": ["T2M", "TEMP"]},
        "physics_schemes": [
            {
                "name": "megan",
                "language": "cxx",
                "refresh_interval_seconds": 600,
                "options": {"input_mapping": {"temperature": "T2M"}},
            }
        ],
        "cece_data": {
            "debug_level": 1,
            "streams": [
                {
                    "name": "NOX_EMIS",
                    "file": "emissions.nc",
                    "variables": [
                        {"file": "NOX_FILE", "model": "NOX_EMIS", "levels": 3}
                    ],
                    "cadence": "monthly",
                    "time_units": "days since 2000-01-01",
                    "calendar": "gregorian",
                    "data_model": "classic",
                }
            ],
        },
        "driver": {
            "start_time": "2020-01-01T00:00:00",
            "end_time": "2020-01-02T00:00:00",
            "timestep_seconds": 3600,
            "grid": {"nx": 10, "ny": 5},
        },
        "diagnostics": {"output_interval": 3600, "variables": ["NOx"]},
        "vertical_grid": {"type": "fv3", "ak_field": "AK"},
        "output": {
            "enabled": True,
            "fields": [{"name": "NOx", "attributes": {"units": "kg/m2/s"}}],
        },
    }


def test_full_schema_round_trips_with_canonical_yaml_keys():
    parsed = config.CeceConfig.from_dict(valid_config())

    assert parsed.validate().is_valid
    dumped = parsed.to_dict()
    layer = dumped["species"]["NOx"][0]
    assert layer["vdist"] == {
        "method": "height",
        "layer_start": 0,
        "layer_end": 0,
        "p_start": 0.0,
        "p_end": 0.0,
        "h_start": 100.0,
        "h_end": 500.0,
    }
    assert "vdist_method" not in layer
    assert dumped["cece_data"]["streams"][0]["variables"][0]["levels"] == 3
    assert config.CeceConfig.from_dict(dumped).to_dict() == dumped


def test_yaml_load_uses_nested_vertical_distribution():
    yaml_text = dedent(
        """\
species:
  CO:
    - field: CO
      operation: add
      vdist:
        method: range
        layer_start: 1
        layer_end: 3
        """
    )

    parsed = config.CeceConfig.from_yaml(yaml_text)

    assert parsed.species["CO"][0].vdist.layer_start == 1
    assert parsed.to_dict()["species"]["CO"][0]["vdist"]["method"] == "range"


def test_empty_config_round_trips():
    dumped = config.CeceConfig().to_dict()

    assert config.CeceConfig.from_dict(dumped).to_dict() == dumped


def test_runtime_driver_grid_name_and_log_file_round_trip():
    parsed = config.CeceConfig.from_dict(
        {
            "driver": {
                "log_file": "cece.log",
                "grid": {"grid_name": "F360", "nz": 72},
            }
        }
    )

    dumped = parsed.to_dict()["driver"]
    assert dumped["log_file"] == "cece.log"
    assert dumped["grid"]["grid_name"] == "F360"
    assert config.CeceConfig.from_dict({"driver": dumped}).to_dict()["driver"] == dumped


def test_output_diagnostics_and_global_attributes_round_trip():
    parsed = config.CeceConfig.from_dict(
        {
            "output": {
                "diagnostics": True,
                "global_attributes": {"title": "Example run"},
            }
        }
    )

    output = parsed.to_dict()["output"]
    assert output["diagnostics"] is True
    assert output["global_attributes"] == {"title": "Example run"}
    assert config.CeceConfig.from_dict({"output": output}).to_dict()["output"] == output


def test_unknown_and_legacy_flat_vertical_keys_are_rejected():
    invalid = {
        "species": {"CO": [{"field": "CO", "operation": "add", "vdist_method": "pbl"}]},
        "driver": {"nx": 4},
    }

    with pytest.raises(ValueError) as error:
        config.CeceConfig.from_dict(invalid)

    message = str(error.value)
    assert "vdist_method" in message
    assert "driver" in message and "unknown key 'nx'" in message


def test_scheme_mappings_must_be_nested_under_options():
    with pytest.raises(ValueError, match="put 'input_mapping' under 'options'"):
        config.CeceConfig.from_dict(
            {"physics_schemes": [{"name": "megan", "input_mapping": {}}]}
        )


def test_temporal_cycle_references_and_lengths_are_validated():
    invalid = valid_config()
    invalid["temporal_profiles"]["traffic"] = [1.0] * 23

    with pytest.raises(ValueError, match="diurnal_cycle.*must have 24 factors"):
        config.CeceConfig.from_dict(invalid)

    programmatic = config.CeceConfig()
    programmatic.add_species(
        "CO",
        [config.EmissionLayer(field_name="CO", diurnal_cycle="missing")],
    )
    result = programmatic.validate()
    assert not result.is_valid
    assert "undefined cycle 'missing'" in str(result)


def test_stream_enum_types_fail_as_configuration_errors():
    invalid = {
        "cece_data": {"streams": [{"name": "CO", "file": "co.nc", "cadence": []}]}
    }

    with pytest.raises(
        ValueError, match=r"cece_data.streams\[0\].*cadence must be a non-empty string"
    ):
        config.CeceConfig.from_dict(invalid)


def test_mapalgo_is_normalized_to_lowercase():
    parsed = config.CeceConfig.from_dict(
        {
            "cece_data": {
                "streams": [{"name": "CO", "file": "co.nc", "mapalgo": "BILINEAR"}]
            }
        }
    )

    assert parsed.cece_data["streams"][0].mapalgo == "bilinear"
