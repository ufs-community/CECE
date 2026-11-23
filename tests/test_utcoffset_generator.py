import sys
import zoneinfo
from datetime import datetime, timezone
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))

import utcoffset_generator as generator


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


def test_unknown_zone_raises():
    with pytest.raises(zoneinfo.ZoneInfoNotFoundError):
        generator.standard_offset_units("Not/AZone")


def test_offset_outside_int8_raises(monkeypatch):
    # Real zones stay within +-24 h, so force an out-of-range quarter-hour count.
    monkeypatch.setattr(generator, "round", lambda _: 200, raising=False)

    with pytest.raises(ValueError, match="does not fit in int8"):
        generator.standard_offset_units("UTC")


def test_reference_dates_cover_both_hemispheres_winters():
    january, july = generator.STANDARD_TIME_REF_DATES

    assert (january.month, july.month) == (1, 7)
    assert all(ref.tzinfo is timezone.utc for ref in (january, july))
    # Away from a local-midnight date boundary and from any DST transition.
    assert all(ref.hour == 12 and ref.day == 15 for ref in (january, july))
    assert isinstance(january, datetime)
