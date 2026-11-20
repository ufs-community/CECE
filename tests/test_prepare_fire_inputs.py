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


def _write_actual_product_inputs(
    tmp_path, sample_path, *, adapter, product_id, version, start, end, created, run_end, variant="unscaled"
):
    config_path, options, source = _write_inputs(tmp_path)
    sample_path = sample_path.resolve()
    source.update(
        {
            "adapter_id": adapter,
            "product_id": product_id,
            "product_version": version,
            "algorithm_version": version,
            "variant": variant,
            "chemistry_state": "bulk",
            "local_path": str(sample_path),
            "content_sha256": _sha256(sample_path),
            "acquired_at": created,
            "available_at": created,
            "interval_start": start,
            "interval_end": end,
            "temporal_resolution_seconds": 3600 if adapter == "rave-v2r0" else 86400,
        }
    )
    (tmp_path / "sources.yaml").write_text(
        yaml.safe_dump({"schema_version": 1, "sources": [source]}, sort_keys=True)
    )
    config = yaml.safe_load(config_path.read_text())
    options["product_adapter"] = adapter
    options["availability_cutoff"] = "2026-07-03T00:00:00Z" if adapter == "rave-v2r0" else "2023-02-26T00:00:00Z"
    config["physics_schemes"][0]["options"] = options
    config["driver"]["start_time"] = start
    config["driver"]["end_time"] = run_end
    config["driver"]["timestep_seconds"] = 3600
    config_path.write_text(yaml.safe_dump(config, sort_keys=True))
    return config_path


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


def test_preflight_interprets_cece_driver_clock_without_zone_as_utc(tmp_path):
    config_path, _, _ = _write_inputs(tmp_path)
    config = yaml.safe_load(config_path.read_text())
    config["driver"]["start_time"] = "2022-07-01T00:00:00"
    config["driver"]["end_time"] = "2022-07-01T01:00:00"
    config_path.write_text(yaml.safe_dump(config, sort_keys=True))

    result = _run(tmp_path, config_path, "--preflight-only")

    assert result.returncode == 0, result.stderr


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


@pytest.mark.parametrize(
    "filename,adapter,product_id,version,start,end,created,run_end,expected_species,variant",
    [
        (
            "RAVE-v2r0-crop.nc",
            "rave-v2r0",
            "RAVE-HrlyEmiss-13km",
            "v2r0",
            "2026-07-01T00:00:00Z",
            "2026-07-01T01:00:00Z",
            "2026-07-01T02:02:02Z",
            "2026-07-01T01:00:00Z",
            11,
            "unscaled",
        ),
        (
            "RAVE-v2r0-crop.nc",
            "rave-v2r0",
            "RAVE-HrlyEmiss-13km",
            "v2r0",
            "2026-07-01T00:00:00Z",
            "2026-07-01T01:00:00Z",
            "2026-07-01T02:02:02Z",
            "2026-07-01T01:00:00Z",
            11,
            "scaled",
        ),
        (
            "GBBEPx-v4r0-crop.nc",
            "gbbepx-v4r0",
            "GBBEPx-all01GRID",
            "v4r0",
            "2023-02-24T00:00:00Z",
            "2023-02-25T00:00:00Z",
            "2023-02-25T01:34:09Z",
            "2023-02-25T00:00:00Z",
            8,
            "unscaled",
        ),
    ],
)
def test_live_preparation_writes_real_product_interval_and_manifest(
    tmp_path, filename, adapter, product_id, version, start, end, created, run_end, expected_species, variant
):
    sample_path = ROOT / "tests" / "data" / "fire" / "product_samples" / filename
    config_path = _write_actual_product_inputs(
        tmp_path,
        sample_path,
        adapter=adapter,
        product_id=product_id,
        version=version,
        start=start,
        end=end,
        created=created,
        run_end=run_end,
        variant=variant,
    )

    result = _run(tmp_path, config_path)

    assert result.returncode == 0, result.stderr
    output = tmp_path / "fire-output"
    manifest = yaml.safe_load((output / "manifest.yaml").read_text())
    assert manifest["product_adapter"] == adapter
    assert len(manifest["intervals"]) == 1
    interval = manifest["intervals"][0]
    assert interval["product_id"] == product_id
    assert interval["variant"] == variant
    assert interval["interval_start"] == start
    assert interval["interval_end"] == end
    assert len(interval["species"]) == expected_species
    if variant == "scaled":
        assert interval["scaling_state"] == "applied"
        assert interval["scaling_profile"]["profile_id"] == "RAVE-embedded-scaled-variant"
    normalized = output / interval["path"]
    assert normalized.is_file()
    assert _sha256(normalized) == interval["content_sha256"]


