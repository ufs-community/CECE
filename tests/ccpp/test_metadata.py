"""Validate shipped caps with the exact framework selected by UFS."""

import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
FRAMEWORK = os.environ.get("CCPP_FRAMEWORK")


@pytest.mark.skipif(
    not FRAMEWORK, reason="set CCPP_FRAMEWORK to the pinned UFS framework checkout"
)
@pytest.mark.parametrize(
    "scheme", ["cece_native_example", "cece_megan3", "cece_bdsnp_yl95"]
)
def test_metadata_matches_fortran(scheme):
    result = subprocess.run(
        [
            sys.executable,
            str(Path(FRAMEWORK) / "capgen/ccpp_validator.py"),
            "--scheme-files",
            str(ROOT / "src/driver/ccpp" / (scheme + ".meta")),
            "--source-files",
            str(ROOT / "src/driver/ccpp" / (scheme + ".F90")),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
