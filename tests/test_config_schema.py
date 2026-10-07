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
    parsed = config.CeceConfig.from_dict({
        "driver": {
            "log_file": "cece.log",
            "grid": {"grid_name": "F360", "nz": 72},
        }
    })

    dumped = parsed.to_dict()["driver"]
    assert dumped["log_file"] == "cece.log"
    assert dumped["grid"]["grid_name"] == "F360"
    assert config.CeceConfig.from_dict({"driver": dumped}).to_dict()["driver"] == dumped


def test_output_global_attributes_round_trip():
    parsed = config.CeceConfig.from_dict({
        "output": {
            "global_attributes": {"title": "Example run"},
        }
    })

    output = parsed.to_dict()["output"]
    assert output["global_attributes"] == {"title": "Example run"}
    assert config.CeceConfig.from_dict({"output": output}).to_dict()["output"] == output


def test_output_diagnostics_is_rejected_as_unsupported():
    with pytest.raises(ValueError, match="output: unknown key 'diagnostics'"):
        config.CeceConfig.from_dict({"output": {"diagnostics": True}})


def test_stream_defaults_match_runtime_and_are_omitted_when_serialized():
    parsed = config.CeceConfig.from_dict({
        "cece_data": {
            "streams": [
                {
                    "name": "CO",
                    "file": "co.nc",
                    "variables": [{"file": "CO_FILE", "model": "CO"}],
                }
            ]
        }
    })

    stream = parsed.cece_data["streams"][0]
    assert (stream.mapalgo, stream.tintalgo) == ("consd", "nearest")
    assert (stream.yearFirst, stream.yearLast, stream.yearAlign) == (0, 0, 0)
    serialized = parsed.to_dict()["cece_data"]["streams"][0]
    assert serialized == {
        "name": "CO",
        "file": "co.nc",
        "variables": [{"file": "CO_FILE", "model": "CO"}],
    }
    assert config.CeceConfig.from_dict(parsed.to_dict()).to_dict() == parsed.to_dict()


def test_stream_variables_default_to_the_stream_name():
    for variables in (None, []):
        stream = {"name": "CO", "file": "co.nc"}
        if variables is not None:
            stream["variables"] = variables
        parsed = config.CeceConfig.from_dict({"cece_data": {"streams": [stream]}})
        mapping = parsed.cece_data["streams"][0].variables[0]
        assert (mapping.file, mapping.model) == ("CO", "CO")

    programmatic = config.CeceConfig()
    programmatic.add_data_stream("CO", "co.nc")
    mapping = programmatic.cece_data["streams"][0].variables[0]
    assert (mapping.file, mapping.model) == ("CO", "CO")


def test_variable_mapping_requires_model_name():
    with pytest.raises(ValueError, match="missing required key 'model'"):
        config.CeceConfig.from_dict({
            "cece_data": {
                "streams": [
                    {
                        "name": "CO",
                        "file": "co.nc",
                        "variables": [{"file": "CO_FILE"}],
                    }
                ]
            }
        })


def test_variable_mapping_defaults_file_name_to_model_name():
    parsed = config.DataVariableConfig.from_dict({"model": "CO"})

    assert parsed.file == "CO"
    assert parsed.model == "CO"


def test_interpolation_alias_is_rejected():
    with pytest.raises(ValueError, match="unknown key 'interpolation'"):
        config.CeceConfig.from_dict({
            "cece_data": {
                "streams": [
                    {
                        "name": "CO",
                        "file": "co.nc",
                        "variables": ["CO"],
                        "interpolation": "linear",
                    }
                ]
            }
        })


def test_global_attribute_values_reject_control_characters():
    with pytest.raises(ValueError, match="must not contain control characters"):
        config.CeceConfig.from_dict({
            "output": {"global_attributes": {"title": "bad\nvalue"}}
        })


def test_global_attributes_accept_scalars_and_preserve_types():
    attributes = {"id": 42, "geospatial_lat_min": -90.5, "summary": True}
    parsed = config.CeceConfig.from_dict({"output": {"global_attributes": attributes}})

    assert parsed.to_dict()["output"]["global_attributes"] == attributes


@pytest.mark.parametrize("value", [None, ["a"], {"a": "b"}, float("nan")])
def test_global_attributes_reject_non_scalar_or_non_finite_values(value):
    with pytest.raises(ValueError, match=r"global_attributes\.title must be"):
        config.CeceConfig.from_dict({"output": {"global_attributes": {"title": value}}})


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
        config.CeceConfig.from_dict({
            "physics_schemes": [{"name": "megan", "input_mapping": {}}]
        })


def test_temporal_cycle_references_and_lengths_are_validated():
    invalid = valid_config()
    invalid["temporal_profiles"]["traffic"] = [1.0] * 23

    with pytest.raises(ValueError, match=r"diurnal_cycle.*must have 24 factors"):
        config.CeceConfig.from_dict(invalid)

    programmatic = config.CeceConfig()
    programmatic.add_species(
        "CO",
        [config.EmissionLayer(field_name="CO", diurnal_cycle="missing")],
    )
    result = programmatic.validate()
    assert not result.is_valid
    assert "undefined cycle 'missing'" in str(result)


def _local_time_config(enabled):
    return {
        "local_time": {"enabled": enabled, "grid_file": "data/utc_grid_720r.rle"},
        "species": {
            "CO": [{"field": "CO", "operation": "add", "use_local_time": True}]
        },
    }


def test_local_time_round_trips():
    parsed = config.CeceConfig.from_dict(_local_time_config(True))

    assert parsed.local_time.enabled
    assert parsed.species["CO"][0].use_local_time
    dumped = parsed.to_dict()
    assert dumped["local_time"] == {
        "enabled": True,
        "grid_file": "data/utc_grid_720r.rle",
    }
    assert config.CeceConfig.from_dict(dumped).to_dict() == dumped


def test_local_time_defaults_are_omitted():
    dumped = config.CeceConfig.from_dict({
        "species": {"CO": [{"field": "CO", "operation": "add"}]}
    }).to_dict()

    assert "local_time" not in dumped
    assert "use_local_time" not in dumped["species"]["CO"][0]


def test_use_local_time_requires_enabled_local_time():
    with pytest.raises(ValueError, match=r"local_time\.enabled is false"):
        config.CeceConfig.from_dict(_local_time_config(False))


def test_local_time_rejects_unknown_keys():
    with pytest.raises(ValueError, match="local_time: unknown key 'grid'"):
        config.CeceConfig.from_dict({"local_time": {"grid": "x.rle"}})


def test_stream_enum_types_fail_as_configuration_errors():
    invalid = {
        "cece_data": {
            "streams": [
                {"name": "CO", "file": "co.nc", "variables": ["CO"], "cadence": []}
            ]
        }
    }

    with pytest.raises(
        ValueError, match=r"cece_data.streams\[0\].*cadence must be a non-empty string"
    ):
        config.CeceConfig.from_dict(invalid)


def test_mapalgo_is_normalized_to_lowercase():
    parsed = config.CeceConfig.from_dict({
        "cece_data": {
            "streams": [
                {
                    "name": "CO",
                    "file": "co.nc",
                    "variables": ["CO"],
                    "mapalgo": "BILINEAR",
                }
            ]
        }
    })

    assert parsed.cece_data["streams"][0].mapalgo == "bilinear"
