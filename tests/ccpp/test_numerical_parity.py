"""Compare actual generated CCPP calls against Python over the same native kernels."""

import json
import subprocess
from pathlib import Path

import cece
import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "build/src/driver/ccpp/tests/test_cece_ccpp_suite"


@pytest.mark.skipif(
    not EXE.exists(), reason="build the real CCPP generated-suite target first"
)
def test_generated_ccpp_fluxes_match_python(tmp_path):
    output = tmp_path / "flux.txt"
    subprocess.run(
        [str(EXE), str(ROOT), str(output)], check=True, capture_output=True, text=True
    )
    reference = np.loadtxt(output)
    manifest = json.loads(
        (ROOT / "tests/data/interfaces/megan3-cb6-species-v1.json").read_text()
    )
    assert reference.shape == (len(manifest["species"]) + 1, 3)
    forcing = {
        name: np.full(3, value)
        for name, value in {
            "temperature": 303.0,
            "leaf_area_index": 3.0,
            "leaf_area_index_prev": 3.0,
            "par_direct": 300.0,
            "par_diffuse": 100.0,
            "solar_cosine": 0.8,
            "soil_moisture_root": 0.2,
            "wind_speed": 2.0,
            "soil_temperature": 303.0,
            "soil_moisture": 0.2,
        }.items()
    }
    with cece.Model(
        ["bdsnp", "megan3"],
        grid=cece.Grid.columns([0, 1, 2], [30, 31, 32]),
        options={"bdsnp": {"soil_no_method": "yl95"}},
    ) as model:
        model.set_inputs(forcing)
        result = model.step(time=0, dt=3600)
    for i, name in enumerate(manifest["species"]):
        np.testing.assert_allclose(
            result[name], reference[i], rtol=2e-12, atol=1e-27, err_msg=name
        )
    np.testing.assert_allclose(
        result.soil_nox_emissions, reference[-1], rtol=2e-12, atol=1e-27
    )
