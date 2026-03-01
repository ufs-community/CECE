import os
import sys
import subprocess
import pytest

# Add scripts directory to path to test functions directly if needed
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'scripts')))

def test_verify_hemco_data_logic(tmp_path):
    import verify_hemco_data

    # Test valid file
    test_file = tmp_path / "test.nc"
    test_file.write_text("dummy content")
    assert verify_hemco_data.verify_netcdf(str(test_file)) is True

    # Test missing file
    assert verify_hemco_data.verify_netcdf("non_existent.nc") is False

    # Test empty file
    empty_file = tmp_path / "empty.nc"
    empty_file.write_text("")
    assert verify_hemco_data.verify_netcdf(str(empty_file)) is False

def test_download_hemco_data_cli():
    # We don't want to actually download anything in CI unless we have to,
    # but we can test the CLI parsing and path construction.
    script = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'scripts', 'download_hemco_data.py'))

    # Test help message
    result = subprocess.run([sys.executable, script, "--help"], capture_output=True, text=True)
    assert result.returncode == 0
    assert "Download HEMCO data" in result.stdout

def test_hemco_to_aces_cli_error():
    script = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'scripts', 'hemco_to_aces.py'))

    # Test missing argument
    result = subprocess.run([sys.executable, script], capture_output=True, text=True)
    assert result.returncode != 0
    assert "the following arguments are required" in result.stderr
