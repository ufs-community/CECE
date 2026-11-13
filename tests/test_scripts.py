import os
import subprocess
import sys

# Add scripts directory to path to test functions directly if needed
sys.path.insert(
    0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "scripts"))
)


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
    script = os.path.abspath(
        os.path.join(
            os.path.dirname(__file__), "..", "scripts", "download_hemco_data.py"
        )
    )

    # Test help message
    result = subprocess.run(
        [sys.executable, script, "--help"], capture_output=True, text=True, check=False
    )
    assert result.returncode == 0
    assert "Download HEMCO data" in result.stdout


def test_hemco_to_cece_cli_error():
    script = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "scripts", "hemco_to_cece.py")
    )

    # Test missing argument
    result = subprocess.run(
        [sys.executable, script], capture_output=True, text=True, check=False
    )
    assert result.returncode != 0
    assert "the following arguments are required" in result.stderr


def test_visualize_stack_cli(tmp_path):
    script = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "scripts", "visualize_stack.py")
    )

    # Create a dummy config
    config = tmp_path / "test_config.yaml"
    config.write_text(
        "species:\n  NO2:\n    - operation: add\n      hierarchy: 1\n      field: f1\n"
    )

    # Test CLI execution
    # Note: we might want to mock matplotlib to avoid GUI/window issues if it was a real environment,
    # but here it's likely headless. We'll just check if it runs without error.
    result = subprocess.run(
        [sys.executable, script, str(config)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0
    assert "--- Stacking Plan for NO2 ---" in result.stdout
    assert "Saved stacking plan visualization" in result.stdout


# ── scripts/build-and-test-container.py ──────────────────────────────────────
# The module name is hyphenated, so it is loaded by path rather than imported.
# Nothing here runs docker: the build phase's bash command is a pure function
# and the docker invocation is captured by replacing subprocess.check_call.


def _load_build_script():
    import importlib.util
    from pathlib import Path

    path = (
        Path(__file__).resolve().parent.parent
        / "scripts"
        / "build-and-test-container.py"
    )
    spec = importlib.util.spec_from_file_location("build_and_test_container", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_build_command_default_builds_all_at_bounded_jobs():
    btc = _load_build_script()
    command = btc.build_command("/work", None, 8)
    configure, build = command.split(" && ")
    assert configure.startswith("[ -f /work/build/CMakeCache.txt ] || cmake -S /work")
    assert "-DCECE_MPIEXEC_CONTAINER_FLAGS=ON" in configure
    assert build == "cmake --build /work/build -j 8"
    assert "--target" not in command


def test_build_command_single_and_repeated_targets():
    btc = _load_build_script()
    single = btc.build_command("/work", ["cece_standalone_driver"], 4)
    assert single.endswith(
        "cmake --build /work/build -j 4 --target cece_standalone_driver"
    )
    double = btc.build_command("/work", ["a", "b"], 4)
    assert double.endswith("cmake --build /work/build -j 4 --target a --target b")


def test_parse_args_defaults(monkeypatch):
    btc = _load_build_script()
    monkeypatch.setattr(sys, "argv", ["build-and-test-container.py"])
    args = btc.parse_args()
    assert args.target is None
    assert args.jobs == (os.cpu_count() or 1)
    assert args.mount == "/work"
    assert args.image == "cece/cece-dev"
    assert not args.no_build and not args.no_test and not args.clean


def test_parse_args_target_repeatable_and_jobs(monkeypatch):
    btc = _load_build_script()
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "build-and-test-container.py",
            "--target",
            "a",
            "--target",
            "b",
            "--jobs",
            "4",
        ],
    )
    args = btc.parse_args()
    assert args.target == ["a", "b"]
    assert args.jobs == 4


def test_parse_args_rejects_non_positive_jobs(monkeypatch):
    import pytest

    btc = _load_build_script()
    for bad in ("0", "-1"):
        monkeypatch.setattr(sys, "argv", ["build-and-test-container.py", "--jobs", bad])
        with pytest.raises(SystemExit):
            btc.parse_args()


def test_build_passes_command_to_container(monkeypatch):
    btc = _load_build_script()
    calls = []
    monkeypatch.setattr(btc.subprocess, "check_call", calls.append)

    btc.build("img:tag", "/work", ["cece_standalone_driver"], 4)

    assert len(calls) == 1
    argv = calls[0]
    assert argv[:3] == ["docker", "run", "--rm"]
    assert f"{btc.REPO_ROOT}:/work" in argv
    assert argv[-4:-1] == ["img:tag", "/bin/bash", "-c"]
    assert argv[-1] == btc.build_command("/work", ["cece_standalone_driver"], 4)


def test_plot_utc_grid_polygons_tile_the_globe():
    import numpy as np
    import plot_utc_grid
    import pytest

    def area(poly):
        x, y = poly[:, 0], poly[:, 1]
        return 0.5 * abs(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))

    # A 1 deg run is the narrowest case (it once came out as a triangle).
    for west, east in [(0.0, 1.0), (0.0, 10.0), (-180.0, 180.0)]:
        poly = plot_utc_grid.densified_run(west, east, 0.0, 1.0)
        assert area(poly) == pytest.approx(east - west)

    # Two 90 deg rows: three equal-offset runs that must cover the whole 360x180 map.
    ncols = np.array([4, 4])
    cells = np.array([0, 0, 4, 4, 8, 8, 8, 8], dtype=np.int8)
    polygons, hours = plot_utc_grid.build_merged_polygons(ncols, cells)
    assert hours.tolist() == [0.0, 1.0, 2.0]
    assert sum(area(p) for p in polygons) == pytest.approx(360.0 * 180.0)


def test_local_weekday_checker_rejects_nan_and_masked_output(tmp_path):
    import numpy as np
    import pytest

    pytest.importorskip("netCDF4")
    import stream_local_weekday_helper as helper

    def check_with(name, values):
        out_dir = tmp_path / name
        out_dir.mkdir()
        with helper.Dataset(out_dir / "wk_20260112_210000.nc", "w") as ds:
            ds.createDimension("lat", helper.NY)
            ds.createDimension("lon", helper.NX)
            lat = ds.createVariable("lat", "f8", ("lat",))
            lon = ds.createVariable("lon", "f8", ("lon",))
            lat[:] = -90.0 + (np.arange(helper.NY) + 0.5) * 180.0 / helper.NY
            lon[:] = -180.0 + (np.arange(helper.NX) + 0.5) * 360.0 / helper.NX
            ds.createVariable("wk", "f8", ("lat", "lon"), fill_value=-999.0)[:] = values
        return helper.check(str(out_dir), "utc")

    shape = (helper.NY, helper.NX)
    assert check_with("good", np.full(shape, 1.0)) == 0
    assert check_with("nan", np.full(shape, np.nan)) == 1
    assert check_with("masked", np.ma.masked_all(shape)) == 1
