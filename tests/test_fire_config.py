import copy
import importlib.util
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
