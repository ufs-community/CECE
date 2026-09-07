#!/usr/bin/env python3
"""Synthetic global invariants for the HEMCO 3.12.1 MEGAN source transcription.

The historical filename is retained to avoid unnecessary CMake/CI churn. This
module uses deterministic analytical drivers on the GEOS 72x46 coordinate
layout. It does not execute HEMCO and is not HEMCO-versus-CECE runtime parity.
"""

import math
import struct

NX = 72
NY = 46
DOY = 171  # 20 June 2021

NORM_FAC = 0.9899364002107353
LDF = 1.0
BETA = 0.13
T_STD = 303.0
R = 8.3144598e-3
CT1 = 95.0
CEO = 2.0
CT2 = 200.0
WM2_TO_UMOL = 4.766
PTOA_C1 = 3000.0
PTOA_C2 = 99.0
PTOA_OFFSET = 10.0
PARDR_HISTORY_WM2 = 30.0
PARDF_HISTORY_WM2 = 48.0
TEMPERATURE_HISTORY_K = struct.unpack("f", struct.pack("f", 288.15))[0]


def longitudes():
    return [-180.0 + 5.0 * i for i in range(NX)]


def latitudes():
    return [-89.0] + [-86.0 + 4.0 * i for i in range(44)] + [89.0]


def gamma_co2(co2_ppm):
    return 8.9406 / (1.0 + 8.9406 * 0.0024 * co2_ppm)


def gamma_lai(lai):
    if lai <= 0.0:
        return 0.0
    return 0.49 * lai / math.sqrt(1.0 + 0.2 * lai * lai)


def gamma_t_ld(temperature, temperature_history=TEMPERATURE_HISTORY_K):
    e_opt = CEO * math.exp(0.08 * (temperature_history - 297.0))
    t_opt = 313.0 + 0.6 * (temperature_history - 297.0)
    x = (1.0 / t_opt - 1.0 / temperature) / R
    value = e_opt * CT2 * math.exp(CT1 * x) / (CT2 - CT1 * (1.0 - math.exp(CT2 * x)))
    return max(value, 0.0)


def gamma_age(lai, lai_prev, temperature_history=TEMPERATURE_HISTORY_K):
    ti = (
        5.0 + 0.7 * (300.0 - temperature_history)
        if temperature_history <= 303.0
        else 2.9
    )
    tm = 2.3 * ti
    days_between_lai = 1.0
    if lai == lai_prev:
        fractions = (0.0, 0.1, 0.8, 0.1)
    elif lai > lai_prev:
        fnew = (
            (ti / days_between_lai) * (1.0 - lai_prev / lai)
            if days_between_lai > ti
            else 1.0 - lai_prev / lai
        )
        fmat = (
            lai_prev / lai
            + (days_between_lai - tm) / days_between_lai * (1.0 - lai_prev / lai)
            if days_between_lai > tm
            else lai_prev / lai
        )
        fractions = (fnew, 1.0 - fnew - fmat, fmat, 0.0)
    else:
        fold = (lai_prev - lai) / lai_prev
        fractions = (0.0, 0.0, 1.0 - fold, fold)
    weights = (0.05, 0.60, 1.00, 0.90)
    return max(
        sum(fraction * weight for fraction, weight in zip(fractions, weights)), 0.0
    )


def gamma_par(
    pardr_wm2,
    pardf_wm2,
    suncos,
    pardr_history_wm2=PARDR_HISTORY_WM2,
    pardf_history_wm2=PARDF_HISTORY_WM2,
    doy=DOY,
):
    if suncos <= 0.0:
        return 0.0
    pac_instant = pardr_wm2 * WM2_TO_UMOL + pardf_wm2 * WM2_TO_UMOL
    pac_daily = pardr_history_wm2 * WM2_TO_UMOL + pardf_history_wm2 * WM2_TO_UMOL
    bbb = 1.0 + 0.0005 * (pac_daily - 400.0)
    ptoa = PTOA_C1 + PTOA_C2 * math.cos(2.0 * math.pi * (doy - PTOA_OFFSET) / 365.0)
    phi = pac_instant / (suncos * ptoa)
    gamma = suncos * (2.46 * bbb * phi - 0.9 * phi * phi)
    beta_degrees = math.asin(suncos) * 180.0 / math.pi
    if beta_degrees < 1.0 and gamma > 0.1:
        gamma = 0.0
    return max(gamma, 0.0)


