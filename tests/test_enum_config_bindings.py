"""
Tests for pybind11 enum and config bindings (_aces_core module).

Tests enum value access, ParseConfig, and AddSpecies/AddScaleFactor/AddMask.
"""

import os
import sys
import tempfile
import pytest

# Add the build output directory to the path so _aces_core can be imported
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "build", "src", "python", "aces"))

import _aces_core


class TestVerticalDistributionMethodEnum:
    """Tests for VerticalDistributionMethod enum binding."""

    def test_single_value(self):
        assert _aces_core.VerticalDistributionMethod.SINGLE is not None

    def test_range_value(self):
        assert _aces_core.VerticalDistributionMethod.RANGE is not None

    def test_pressure_value(self):
        assert _aces_core.VerticalDistributionMethod.PRESSURE is not None

    def test_height_value(self):
        assert _aces_core.VerticalDistributionMethod.HEIGHT is not None

    def test_pbl_value(self):
        assert _aces_core.VerticalDistributionMethod.PBL is not None

    def test_enum_values_are_distinct(self):
        values = [
            _aces_core.VerticalDistributionMethod.SINGLE,
            _aces_core.VerticalDistributionMethod.RANGE,
            _aces_core.VerticalDistributionMethod.PRESSURE,
            _aces_core.VerticalDistributionMethod.HEIGHT,
            _aces_core.VerticalDistributionMethod.PBL,
        ]
        assert len(set(values)) == 5


class TestVerticalCoordTypeEnum:
    """Tests for VerticalCoordType enum binding."""

    def test_none_value(self):
        assert _aces_core.VerticalCoordType.NONE is not None

    def test_fv3_value(self):
        assert _aces_core.VerticalCoordType.FV3 is not None

    def test_mpas_value(self):
        assert _aces_core.VerticalCoordType.MPAS is not None

    def test_wrf_value(self):
        assert _aces_core.VerticalCoordType.WRF is not None

    def test_enum_values_are_distinct(self):
        values = [
            _aces_core.VerticalCoordType.NONE,
            _aces_core.VerticalCoordType.FV3,
            _aces_core.VerticalCoordType.MPAS,
            _aces_core.VerticalCoordType.WRF,
        ]
        assert len(set(values)) == 4


class TestEmissionLayer:
    """Tests for EmissionLayer binding."""

    def test_create_default(self):
        layer = _aces_core.EmissionLayer()
        assert layer is not None

    def test_readwrite_operation(self):
        layer = _aces_core.EmissionLayer()
        layer.operation = "add"
        assert layer.operation == "add"

    def test_readwrite_field_name(self):
        layer = _aces_core.EmissionLayer()
        layer.field_name = "co_emis"
        assert layer.field_name == "co_emis"

    def test_readwrite_scale(self):
        layer = _aces_core.EmissionLayer()
        assert layer.scale == 1.0  # default
        layer.scale = 2.5
        assert layer.scale == 2.5

    def test_readwrite_hierarchy(self):
        layer = _aces_core.EmissionLayer()
        layer.hierarchy = 3
        assert layer.hierarchy == 3

    def test_readwrite_masks(self):
        layer = _aces_core.EmissionLayer()
        layer.masks = ["mask1", "mask2"]
        assert layer.masks == ["mask1", "mask2"]

    def test_readwrite_vdist_method(self):
        layer = _aces_core.EmissionLayer()
        assert layer.vdist_method == _aces_core.VerticalDistributionMethod.SINGLE
        layer.vdist_method = _aces_core.VerticalDistributionMethod.PRESSURE
        assert layer.vdist_method == _aces_core.VerticalDistributionMethod.PRESSURE

    def test_readwrite_vdist_layer_range(self):
        layer = _aces_core.EmissionLayer()
        layer.vdist_layer_start = 1
        layer.vdist_layer_end = 5
        assert layer.vdist_layer_start == 1
        assert layer.vdist_layer_end == 5

    def test_readwrite_vdist_pressure_range(self):
        layer = _aces_core.EmissionLayer()
        layer.vdist_p_start = 100000.0
        layer.vdist_p_end = 50000.0
        assert layer.vdist_p_start == 100000.0
        assert layer.vdist_p_end == 50000.0

    def test_readwrite_vdist_height_range(self):
        layer = _aces_core.EmissionLayer()
        layer.vdist_h_start = 0.0
        layer.vdist_h_end = 1000.0
        assert layer.vdist_h_start == 0.0
        assert layer.vdist_h_end == 1000.0


class TestParseConfig:
    """Tests for ParseConfig function."""

    def test_parse_valid_config(self):
        config_path = os.path.join(
            os.path.dirname(__file__), "..", "examples", "aces_config_ex1.yaml"
        )
        config = _aces_core.ParseConfig(config_path)
        assert config is not None
        assert isinstance(config, _aces_core.AcesConfig)
        # ex1 has a "co" species
        assert "co" in config.species_layers

    def test_parse_invalid_path_raises(self):
        from exceptions import AcesException
        with pytest.raises(AcesException):
            _aces_core.ParseConfig("/nonexistent/path/config.yaml")


class TestAcesConfig:
    """Tests for AcesConfig binding."""

    def test_create_default(self):
        config = _aces_core.AcesConfig()
        assert config is not None
        assert len(config.species_layers) == 0
        assert len(config.met_mapping) == 0

    def test_readwrite_met_mapping(self):
        config = _aces_core.AcesConfig()
        config.met_mapping = {"T": "temperature", "P": "pressure"}
        assert config.met_mapping["T"] == "temperature"
        assert config.met_mapping["P"] == "pressure"

    def test_readwrite_scale_factor_mapping(self):
        config = _aces_core.AcesConfig()
        config.scale_factor_mapping = {"sf1": "scale_field_1"}
        assert config.scale_factor_mapping["sf1"] == "scale_field_1"

    def test_readwrite_mask_mapping(self):
        config = _aces_core.AcesConfig()
        config.mask_mapping = {"canada": "Canada_mask"}
        assert config.mask_mapping["canada"] == "Canada_mask"


class TestAddSpecies:
    """Tests for AddSpecies function."""

    def test_add_species_mutates_config(self):
        config = _aces_core.AcesConfig()
        layer = _aces_core.EmissionLayer()
        layer.operation = "add"
        layer.field_name = "no_emis"
        layer.scale = 1.0
        _aces_core.AddSpecies(config, "NO", [layer])
        assert "NO" in config.species_layers
        assert len(config.species_layers["NO"]) == 1
        assert config.species_layers["NO"][0].field_name == "no_emis"


class TestAddScaleFactor:
    """Tests for AddScaleFactor function."""

    def test_add_scale_factor_mutates_config(self):
        config = _aces_core.AcesConfig()
        _aces_core.AddScaleFactor(config, "pop_density", "POPULATION")
        assert config.scale_factor_mapping["pop_density"] == "POPULATION"


class TestAddMask:
    """Tests for AddMask function."""

    def test_add_mask_mutates_config(self):
        config = _aces_core.AcesConfig()
        _aces_core.AddMask(config, "canada", "Canada_mask")
        assert config.mask_mapping["canada"] == "Canada_mask"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
