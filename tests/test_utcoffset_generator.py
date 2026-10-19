import importlib.util
import zoneinfo
from datetime import datetime, timezone
from pathlib import Path

import pytest

GENERATOR_PATH = (
    Path(__file__).resolve().parents[1]
    / "scripts"
    / "python"
    / "utcoffset_generator.py"
)
SPEC = importlib.util.spec_from_file_location("utcoffset_generator", GENERATOR_PATH)
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


def _require_zone(name):
    try:
        zoneinfo.ZoneInfo(name)
    except zoneinfo.ZoneInfoNotFoundError:
        pytest.skip(f"no tz database entry for {name}")


# Standard (non-DST) offsets in 15-minute units.
STANDARD_OFFSETS = [
    # Southern hemisphere: on DST in January, standard in July.
    ("Pacific/Auckland", 48),  # NZST +12, not NZDT +13
    ("Australia/Sydney", 40),  # AEST +10, not AEDT +11
    ("Australia/Melbourne", 40),
    ("Australia/Adelaide", 38),  # ACST +9:30, not ACDT +10:30
    ("Australia/Lord_Howe", 42),  # +10:30, not +11
    ("America/Santiago", -16),  # -4, not -3
    ("America/Sao_Paulo", -12),
    # Northern hemisphere: on DST in July, standard in January.
    ("America/New_York", -20),
    # zoneinfo's dst() reports 2h for Inuvik, so utcoffset() - dst() would give -8.
    ("America/Inuvik", -28),
    ("America/Edmonton", -28),
    ("Europe/Berlin", 4),
    ("Europe/London", 0),
    # tzdata models Ireland as +01 with a negative DST in winter; winter GMT is wanted.
    ("Europe/Dublin", 0),
    # Morocco is +01 all year apart from Ramadan, when it drops to +00.
    ("Africa/Casablanca", 4),
    ("Africa/El_Aaiun", 4),
    # No DST at all.
    ("Asia/Kolkata", 22),
    ("Asia/Kathmandu", 23),
    ("Pacific/Honolulu", -40),
]


@pytest.mark.parametrize(("zone", "expected"), STANDARD_OFFSETS)
def test_standard_offset_is_the_non_dst_offset(zone, expected):
    _require_zone(zone)

    assert generator.standard_offset_units(zone) == expected


def test_unknown_zone_falls_back_to_utc():
    assert generator.standard_offset_units("Not/AZone") == 0


def test_reference_dates_cover_both_hemispheres_winters():
    january, july = generator.STANDARD_TIME_REF_DATES

    assert (january.month, july.month) == (1, 7)
    assert all(ref.tzinfo is timezone.utc for ref in (january, july))
    # Away from a local-midnight date boundary and from any DST transition.
    assert all(ref.hour == 12 and ref.day == 15 for ref in (january, july))
    assert isinstance(january, datetime)