def emission_factor(
    temperature,
    lai,
    lai_prev,
    pardr_wm2,
    pardf_wm2,
    suncos,
    co2_ppm=390.0,
    apply_co2_inhibition=True,
    temperature_history=TEMPERATURE_HISTORY_K,
    pardr_history=PARDR_HISTORY_WM2,
    pardf_history=PARDF_HISTORY_WM2,
    doy=DOY,
):
    if lai <= 0.0:
        return 0.0
    gco2 = gamma_co2(co2_ppm) if apply_co2_inhibition else 1.0
    return (
        NORM_FAC
        * gamma_age(lai, lai_prev, temperature_history)
        * gamma_lai(lai)
        * gamma_par(
            pardr_wm2,
            pardf_wm2,
            suncos,
            pardr_history,
            pardf_history,
            doy,
        )
        * gamma_t_ld(temperature, temperature_history)
        * gco2
    )


def smooth_box(lon, lat, lon1, lon2, lat1, lat2, edge=3.0):
    def sigmoid(value, lower, upper):
        return 0.5 * (
            math.tanh((value - lower) / edge) - math.tanh((value - upper) / edge)
        )

    return min(max(sigmoid(lon, lon1, lon2) * sigmoid(lat, lat1, lat2), 0.0), 1.0)


def global_fields():
    fields = []
    for lat in latitudes():
        abs_lat = abs(lat)
        solar = min(max(math.cos(math.radians(lat - 23.0)) * 0.85, 0.0), 1.0)
        if abs_lat < 15.0:
            base_lai, base_aef = 5.0, 3.0e-9
        elif abs_lat < 35.0:
            base_lai, base_aef = 3.5, 2.0e-9
        elif abs_lat < 55.0:
            base_lai, base_aef = 2.5, 1.5e-9
        elif abs_lat < 70.0:
            base_lai, base_aef = 1.5, 0.8e-9
        else:
            base_lai, base_aef = 0.0, 0.3e-9
        for lon in longitudes():
            land = min(
                max(
                    smooth_box(lon, lat, -125, -60, 10, 72)
                    + smooth_box(lon, lat, -10, 40, 36, 72)
                    + smooth_box(lon, lat, 25, 145, 0, 72)
                    + smooth_box(lon, lat, -80, -35, -55, 12),
                    0.0,
                ),
                1.0,
            )
            lai = base_lai * land
            aef = base_aef * land if lai > 0.0 else 0.0
            temperature = min(max(302.0 - 0.10 * abs_lat, 255.0), 312.0)
            fields.append((temperature, lai, aef, 400.0 * solar, 120.0 * solar, solar))
    return fields


def test_exact_geos_four_by_five_coordinate_layout():
    lons = longitudes()
    lats = latitudes()
    assert len(lons) == NX and len(lats) == NY
    assert (lons[0], lons[-1]) == (-180.0, 175.0)
    assert lats[:3] == [-89.0, -86.0, -82.0]
    assert lats[-3:] == [82.0, 86.0, 89.0]


def test_source_constants_and_cold_start_contract():
    assert LDF == 1.0
    assert (PTOA_C1, PTOA_C2, PTOA_OFFSET) == (3000.0, 99.0, 10.0)
    assert (PARDR_HISTORY_WM2, PARDF_HISTORY_WM2) == (30.0, 48.0)
    assert TEMPERATURE_HISTORY_K == struct.unpack("f", struct.pack("f", 288.15))[0]
    assert DOY == 171
    exact_lai = 4.123456789
    projected_lai = struct.unpack("f", struct.pack("f", exact_lai))[0]
    assert projected_lai != exact_lai
    assert gamma_age(exact_lai, exact_lai) != gamma_age(exact_lai, projected_lai)


def test_global_finite_nonnegative_and_mask_contract():
    positive = 0
    for temperature, lai, aef, pardr, pardf, suncos in global_fields():
        factor = emission_factor(temperature, lai, lai, pardr, pardf, suncos)
        emission = aef * factor
        assert math.isfinite(emission) and emission >= 0.0
        assert (emission > 0.0) == (lai > 0.0 and aef > 0.0 and suncos > 0.0)
        positive += emission > 0.0
    assert positive > 100


def test_history_date_and_co2_switches_are_active():
    args = (303.0, 4.0, 4.0, 200.0, 100.0, 0.7)
    baseline = emission_factor(*args)
    assert baseline != emission_factor(*args, pardr_history=200.0, pardf_history=200.0)
    assert baseline != emission_factor(*args, temperature_history=297.0)
    assert baseline != emission_factor(*args, doy=1)
    assert emission_factor(
        *args, co2_ppm=390.0, apply_co2_inhibition=False
    ) == emission_factor(*args, co2_ppm=560.0, apply_co2_inhibition=False)
    assert emission_factor(*args, co2_ppm=390.0) > emission_factor(*args, co2_ppm=560.0)


if __name__ == "__main__":
    tests = [
        value for name, value in sorted(globals().items()) if name.startswith("test_")
    ]
    for test in tests:
        test()
    print(f"PASS: {len(tests)} synthetic HEMCO-source-conformance checks")
