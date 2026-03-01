import os
import subprocess
import yaml
import pytest
import sys

def test_hemco_to_aces_conversion(tmp_path):
    # Use paths relative to this test file
    test_dir = os.path.dirname(__file__)
    hemco_rc = os.path.join(test_dir, "test_hemco.rc")
    aces_yaml = tmp_path / "aces_config_converted.yaml"
    script = os.path.join(test_dir, "..", "scripts", "hemco_to_aces.py")

    # Run conversion using sys.executable
    result = subprocess.run([sys.executable, script, hemco_rc, "-o", str(aces_yaml)],
                            capture_output=True, text=True)

    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)

    assert result.returncode == 0
    assert "Successfully converted" in result.stdout

    # Verify content
    with open(aces_yaml, 'r') as f:
        config = yaml.safe_load(f)

    assert "species" in config
    assert "co" in config["species"]
    assert len(config["species"]["co"]) >= 2

    # Check for specific layer attributes
    layers = config["species"]["co"]
    maccity = next(l for l in layers if l["field"] == "MACCITY_CO")
    assert maccity["hierarchy"] == 1
    assert maccity["weekly_cycle"] == "geia_dow"
    assert "hourly_scalfact" in maccity["scale_fields"]

    # Check temporal profiles
    assert "temporal_profiles" in config
    assert "geia_dow" in config["temporal_profiles"]
    assert len(config["temporal_profiles"]["geia_dow"]) == 7

    # Check diagnostics
    assert "diagnostics" in config
    assert config["diagnostics"]["nx"] == 720
    assert config["diagnostics"]["ny"] == 360
    assert "EmisNO_Total" in config["diagnostics"]["variables"]

    # Check ROOT replacement (our test rc has ROOT: data)
    stream = next(s for s in config["cdeps_inline_config"]["streams"] if s["name"] == "HOURLY_SCALFACT")
    assert stream["file"] == "data/hourly.nc"
