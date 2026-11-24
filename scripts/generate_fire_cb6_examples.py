#!/usr/bin/env python3
"""Generate non-production CB6 fire profiles and product-run examples."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
from typing import Any

import yaml

ROOT = Path(__file__).resolve().parents[1]
BULK_SPECIES = [
    "CO2",
    "CO",
    "CH4",
    "PM25",
    "TPM",
    "SO2",
    "OC",
    "BC",
    "NOX",
    "NH3",
    "VOC",
]
NATIVE_SPECIES = ["CO2", "CO", "CH4", "SO2", "OC", "BC", "NH3"]
TARGET_ID = "CB6"
TARGET_VERSION = "cb6r5_ae7_aq"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def yaml_bytes(value: Any) -> bytes:
    return yaml.safe_dump(
        value, sort_keys=False, allow_unicode=False, width=110
    ).encode("utf-8")


def make_factor_species(manifest: dict[str, Any]) -> dict[str, Any]:
    result = {
        "schema_version": 1,
        "identity": {
            "target_id": TARGET_ID,
            "target_version": TARGET_VERSION,
            "mechanism_version": "r5",
        },
        "purpose": "Fire-specific CB6 factor output species; separate from data/speciation/map_cb6.yaml.",
        "coefficient_semantics": "independent coefficients; values are not normalized",
        "species": [],
    }
    for source_name, source_species in (
        ("species_mapping_co", "CO"),
        ("species_mapping_pm25", "PM25"),
    ):
        for name, row in manifest[source_name]["factors"].items():
            result["species"].append({
                "name": name,
                "source_species": source_species,
                "factor": row["value"],
                "output_unit": "kg",
                "source_unit_label": row["unit"],
                "description": row["name"],
            })
    return result


def make_factor_profile(
    manifest_path: Path,
    species_path: Path,
    species_sha256: str,
    source_excerpt_path: Path,
    manifest: dict[str, Any],
) -> dict[str, Any]:
    mappings = []
    target_species = []
    for source_name, source_species in (
        ("species_mapping_co", "CO"),
        ("species_mapping_pm25", "PM25"),
    ):
        for name, row in manifest[source_name]["factors"].items():
            mappings.append({
                "source_species": source_species,
                "target_species": name,
                "basis": "mass",
                "factor": row["value"],
                "output_unit": "kg",
            })
            target_species.append({"name": name})

    return {
        "schema_version": 1,
        "production_eligible": False,
        "mapping_factor_semantics": "independent_coefficients",
        "identity": {
            "id": "cb6-fire-co-pm25-factor-profile",
            "version": "manifest-v1",
            "target_id": TARGET_ID,
            "target_version": TARGET_VERSION,
        },
        "assets": [
            {
                "name": "verified-factor-manifest",
                "path": manifest_path.name,
                "sha256": sha256(manifest_path),
            },
            {
                "name": "pinned-aqm-configuration-excerpt",
                "path": source_excerpt_path.name,
                "sha256": sha256(source_excerpt_path),
            },
            {
                "name": "fire-specific-target-species",
                "path": species_path.name,
                "sha256": species_sha256,
            },
        ],
        "mechanism_species": target_species,
        "typed_mappings": mappings,
        "conversion_factors": {
            "source_molecular_weights_g_mol": {},
            "target_molecular_weights_g_mol": {},
            "source_carbon_numbers": {},
            "target_carbon_numbers": {},
            "aerosol_factors": {},
        },
        "oc_om_policy": {"kind": "preserve_oc"},
        "source_disposition": {
            species: (
                {"kind": "mapped"}
                if species in {"CO", "PM25"}
                else {
                    "kind": "excluded",
                    "reason": "Not consumed in factor mode; CO/PM25 factors generate overlapping target species. Select the separate native profile to use product fields directly.",
                }
            )
            for species in BULK_SPECIES
        },
        "budgets_tolerances": {"relative": 1.0e-6, "absolute": 1.0e-12},
    }


def make_native_species() -> dict[str, Any]:
    return {
        "schema_version": 1,
        "identity": {"target_id": TARGET_ID, "target_version": TARGET_VERSION},
        "purpose": "Native product-basis export fields; these are not CB6-speciated mechanism emissions.",
        "warning": "Use instead of, never together with, cb6_fire_speciation_profile.yaml.",
        "species": [{"name": name, "source_species": name} for name in NATIVE_SPECIES],
    }


def make_native_profile(
    manifest_path: Path,
    species_path: Path,
    species_sha256: str,
    source_excerpt_path: Path,
) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "production_eligible": False,
        "mapping_factor_semantics": "independent_coefficients",
        "identity": {
            "id": "cb6-fire-native-product-basis",
            "version": "manifest-v1",
            "target_id": TARGET_ID,
            "target_version": TARGET_VERSION,
        },
        "assets": [
            {
                "name": "fire-specific-native-species-definition",
                "path": species_path.name,
                "sha256": species_sha256,
            },
            {
                "name": "verified-factor-manifest",
                "path": manifest_path.name,
                "sha256": sha256(manifest_path),
            },
            {
                "name": "pinned-aqm-configuration-excerpt",
                "path": source_excerpt_path.name,
                "sha256": sha256(source_excerpt_path),
            },
        ],
        "mechanism_species": [{"name": name} for name in NATIVE_SPECIES],
        "typed_mappings": [
            {
                "source_species": name,
                "target_species": name,
                "basis": "mass",
                "factor": 1.0,
                "output_unit": "kg",
            }
            for name in NATIVE_SPECIES
        ],
        "conversion_factors": {
            "source_molecular_weights_g_mol": {},
            "target_molecular_weights_g_mol": {},
            "source_carbon_numbers": {},
            "target_carbon_numbers": {},
            "aerosol_factors": {},
        },
        "oc_om_policy": {"kind": "preserve_oc"},
        "source_disposition": {
            species: (
                {"kind": "mapped"}
                if species in NATIVE_SPECIES
                else {
                    "kind": "excluded",
                    "reason": (
                        "Excluded from native mode to avoid overlap with PM2.5/TPM or the NOx/VOC lumps; "
                        "PM2.5/CO factor-derived outputs are an alternative profile, not an additive contribution."
                    ),
                }
            )
            for species in BULK_SPECIES
        },
        "budgets_tolerances": {"relative": 1.0e-6, "absolute": 1.0e-12},
    }


def make_source_manifest(product: str) -> dict[str, Any]:
    if product == "rave":
        source = {
            "source_id": "rave-v2r0-20260701-hour-00",
            "adapter_id": "rave-v2r0",
            "product_id": "RAVE-HrlyEmiss-13km",
            "product_version": "v2r0",
            "algorithm_version": "v2r0",
            "variant": "unscaled",
            "chemistry_state": "bulk",
            "interval_start": "2026-07-01T00:00:00Z",
            "interval_end": "2026-07-01T01:00:00Z",
            "acquired_at": "2026-07-01T02:02:02Z",
            "available_at": "2026-07-01T02:02:02Z",
            "local_path": "../../data/fire/product_samples/RAVE-HrlyEmiss-13km_v2r0_20260701_sample.nc",
            "content_sha256": "0ac99097022d50bd9128af93df8268a3d53f96b70bad8d267cfde497ab264141",
        }
    else:
        source = {
            "source_id": "gbbepx-v4r0-20230224-day-00",
            "adapter_id": "gbbepx-v4r0",
            "product_id": "GBBEPx-all01GRID",
            "product_version": "v4r0",
            "algorithm_version": "v4r0",
            "variant": "unscaled",
            "chemistry_state": "bulk",
            "interval_start": "2023-02-24T00:00:00Z",
            "interval_end": "2023-02-25T00:00:00Z",
            "acquired_at": "2023-02-25T01:34:09Z",
            "available_at": "2023-02-25T01:34:09Z",
            "local_path": "../../data/fire/product_samples/GBBEPx-all01GRID_v4r0_20230224_sample.nc",
            "content_sha256": "4dbcff118dd4bf78013d69a260c4700225464f1c0c3301839eb62d74ba40b56c",
        }
    return {"schema_version": 1, "sources": [source]}


def make_config(
    product: str, mode: str, source_species: list[str], output_species: list[str]
) -> dict[str, Any]:
    rave = product == "rave"
    prefix = f"{product}_cb6_{mode}"
    start, end, timestep, cutoff = (
        ("2026-07-01T00:00:00", "2026-07-01T01:00:00", 3600, "2026-07-03T00:00:00Z")
        if rave
        else (
            "2023-02-24T00:00:00",
            "2023-02-25T00:00:00",
            86400,
            "2023-02-26T00:00:00Z",
        )
    )
    requested_species = ["CO", "PM25"] if mode == "speciation" else source_species
    profile = (
        "cb6_fire_speciation_profile.yaml"
        if mode == "speciation"
        else "cb6_fire_native_profile.yaml"
    )
    species_definition = (
        "cb6_fire_target_species.yaml"
        if mode == "speciation"
        else "cb6_fire_native_species.yaml"
    )
    fields = [
        {
            "name": f"FIRE_{name}",
            "attributes": {
                "units": "kg m-2 s-1",
                "long_name": f"fire {name} emission flux",
            },
        }
        for name in output_species
    ]
    diagnostics = (
        (["PM25", "TPM", "NOX", "VOC"] if rave else ["PM25", "NOX"])
        if mode == "native"
        else []
    )
    warnings = {
        "PM25": "overlapping pool",
        "TPM": "overlapping pool",
        "NOX": "reference basis unverified",
        "VOC": "composition unverified",
    }
    fields.extend(
        {
            "name": f"FIRE_NATIVE_{name}",
            "attributes": {
                "units": "kg m-2 s-1",
                "long_name": f"nonproduction native {name} diagnostic; {warnings[name]}; not model emissions",
            },
        }
        for name in diagnostics
    )
    return {
        "driver": {
            "start_time": start,
            "end_time": end,
            "timestep_seconds": timestep,
            "grid": {
                "nx": 72,
                "ny": 46,
                "nz": 1,
                "lon_min": 235.0,
                "lon_max": 294.0,
                "lat_min": 25.0,
                "lat_max": 49.0,
            },
        },
        "vertical_grid": {"type": "none"},
        "physics_schemes": [
            {
                "name": "fire",
                "language": "cpp",
                "options": {
                    "mode": "product",
                    "input_provider": "prepared",
                    "source_manifest": f"examples/fire_sources/{'rave_v2r0' if rave else 'gbbepx_v4r0'}.yaml",
                    "input_manifest": f"examples/fire_inputs/{product}/manifest.yaml",
                    "product_adapter": "rave-v2r0" if rave else "gbbepx-v4r0",
                    "product_time_allocation": "source_resolved",
                    "chemistry_state": "bulk",
                    "requested_species": requested_species,
                    **(
                        {"native_diagnostic_species": diagnostics}
                        if diagnostics
                        else {}
                    ),
                    "target_id": TARGET_ID,
                    "target_version": TARGET_VERSION,
                    "mechanism_file": f"data/fire/{species_definition}",
                    "fire_mapping_file": f"data/fire/{profile}",
                    "missing_policy": "error",
                    "cutoff_mode": "fixed",
                    "availability_cutoff": cutoff,
                    "scaling": "none",
                },
            }
        ],
        "output": {
            "enabled": True,
            "directory": f"./cece_output_{prefix}",
            "filename_pattern": f"cece_fire_{prefix}_{{YYYY}}{{MM}}{{DD}}_{{HH}}{{mm}}{{ss}}.nc",
            "frequency_steps": 1,
            "fields": fields,
        },
    }


def generated_files() -> dict[Path, bytes]:
    data_fire = ROOT / "data" / "fire"
    manifest_path = data_fire / "cb6_fire_factors_manifest.yaml"
    source_excerpt_path = data_fire / "cb6_fire_config.txt"
    manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    factor_species_path = data_fire / "cb6_fire_target_species.yaml"
    native_species_path = data_fire / "cb6_fire_native_species.yaml"
    factor_species = make_factor_species(manifest)
    native_species = make_native_species()
    factor_species_bytes = yaml_bytes(factor_species)
    native_species_bytes = yaml_bytes(native_species)
    files = {
        factor_species_path: factor_species_bytes,
        native_species_path: native_species_bytes,
    }
    files[data_fire / "cb6_fire_speciation_profile.yaml"] = yaml_bytes(
        make_factor_profile(
            manifest_path,
            factor_species_path,
            sha256_bytes(factor_species_bytes),
            source_excerpt_path,
            manifest,
        )
    )
    files[data_fire / "cb6_fire_native_profile.yaml"] = yaml_bytes(
        make_native_profile(
            manifest_path,
            native_species_path,
            sha256_bytes(native_species_bytes),
            source_excerpt_path,
        )
    )
    source_dir = ROOT / "examples" / "fire_sources"
    files[source_dir / "rave_v2r0.yaml"] = yaml_bytes(make_source_manifest("rave"))
    files[source_dir / "gbbepx_v4r0.yaml"] = yaml_bytes(make_source_manifest("gbbepx"))

    example_dir = ROOT / "examples"
    for product, direct_species in (
        ("rave", NATIVE_SPECIES),
        ("gbbepx", [name for name in NATIVE_SPECIES if name != "CH4"]),
    ):
        factor_targets = [row["name"] for row in factor_species["species"]]
        for mode, source_species, output_species in (
            ("speciation", ["CO", "PM25"], factor_targets),
            ("native", direct_species, direct_species),
        ):
            config_path = example_dir / f"cece_config_fire_{product}_cb6_{mode}.yaml"
            files[config_path] = yaml_bytes(
                make_config(product, mode, source_species, output_species)
            )
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if generated assets differ from checked-in files",
    )
    args = parser.parse_args()
    stale = []
    for path, content in generated_files().items():
        if args.check:
            if not path.is_file() or path.read_bytes() != content:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
    if stale:
        parser.error("generated CB6 fire assets are stale: " + ", ".join(stale))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
