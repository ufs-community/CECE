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

def test_hemco_to_cece_cli_error():
    script = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'scripts', 'hemco_to_cece.py'))

    # Test missing argument
    result = subprocess.run([sys.executable, script], capture_output=True, text=True)
    assert result.returncode != 0
    assert "the following arguments are required" in result.stderr

def test_visualize_stack_cli(tmp_path):
    script = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'scripts', 'visualize_stack.py'))

    # Create a dummy config
    config = tmp_path / "test_config.yaml"
    config.write_text("species:\n  NO2:\n    - operation: add\n      hierarchy: 1\n      field: f1\n")

    # Test CLI execution
    # Note: we might want to mock matplotlib to avoid GUI/window issues if it was a real environment,
    # but here it's likely headless. We'll just check if it runs without error.
    result = subprocess.run([sys.executable, script, str(config)], capture_output=True, text=True)
    assert result.returncode == 0
    assert "--- Stacking Plan for NO2 ---" in result.stdout
    assert "Saved stacking plan visualization" in result.stdout