def test_live_preparation_cleans_committed_intervals_after_later_decode_failure(tmp_path):
    sample_path = ROOT / "tests" / "data" / "fire" / "product_samples" / "RAVE-v2r0-crop.nc"
    config_path = _write_actual_product_inputs(
        tmp_path,
        sample_path,
        adapter="rave-v2r0",
        product_id="RAVE-HrlyEmiss-13km",
        version="v2r0",
        start="2026-07-01T00:00:00Z",
        end="2026-07-01T01:00:00Z",
        created="2026-07-01T02:02:02Z",
        run_end="2026-07-01T02:00:00Z",
    )
    invalid_source = tmp_path / "invalid.nc"
    invalid_source.write_bytes(b"not a netcdf product")
    source_manifest_path = tmp_path / "sources.yaml"
    source_manifest = yaml.safe_load(source_manifest_path.read_text())
    second = dict(source_manifest["sources"][0])
    second.update(
        {
            "source_id": "rave-hour-01",
            "local_path": invalid_source.name,
            "content_sha256": _sha256(invalid_source),
            "interval_start": "2026-07-01T01:00:00Z",
            "interval_end": "2026-07-01T02:00:00Z",
        }
    )
    source_manifest["sources"].append(second)
    source_manifest_path.write_text(yaml.safe_dump(source_manifest, sort_keys=True))

    result = _run(tmp_path, config_path)

    assert result.returncode == 2
    assert "decoder failed" in result.stderr
    assert not (tmp_path / "fire-output" / "manifest.yaml").exists()
    assert not (tmp_path / "fire-output").exists()
    assert not list(tmp_path.glob(".cece-fire-stage-*"))


def test_live_preparation_failure_preserves_existing_manifest_and_files(tmp_path):
    sample_path = ROOT / "tests" / "data" / "fire" / "product_samples" / "RAVE-v2r0-crop.nc"
    config_path = _write_actual_product_inputs(
        tmp_path,
        sample_path,
        adapter="rave-v2r0",
        product_id="RAVE-HrlyEmiss-13km",
        version="v2r0",
        start="2026-07-01T00:00:00Z",
        end="2026-07-01T01:00:00Z",
        created="2026-07-01T02:02:02Z",
        run_end="2026-07-01T02:00:00Z",
    )
    invalid_source = tmp_path / "invalid.nc"
    invalid_source.write_bytes(b"not a netcdf product")
    source_manifest_path = tmp_path / "sources.yaml"
    source_manifest = yaml.safe_load(source_manifest_path.read_text())
    second = dict(source_manifest["sources"][0])
    second.update(
        {
            "source_id": "rave-hour-01",
            "local_path": invalid_source.name,
            "content_sha256": _sha256(invalid_source),
            "interval_start": "2026-07-01T01:00:00Z",
            "interval_end": "2026-07-01T02:00:00Z",
        }
    )
    source_manifest["sources"].append(second)
    source_manifest_path.write_text(yaml.safe_dump(source_manifest, sort_keys=True))

    output = tmp_path / "fire-output"
    interval_dir = output / "intervals"
    interval_dir.mkdir(parents=True)
    previous_manifest = {
        "schema_version": 1,
        "source_manifest_sha256": _sha256(source_manifest_path),
        "mode": "product",
        "product_adapter": "rave-v2r0",
        "product_time_allocation": "source_resolved",
        "chemistry_state": "bulk",
        "target_id": "CB6",
        "target_version": "synthetic-v1",
        "cutoff_mode": "fixed",
        "availability_cutoff": "2026-07-03T00:00:00Z",
    }
    (output / "manifest.yaml").write_text(yaml.safe_dump(previous_manifest, sort_keys=True))
    previous_interval = interval_dir / "previous.nc"
    previous_interval.write_bytes(b"previous normalized interval")

    result = _run(tmp_path, config_path)

    assert result.returncode == 2
    assert "decoder failed" in result.stderr
    assert yaml.safe_load((output / "manifest.yaml").read_text()) == previous_manifest
    assert previous_interval.read_bytes() == b"previous normalized interval"
    assert sorted(path.name for path in interval_dir.iterdir()) == ["previous.nc"]
    assert not list(tmp_path.glob(".cece-fire-stage-*"))


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
