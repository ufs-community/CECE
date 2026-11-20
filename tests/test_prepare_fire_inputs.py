import hashlib
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "prepare_fire_inputs.py"


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _remote_source(source, source_id, digest):
    remote = dict(source, source_id=source_id, content_sha256=digest, uri=f"https://example.test/{source_id}.nc")
    remote.pop("local_path")
    return remote


def _write_inputs(tmp_path, *, source_entries=None, fire_options=None, schedule=None, profile=None):
    raw = tmp_path / "source.bin"
    raw.write_bytes(b"synthetic fire source")
    source = {
        "source_id": "source-a",
        "adapter_id": "synthetic-product-v1",
        "product_version": "product-v1",
        "algorithm_version": "algorithm-v1",
        "acquired_at": "2022-07-01T00:00:00Z",
        "available_at": "2022-07-01T01:00:00Z",
        "local_path": raw.name,
        "content_sha256": _sha256(raw),
        "product_id": "synthetic-product",
        "variant": "unscaled",
        "chemistry_state": "bulk",
        "interval_start": "2022-07-01T00:00:00Z",
        "interval_end": "2022-07-01T01:00:00Z",
        "temporal_resolution_seconds": 3600,
    }
    manifest_path = tmp_path / "sources.yaml"
    manifest_path.write_text(
        yaml.safe_dump(
            {
                "schema_version": 1,
                "sources": source_entries if source_entries is not None else [source],
            },
            sort_keys=True,
        )
    )
    options = {
        "mode": "product",
        "input_provider": "prepared",
        "source_manifest": manifest_path.name,
        "input_manifest": "fire-output/manifest.yaml",
        "product_adapter": "synthetic-product-v1",
        "product_time_allocation": "source_resolved",
        "mechanism_file": "profiles/species.yaml",
        "fire_mapping_file": "profiles/mapping.yaml",
        "target_id": "CB6",
        "target_version": "synthetic-v1",
        "cutoff_mode": "fixed",
        "availability_cutoff": "2022-07-02T00:00:00Z",
    }
    options.update(fire_options or {})
    if options["cutoff_mode"] == "host_supplied":
        options.pop("availability_cutoff", None)
    config = {
        "driver": {
            "start_time": "2022-07-01T00:00:00Z",
            "end_time": "2022-07-01T01:00:00Z",
            "timestep_seconds": 3600,
        },
        "physics_schemes": [{"name": "fire", "options": options}],
    }
    config_path = tmp_path / "config.yaml"
    config_path.write_text(yaml.safe_dump(config, sort_keys=True))
    if schedule is not None:
        (tmp_path / "cutoffs.yaml").write_text(yaml.safe_dump(schedule, sort_keys=True))
    if profile is not None:
        (tmp_path / "profile.yaml").write_text(yaml.safe_dump(profile, sort_keys=True))
    return config_path, options, source


def _run(tmp_path, config_path, *extra):
    output = tmp_path / "fire-output"
    return subprocess.run(
        [sys.executable, str(SCRIPT), "--config", str(config_path), "--output-dir", str(output), *extra],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        check=False,
    )


def test_preflight_selects_higher_priority_compatible_source_without_output(tmp_path):
    config_path, _, first = _write_inputs(tmp_path)
    first["priority"] = 1
    preferred = _remote_source(first, "source-b", "b" * 64)
    preferred["priority"] = 2
    config_path, _, _ = _write_inputs(tmp_path, source_entries=[first, preferred])

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 0, result.stderr
    assert "source-b" in result.stdout
    assert not (tmp_path / "fire-output").exists()


def test_preflight_does_not_require_normalized_output_to_exist(tmp_path):
    config_path, _, _ = _write_inputs(tmp_path)

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 0, result.stderr
    assert not (tmp_path / "fire-output" / "manifest.yaml").exists()


def test_preflight_never_fetches_remote_sources(tmp_path):
    config_path, _, source = _write_inputs(tmp_path)
    remote = _remote_source(source, "remote-source", "d" * 64)
    remote["uri"] = "https://unreachable.invalid/fire.nc"
    config_path, _, _ = _write_inputs(tmp_path, source_entries=[remote])

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 0, result.stderr
    assert "remote-source" in result.stdout


def test_local_source_read_failure_uses_operational_exit_code(tmp_path):
    config_path, _, _ = _write_inputs(tmp_path)
    (tmp_path / "source.bin").unlink()

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 1
    assert "cannot read local source" in result.stderr


def test_source_manifest_cannot_resolve_inside_output_directory(tmp_path):
    output = tmp_path / "fire-output"
    output.mkdir()
    config_path, options, _ = _write_inputs(tmp_path)
    options["source_manifest"] = "fire-output/manifest.yaml"
    (output / "manifest.yaml").write_text("schema_version: 1\nsources: []\n")
    config = yaml.safe_load(config_path.read_text())
    config["physics_schemes"][0]["options"] = options
    config_path.write_text(yaml.safe_dump(config))

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 2
    assert "source_manifest" in result.stderr


def test_conflicting_equal_priority_overlapping_revisions_are_rejected(tmp_path):
    config_path, _, first = _write_inputs(tmp_path)
    first["priority"] = 3
    second = _remote_source(first, "source-b", "c" * 64)
    config_path, _, _ = _write_inputs(tmp_path, source_entries=[first, second])

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 2
    assert "priority" in result.stderr.lower()


