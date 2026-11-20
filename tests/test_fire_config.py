import copy
import hashlib
import importlib.util
import re
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "cece_fire_config_schema", ROOT / "src/python/config.py"
)
config = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = config
SPEC.loader.exec_module(config)
FIXTURES = yaml.safe_load((ROOT / "tests/data/fire/config_cases.yaml").read_text())


@pytest.mark.parametrize("case", FIXTURES["cases"], ids=lambda case: case["id"])
def test_shared_fire_configuration_cases(case):
    options = copy.deepcopy(FIXTURES["base_options"])
    options.update(case.get("set", {}))
    for key in case.get("unset", []):
        options.pop(key, None)
    schemes = [{"name": "fire", "options": options}]
    if case.get("duplicate_fire"):
        schemes.append(copy.deepcopy(schemes[0]))
    data = {"physics_schemes": schemes}
    if case["valid"]:
        result = config.CeceConfig.from_dict(data)
        assert result.validate().is_valid
        assert result.to_dict()["physics_schemes"][0]["options"] == options
    else:
        with pytest.raises(ValueError, match=case["error"]):
            config.CeceConfig.from_dict(data)


def test_fire_manifest_aliases_are_rejected(tmp_path):
    source = tmp_path / "source.yaml"
    source.write_text("raw input")
    alias = tmp_path / "alias.yaml"
    alias.symlink_to(source)
    options = dict(
        FIXTURES["base_options"], source_manifest=str(source), input_manifest=str(alias)
    )
    with pytest.raises(ValueError, match="source_manifest"):
        config.PhysicsSchemeConfig(name="fire", options=options).validate()


def test_cb6_factor_manifest_matches_pinned_excerpt_without_enabling_production():
    factor_manifest = yaml.safe_load(
        (ROOT / "data/fire/cb6_fire_factors_manifest.yaml").read_text()
    )
    registry = yaml.safe_load((ROOT / "data/fire/asset_registry.yaml").read_text())
    asset = next(item for item in registry["assets"] if item["asset_id"] == "fire-CB6")
    source_path = ROOT / "data/fire/cb6_fire_config.txt"

    assert factor_manifest["factor_semantics_verified"] is True
    assert factor_manifest["production_eligible"] is False
    assert factor_manifest["factor_verification"]["status"] == "verified"
    assert factor_manifest["mechanism_version"] == "r5"
    assert factor_manifest["mechanism_configuration"] == "cb6r5_ae7_aq"
    assert asset["status"] == "verified"
    assert asset["production_use"] is False
    assert registry["production_support"] is False
    assert hashlib.sha256(source_path.read_bytes()).hexdigest() == asset["content_sha256"]

    source_rows = {"PM2.5": {}, "CO": {}}
    row_pattern = re.compile(
        r"^\s*([A-Z0-9]+)\s+([0-9.]+)\s+(PM2\.5|CO)\s+KG/M2/S\s*$"
    )
    for line in source_path.read_text().splitlines():
        match = row_pattern.match(line)
        if match:
            species, value, base = match.groups()
            source_rows[base][species] = float(value)

    for base, key, expected_count, expected_sum in (
        ("PM2.5", "species_mapping_pm25", 17, 1.0),
        ("CO", "species_mapping_co", 26, 1.1247),
    ):
        factors = factor_manifest[key]["factors"]
        actual = {str(species): float(row["value"]) for species, row in factors.items()}
        assert actual == source_rows[base]
        assert len(actual) == expected_count
        assert sum(actual.values()) == pytest.approx(expected_sum, abs=1e-12)
    assert "NO" in factor_manifest["species_mapping_co"]["factors"]
