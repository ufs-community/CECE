#!/usr/bin/env python3
"""Preflight fire source manifests before a verified adapter normalizes inputs.

The source-manifest and temporal-policy checks are intentionally independent of
NOAA decoders. Preflight never downloads remote data or creates normalized
outputs. Actual normalization remains disabled until a verified adapter is
available.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import math
import os
import re
import sys
import tempfile
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any
from urllib.parse import parse_qsl, urlparse

import yaml


class PreparationError(ValueError):
    """Invalid configuration or unsupported source metadata."""


class OutputError(OSError):
    """Preparation could not read or commit requested output."""


class _UniqueKeyLoader(yaml.SafeLoader):
    pass


def _construct_unique_mapping(loader: _UniqueKeyLoader, node: yaml.MappingNode, deep: bool = False):
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise PreparationError(f"duplicate YAML key {key!r}")
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


_UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _construct_unique_mapping
)
_SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
_UTC_FORMAT = "%Y-%m-%dT%H:%M:%SZ"
_UTC_RE = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z\Z")


def _load_yaml(path: Path) -> Any:
    try:
        with path.open(encoding="utf-8") as stream:
            return yaml.load(stream, Loader=_UniqueKeyLoader)
    except OSError as error:
        raise PreparationError(f"cannot read {path}: {error}") from error
    except yaml.YAMLError as error:
        raise PreparationError(f"invalid YAML in {path}: {error}") from error


def _mapping(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict) or any(not isinstance(key, str) for key in value):
        raise PreparationError(f"{context} must be a YAML mapping")
    return value


def _text(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise PreparationError(f"{context} must be a nonempty string")
    return value


def _utc(value: Any, context: str) -> datetime:
    text = _text(value, context)
    if not _UTC_RE.fullmatch(text):
        raise PreparationError(f"{context} must use YYYY-MM-DDTHH:MM:SSZ format")
    try:
        parsed = datetime.strptime(text, _UTC_FORMAT).replace(tzinfo=timezone.utc)
    except ValueError as error:
        raise PreparationError(f"{context} must be a valid UTC timestamp in YYYY-MM-DDTHH:MM:SSZ format") from error
    return parsed


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise OutputError(f"cannot read local source {path}: {error}") from error
    return digest.hexdigest()


def _resolve_path(value: str, context: str) -> Path:
    path = Path(_text(value, context)).expanduser()
    return path.resolve()


def _load_fire_config(config_path: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    config = _mapping(_load_yaml(config_path), "CECE config")
    schema_path = Path(__file__).resolve().parents[1] / "src" / "python" / "config.py"
    spec = importlib.util.spec_from_file_location("cece_prepare_fire_config_schema", schema_path)
    if spec is None or spec.loader is None:
        raise PreparationError(f"cannot load CECE configuration schema from {schema_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    try:
        validation = module.CeceConfig.from_dict(config).validate()
    except (TypeError, ValueError) as error:
        raise PreparationError(f"invalid CECE configuration: {error}") from error
    if not validation.is_valid:
        raise PreparationError(f"invalid CECE configuration: {validation}")
    schemes = config.get("physics_schemes")
    if not isinstance(schemes, list):
        raise PreparationError("CECE config physics_schemes must be a list")
    fire_schemes = [
        _mapping(item, f"physics_schemes[{index}]")
        for index, item in enumerate(schemes)
        if isinstance(item, dict) and item.get("name") == "fire"
    ]
    if len(fire_schemes) != 1:
        raise PreparationError("CECE config must contain exactly one fire physics scheme")
    scheme = fire_schemes[0]
    options = _mapping(scheme.get("options"), "fire scheme options")
    mode = options.get("mode")
    if mode not in {"product", "derive_frp"}:
        raise PreparationError("fire.options.mode must be product or derive_frp")
    if mode == "derive_frp":
        raise PreparationError("FRP preparation is not supported; sensor normalization requires a verified sensor adapter")
    for key in ("source_manifest", "product_adapter", "product_time_allocation", "cutoff_mode"):
        if key not in options:
            raise PreparationError(f"fire.options.{key} is required for product preparation")
    if options["product_time_allocation"] not in {"uniform", "source_resolved", "profile"}:
        raise PreparationError("fire.options.product_time_allocation must be uniform, source_resolved or profile")
    if options["cutoff_mode"] not in {"fixed", "host_supplied"}:
        raise PreparationError("fire.options.cutoff_mode must be fixed or host_supplied")
    return config, options


def _validate_source_entry(entry: Any, index: int, manifest_path: Path) -> dict[str, Any]:
    source = _mapping(entry, f"source_manifest.sources[{index}]")
    for key in (
        "source_id",
        "adapter_id",
        "product_version",
        "algorithm_version",
        "variant",
        "chemistry_state",
        "acquired_at",
        "available_at",
    ):
        _text(source.get(key), f"source_manifest.sources[{index}].{key}")
    if source["chemistry_state"] not in {"bulk", "target_specific"}:
        raise PreparationError(f"source {source['source_id']!r}: chemistry_state must be bulk or target_specific")
    acquired_at = _utc(source["acquired_at"], f"source_manifest.sources[{index}].acquired_at")
    available_at = _utc(source["available_at"], f"source_manifest.sources[{index}].available_at")
    if available_at < acquired_at:
        raise PreparationError(f"source {source['source_id']!r}: available_at precedes acquired_at")

    has_local = "local_path" in source
    has_uri = "uri" in source
    if has_local == has_uri:
        raise PreparationError(f"source {source['source_id']!r}: provide exactly one of local_path or uri")
    digest = source.get("content_sha256")
    if has_local:
        local_path = Path(_text(source["local_path"], f"source {source['source_id']}.local_path")).expanduser()
        if not local_path.is_absolute():
            local_path = manifest_path.parent / local_path
        local_path = local_path.resolve()
        if not isinstance(digest, str) or not _SHA256_RE.fullmatch(digest):
            raise PreparationError(f"source {source['source_id']!r}: local sources require lowercase SHA256 content_sha256")
        actual = _sha256_file(local_path)
        if actual != digest:
            raise PreparationError(f"source {source['source_id']!r}: content_sha256 mismatch for {local_path}")
        source["_resolved_path"] = str(local_path)
        source["_actual_sha256"] = actual
    else:
        uri = _text(source["uri"], f"source {source['source_id']}.uri")
        parsed = urlparse(uri)
        if parsed.scheme not in {"https", "http", "s3", "gs"} or not parsed.netloc:
            raise PreparationError(f"source {source['source_id']!r}: uri must use an explicit supported scheme")
        query_credentials = {key.lower() for key, _ in parse_qsl(parsed.query)}
        if parsed.username or parsed.password or query_credentials.intersection({"token", "access_token", "password", "authorization"}):
            raise PreparationError(f"source {source['source_id']!r}: credentials must not be embedded in uri")
        provider_checksum = source.get("provider_checksum")
        if not (isinstance(digest, str) and _SHA256_RE.fullmatch(digest)) and not _text_optional(provider_checksum):
            raise PreparationError(f"source {source['source_id']!r}: remote sources require content_sha256 or provider_checksum")
        source["_actual_sha256"] = digest if isinstance(digest, str) else None
    priority = source.get("priority", 0)
    if isinstance(priority, bool) or not isinstance(priority, int):
        raise PreparationError(f"source {source['source_id']!r}: priority must be an integer")
    source["_acquired_at"] = acquired_at
    source["_available_at"] = available_at
    source["_priority"] = priority
    return source


def _text_optional(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def _interval(source: dict[str, Any], context: str) -> tuple[datetime, datetime] | None:
    has_start, has_end = "interval_start" in source, "interval_end" in source
    if has_start != has_end:
        raise PreparationError(f"{context}: interval_start and interval_end must be supplied together")
    if not has_start:
        return None
    start = _utc(source["interval_start"], f"{context}.interval_start")
    end = _utc(source["interval_end"], f"{context}.interval_end")
    if end <= start:
        raise PreparationError(f"{context}: interval_end must be later than interval_start")
    return start, end


def _select_sources(sources: list[dict[str, Any]]) -> list[dict[str, Any]]:
    identifiers = [source["source_id"] for source in sources]
    if len(set(identifiers)) != len(identifiers):
        raise PreparationError("source_manifest source_id values must be unique")
    selected = list(sources)
    for index, first in enumerate(sources):
        first_interval = _interval(first, f"source {first['source_id']!r}")
        if first_interval is None:
            continue
        for second in sources[index + 1 :]:
            second_interval = _interval(second, f"source {second['source_id']!r}")
            if second_interval is None:
                continue
            first_product = first.get("product_id", first["adapter_id"])
            second_product = second.get("product_id", second["adapter_id"])
            same_product = (
                first_product == second_product
                and first.get("variant") == second.get("variant")
                and first.get("chemistry_state") == second.get("chemistry_state")
            )
            overlaps = first_interval[0] < second_interval[1] and second_interval[0] < first_interval[1]
            if not (same_product and overlaps):
                continue
            if first["_actual_sha256"] == second["_actual_sha256"]:
                loser = second if first["_priority"] >= second["_priority"] else first
            elif first["_priority"] == second["_priority"]:
                raise PreparationError(
                    f"overlapping product sources {first['source_id']!r} and {second['source_id']!r} "
                    "have conflicting content and equal priority"
                )
            else:
                loser = second if first["_priority"] > second["_priority"] else first
            if loser in selected:
                selected.remove(loser)
    return selected


def _expected_intervals(config: dict[str, Any]) -> list[tuple[datetime, datetime]]:
    driver = _mapping(config.get("driver", {}), "driver")
    start = _utc(driver.get("start_time"), "driver.start_time")
    end = _utc(driver.get("end_time"), "driver.end_time")
    step = driver.get("timestep_seconds")
    if isinstance(step, bool) or not isinstance(step, int) or step < 1:
        raise PreparationError("driver.timestep_seconds must be a positive integer")
    if end <= start:
        raise PreparationError("driver.end_time must be after driver.start_time")
    intervals = []
    current = start
    while current < end:
        next_time = min(current + timedelta(seconds=step), end)
        intervals.append((current, next_time))
        current = next_time
    return intervals


def _validate_schedule(path: Path, config: dict[str, Any]) -> dict[tuple[datetime, datetime], datetime]:
    schedule = _mapping(_load_yaml(path), "cutoff_schedule")
    if schedule.get("schema_version") != 1 or not isinstance(schedule.get("intervals"), list):
        raise PreparationError("cutoff_schedule requires schema_version: 1 and an intervals list")
    parsed: dict[tuple[datetime, datetime], datetime] = {}
    previous_cutoff = None
    for index, entry in enumerate(schedule["intervals"]):
        item = _mapping(entry, f"cutoff_schedule.intervals[{index}]")
        start = _utc(item.get("interval_start"), f"cutoff_schedule.intervals[{index}].interval_start")
        end = _utc(item.get("interval_end"), f"cutoff_schedule.intervals[{index}].interval_end")
        cutoff = _utc(item.get("availability_cutoff"), f"cutoff_schedule.intervals[{index}].availability_cutoff")
        if end <= start:
            raise PreparationError(f"cutoff_schedule.intervals[{index}]: interval_end must follow interval_start")
        key = (start, end)
        if key in parsed:
            raise PreparationError("cutoff_schedule contains duplicate intervals")
        if previous_cutoff is not None and cutoff < previous_cutoff:
            raise PreparationError("cutoff_schedule cutoffs must be nondecreasing")
        parsed[key] = cutoff
        previous_cutoff = cutoff
    expected = _expected_intervals(config)
    if list(parsed) != expected:
        raise PreparationError("cutoff_schedule must list exactly one entry per requested model interval in chronological order")
    return parsed


def _validate_temporal_profile(options: dict[str, Any], sources: list[dict[str, Any]]) -> None:
    if options["product_time_allocation"] != "profile":
        if "product_time_profile" in options:
            raise PreparationError("product_time_profile is valid only when product_time_allocation is profile")
        return
    profile_value = options.get("product_time_profile")
    profile_path = _resolve_path(profile_value, "fire.options.product_time_profile")
    profile = _mapping(_load_yaml(profile_path), "product_time_profile")
    profile_id = _text(profile.get("profile_id"), "product_time_profile.profile_id")
    _text(profile.get("version"), "product_time_profile.version")
    if profile.get("schema_version") != 1 or profile.get("time_basis") not in {"UTC", "local_solar_time"}:
        raise PreparationError("product_time_profile requires schema_version: 1 and a supported time_basis")
    cadence = profile.get("supported_source_cadence_seconds")
    if isinstance(cadence, bool) or not isinstance(cadence, int) or cadence <= 0:
        raise PreparationError("product_time_profile.supported_source_cadence_seconds must be a positive integer")
    weights = profile.get("weights")
    if not isinstance(weights, list) or not weights:
        raise PreparationError("product_time_profile.weights must be a nonempty list")
    integrated_weight = 0.0
    weighted_ranges = []
    for index, value in enumerate(weights):
        weight = _mapping(value, f"product_time_profile.weights[{index}]")
        rate = weight.get("weight_per_second")
        duration = weight.get("duration_seconds")
        offset = weight.get("offset_seconds")
        if (
            isinstance(rate, bool)
            or not isinstance(rate, (int, float))
            or not math.isfinite(rate)
            or rate < 0
            or isinstance(duration, bool)
            or not isinstance(duration, int)
            or duration < 1
            or isinstance(offset, bool)
            or not isinstance(offset, int)
            or offset < 0
        ):
            raise PreparationError(
                f"product_time_profile.weights[{index}] requires nonnegative offset/rate and positive duration"
            )
        integrated_weight += rate * duration
        if offset + duration > cadence:
            raise PreparationError(f"product_time_profile.weights[{index}] extends beyond supported source cadence")
        weighted_ranges.append((offset, offset + duration))
    weighted_ranges.sort()
    if any(current[0] < previous[1] for previous, current in zip(weighted_ranges, weighted_ranges[1:])):
        raise PreparationError("product_time_profile weight intervals must not overlap")
    if not math.isclose(integrated_weight, 1.0, rel_tol=1e-12, abs_tol=1e-12):
        raise PreparationError("product_time_profile weights must integrate to one")
    for source in sources:
        source_cadence = source.get("temporal_resolution_seconds")
        if source_cadence is not None:
            if isinstance(source_cadence, bool) or not isinstance(source_cadence, int) or source_cadence <= 0:
                raise PreparationError(f"source {source['source_id']!r}: temporal_resolution_seconds must be positive")
            if source_cadence <= cadence:
                raise PreparationError(
                    f"source {source['source_id']!r}: product temporal cadence is already resolved at or finer than profile cadence"
                )
        if source.get("temporal_profile_id") == profile_id:
            raise PreparationError(f"source {source['source_id']!r}: product temporal profile has already been applied")
def _atomic_write_yaml(path: Path, value: dict[str, Any], *, overwrite: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not overwrite:
        raise FileExistsError(f"output already exists: {path}")
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.", suffix=".tmp", delete=False
        ) as stream:
            temporary_path = Path(stream.name)
            yaml.safe_dump(value, stream, sort_keys=True)
            stream.flush()
            os.fsync(stream.fileno())
        if overwrite:
            os.replace(temporary_path, path)
        else:
            os.link(temporary_path, path)
            temporary_path.unlink()
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def _preflight(config_path: Path, output_dir: Path, *, overwrite: bool = False) -> list[dict[str, Any]]:
    config_path = config_path.resolve()
    config, options = _load_fire_config(config_path)
    source_path = _resolve_path(options["source_manifest"], "fire.options.source_manifest")
    output_dir = output_dir.resolve()
    manifest_path = output_dir / "manifest.yaml"
    if source_path == manifest_path.resolve() or output_dir == source_path or output_dir in source_path.parents:
        raise PreparationError("source_manifest must not resolve to the normalized output manifest or reside inside output_dir")
    configured_output = options.get("input_manifest")
    if configured_output:
        configured_output_path = _resolve_path(configured_output, "fire.options.input_manifest")
        if configured_output_path != manifest_path:
            raise PreparationError("fire.options.input_manifest must match <output-dir>/manifest.yaml")

    manifest = _mapping(_load_yaml(source_path), "source_manifest")
    if manifest.get("schema_version") != 1 or not isinstance(manifest.get("sources"), list) or not manifest["sources"]:
        raise PreparationError("source_manifest requires schema_version: 1 and a nonempty sources list")
    sources = [
        _validate_source_entry(item, index, source_path)
        for index, item in enumerate(manifest["sources"])
    ]
    selected = _select_sources(sources)
    _validate_temporal_profile(options, selected)
    for source in selected:
        if source["adapter_id"] != options["product_adapter"]:
            raise PreparationError(
                f"source {source['source_id']!r} adapter_id does not match configured product_adapter"
            )
        configured_chemistry = options.get("chemistry_state", "bulk")
        if source["chemistry_state"] != configured_chemistry:
            raise PreparationError(
                f"source {source['source_id']!r} chemistry_state does not match the configured fire scheme"
            )
        if configured_chemistry == "target_specific":
            if source.get("target_id") != options.get("target_id") or source.get("target_version") != options.get("target_version"):
                raise PreparationError(
                    f"source {source['source_id']!r} target identity does not match configured target id/version"
                )

    cutoffs: dict[tuple[datetime, datetime], datetime] = {}
    if options["cutoff_mode"] == "fixed":
        cutoff = _utc(options.get("availability_cutoff"), "fire.options.availability_cutoff")
        cutoffs = {interval: cutoff for interval in _expected_intervals(config)}
    else:
        schedule_value = options.get("cutoff_schedule")
        schedule_path = _resolve_path(schedule_value, "fire.options.cutoff_schedule")
        cutoffs = _validate_schedule(schedule_path, config)
    for source in selected:
        interval = _interval(source, f"source {source['source_id']!r}")
        matching_cutoffs = [
            cutoff
            for (start, end), cutoff in cutoffs.items()
            if interval is None or (start < interval[1] and interval[0] < end)
        ]
        if not matching_cutoffs:
            raise PreparationError(f"source {source['source_id']!r} does not overlap a requested model interval")
        if any(source["_acquired_at"] > cutoff or source["_available_at"] > cutoff for cutoff in matching_cutoffs):
            raise PreparationError(f"source {source['source_id']!r} was acquired or available after its cutoff")

    if manifest_path.exists() and not overwrite:
        existing = _mapping(_load_yaml(manifest_path), "existing normalized manifest")
        expected = {
            "schema_version": 1,
            "source_manifest_sha256": _sha256_file(source_path),
            "mode": options["mode"],
            "product_adapter": options["product_adapter"],
            "product_time_allocation": options["product_time_allocation"],
            "chemistry_state": options.get("chemistry_state", "bulk"),
            "target_id": options.get("target_id"),
            "target_version": options.get("target_version"),
            "cutoff_mode": options["cutoff_mode"],
        }
        if options["cutoff_mode"] == "fixed":
            expected["availability_cutoff"] = options["availability_cutoff"]
        for key, expected_value in expected.items():
            if existing.get(key) != expected_value:
                raise PreparationError(f"existing normalized manifest has incompatible {key}; pass --overwrite")
    return selected


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True, help="CECE configuration with one fire scheme")
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory for normalized manifest and interval files")
    parser.add_argument("--preflight-only", action="store_true", help="Validate sources and policies without writing outputs")
    parser.add_argument("--overwrite", action="store_true", help="Allow replacing an incompatible normalized manifest")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        selected = _preflight(args.config, args.output_dir, overwrite=args.overwrite)
        if args.preflight_only:
            print(
                "Manifest-level preflight passed; decoder file-metadata checks remain pending. Selected sources: "
                + ", ".join(source["source_id"] for source in selected)
            )
            return 0
        raise PreparationError(
            "no verified fire product decoder is registered; preflight passed but no normalized outputs were written"
        )
    except PreparationError as error:
        print(f"fire preparation error: {error}", file=sys.stderr)
        return 2
    except (OSError, yaml.YAMLError) as error:
        print(f"fire preparation I/O failure: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