def test_profile_rejects_already_resolved_or_incompatible_source_cadence(tmp_path):
    config_path, _, source = _write_inputs(
        tmp_path,
        fire_options={"product_time_allocation": "profile", "product_time_profile": "profile.yaml"},
        profile={
            "schema_version": 1,
            "profile_id": "hourly",
            "version": "1",
            "time_basis": "UTC",
            "supported_source_cadence_seconds": 86400,
            "weights": [{"offset_seconds": 0, "duration_seconds": 86400, "weight_per_second": 1 / 86400}],
        },
    )
    source["temporal_resolution_seconds"] = 3600
    config_path, _, _ = _write_inputs(
        tmp_path,
        source_entries=[source],
        fire_options={"product_time_allocation": "profile", "product_time_profile": "profile.yaml"},
        profile={
            "schema_version": 1,
            "profile_id": "hourly",
            "version": "1",
            "time_basis": "UTC",
            "supported_source_cadence_seconds": 86400,
            "weights": [{"offset_seconds": 0, "duration_seconds": 86400, "weight_per_second": 1 / 86400}],
        },
    )

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 2
    assert "cadence" in result.stderr.lower()


def test_host_supplied_cutoff_schedule_must_cover_every_model_interval(tmp_path):
    schedule = {
        "schema_version": 1,
        "intervals": [
            {
                "interval_start": "2022-07-01T00:00:00Z",
                "interval_end": "2022-07-01T00:30:00Z",
                "availability_cutoff": "2022-07-01T01:00:00Z",
            }
        ],
    }
    config_path, _, _ = _write_inputs(
        tmp_path,
        fire_options={"cutoff_mode": "host_supplied", "cutoff_schedule": "cutoffs.yaml"},
        schedule=schedule,
    )

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 2
    assert "cutoff" in result.stderr.lower()


def test_host_supplied_cutoff_schedule_accepts_complete_interval_coverage(tmp_path):
    schedule = {
        "schema_version": 1,
        "intervals": [
            {
                "interval_start": "2022-07-01T00:00:00Z",
                "interval_end": "2022-07-01T01:00:00Z",
                "availability_cutoff": "2022-07-01T01:00:00Z",
            }
        ],
    }
    config_path, _, _ = _write_inputs(
        tmp_path,
        fire_options={"cutoff_mode": "host_supplied", "cutoff_schedule": "cutoffs.yaml"},
        schedule=schedule,
    )

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 0, result.stderr


def test_mismatched_existing_manifest_requires_explicit_overwrite(tmp_path):
    config_path, _, _ = _write_inputs(tmp_path)
    output = tmp_path / "fire-output"
    output.mkdir()
    (output / "manifest.yaml").write_text(
        yaml.safe_dump({"schema_version": 1, "source_manifest_sha256": "0" * 64})
    )

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 2
    assert "manifest" in result.stderr.lower()

    overwritten_preflight = _run(tmp_path, config_path, "--preflight-only", "--overwrite")
    assert overwritten_preflight.returncode == 0, overwritten_preflight.stderr
    assert yaml.safe_load((output / "manifest.yaml").read_text()) == {
        "schema_version": 1,
        "source_manifest_sha256": "0" * 64,
    }


def test_live_preparation_refuses_unverified_adapter_and_preserves_output(tmp_path):
    config_path, _, _ = _write_inputs(tmp_path)

    result = _run(tmp_path, config_path)

    assert result.returncode == 2
    assert "decoder" in result.stderr.lower()
    assert not (tmp_path / "fire-output" / "manifest.yaml").exists()


def test_atomic_manifest_writer_preserves_old_file_on_replace_failure(tmp_path):
    sys.path.insert(0, str(ROOT / "scripts"))
    try:
        import prepare_fire_inputs
    finally:
        sys.path.pop(0)

    target = tmp_path / "manifest.yaml"
    target.write_text("old: true\n")
    original_replace = prepare_fire_inputs.os.replace

    def fail_replace(source, destination):
        raise OSError("synthetic replace failure")

    prepare_fire_inputs.os.replace = fail_replace
    try:
        try:
            prepare_fire_inputs._atomic_write_yaml(target, {"new": True}, overwrite=True)
        except OSError as error:
            assert "synthetic replace failure" in str(error)
        else:
            raise AssertionError("atomic write should surface replace failures")
    finally:
        prepare_fire_inputs.os.replace = original_replace

    assert yaml.safe_load(target.read_text()) == {"old": True}
    assert sorted(path.name for path in tmp_path.iterdir()) == ["manifest.yaml"]


def test_overwrite_flag_is_required_before_replacing_existing_manifest(tmp_path):
    sys.path.insert(0, str(ROOT / "scripts"))
    try:
        import prepare_fire_inputs
    finally:
        sys.path.pop(0)

    target = tmp_path / "manifest.yaml"
    prepare_fire_inputs._atomic_write_yaml(target, {"version": 1}, overwrite=False)
    with pytest.raises(FileExistsError):
        prepare_fire_inputs._atomic_write_yaml(target, {"version": 2}, overwrite=False)
    prepare_fire_inputs._atomic_write_yaml(target, {"version": 2}, overwrite=True)
    assert yaml.safe_load(target.read_text()) == {"version": 2}
