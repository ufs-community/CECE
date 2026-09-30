import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from prepare_global_pollen_rf import (  # noqa: E402
    nested_count,
    parse_count_specs,
    resolve_record_counts,
)


def test_nested_count_sums_ambee_pollen_types():
    record = {
        "Count": {
            "grass_pollen": 2,
            "tree_pollen": 35,
            "weed_pollen": 35,
        }
    }

    assert (
        nested_count(
            record,
            "Count.grass_pollen+Count.tree_pollen+Count.weed_pollen",
        )
        == 72
    )


def test_nested_count_reads_species_key_with_slashes():
    record = {"Species": {"Tree": {"Cypress/Juniper/Cedar": 12}}}

    assert nested_count(record, "Species.Tree.Cypress/Juniper/Cedar") == 12


def test_parse_count_specs_supports_batch_and_legacy_modes():
    assert parse_count_specs(
        None,
        ["grass=Count.grass_pollen", "elm=Species.Tree.Elm"],
    ) == [("grass", "Count.grass_pollen"), ("elm", "Species.Tree.Elm")]
    assert parse_count_specs("elm", ["Species.Tree.Elm"]) == [
        ("elm", "Species.Tree.Elm")
    ]


def test_missing_optional_species_is_skipped_without_zero_filling():
    record = {"Count": {"grass_pollen": 4}}
    specs = [
        ("grass", "Count.grass_pollen"),
        ("elm", "Species.Tree.Elm"),
    ]

    assert resolve_record_counts(record, specs, "skip") == ([("grass", 4.0)], 1)
    with pytest.raises(KeyError):
        resolve_record_counts(record, specs, "error")
