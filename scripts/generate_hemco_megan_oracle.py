#!/usr/bin/env python3
"""
generate_hemco_megan_oracle.py
──────────────────────────────
Regenerates tests/data/hemco_megan/hemco_3_12_1_megan_reference.csv
from a Python transcription of the pinned HEMCO 3.12.1 MEGAN scalar equations.

The pinned HEMCO source is authoritative. This script creates deterministic
source-derived regression vectors used by tests/test_hemco_megan_runtime.cpp;
it does not run HEMCO or contain HEMCO-produced output. Run it again to confirm
byte-identical output:

    python scripts/generate_hemco_megan_oracle.py \\
        > tests/data/hemco_megan/hemco_3_12_1_megan_reference.csv

If any value changes, the pinned constants in hemco_megan_stateless.hpp
must be audited before updating the CSV.
"""

import math
import struct

# ── Frozen HEMCO 3.12.1 constants ──────────────────────────────────────────

LDF = 1.0
BETA = 0.13
T_STD = 303.0
R = 8.3144598e-3
CT1 = 95.0
CEO = 2.0
CT2 = 200.0
T_OPT_C1 = 313.0
T_OPT_C2 = 0.6
E_OPT_C = 0.08
WM2_TO_UMOL = 4.766
PTOA_C1 = 3000.0
PTOA_C2 = 99.0
PTOA_DOY_OFFSET = 10.0
PARDR_HISTORY_WM2 = 30.0
PARDF_HISTORY_WM2 = 48.0
TEMPERATURE_HISTORY_K = struct.unpack("f", struct.pack("f", 288.15))[0]
REF_DOY = 171  # 20 June 2021
GCO2_C1 = 8.9406
GCO2_C2 = 0.0024
LAI_C1 = 0.49
LAI_C2 = 0.2
GP_C1 = 1.0
GP_C2 = 0.0005
GP_C3 = 2.46
GP_C4 = 0.9
ANEW = 0.05
AGRO = 0.60
AMAT = 1.00
AOLD = 0.90
DAYS_BETWEEN_LAI = 1.0


def calc_norm_fac():
    """Reproduce HEMCO CALC_NORM_FAC instead of using its rounded comment."""
    pac_daily = 400.0
    phi = 0.6
    bbb = 1.0 + 0.0005 * (pac_daily - 400.0)
    aaa = 2.46 * bbb * phi - 0.9 * phi**2
    gamma_p = 0.866 * aaa
    gamma_lai = 0.49 * 5.0 / math.sqrt(1.0 + 0.2 * 5.0**2)
    gamma_age = 0.1 * 0.6 + 0.8 * 1.0 + 0.1 * 0.9
    e_opt = 2.0 * math.exp(0.08 * (297.0 - 297.0))
    t_opt = 313.0 + 0.6 * (297.0 - 297.0)
    x = (1.0 / t_opt - 1.0 / 303.0) / R
    gamma_t_ld = (
        e_opt * CT2 * math.exp(CT1 * x) / (CT2 - CT1 * (1.0 - math.exp(CT2 * x)))
    )
    gamma_standard = gamma_age * gamma_lai * gamma_p * gamma_t_ld
    return 1.0 / gamma_standard


NORM_FAC = calc_norm_fac()


# ── Scalar gamma functions ──────────────────────────────────────────────────


def gamma_co2(co2):
    return GCO2_C1 / (1.0 + GCO2_C1 * GCO2_C2 * co2)


def gamma_t_li(T):
    return math.exp(BETA * (T - T_STD))


def gamma_t_ld(T, temperature_history=TEMPERATURE_HISTORY_K):
    e_opt = CEO * math.exp(E_OPT_C * (temperature_history - 297.0))
    t_opt = T_OPT_C1 + T_OPT_C2 * (temperature_history - 297.0)
    x = (1.0 / t_opt - 1.0 / T) / R
    num = e_opt * CT2 * math.exp(CT1 * x)
    den = CT2 - CT1 * (1.0 - math.exp(CT2 * x))
    return max(num / den, 0.0)


def gamma_par(
    pardr,
    pardf,
    suncos,
    pardr_history=PARDR_HISTORY_WM2,
    pardf_history=PARDF_HISTORY_WM2,
    doy=REF_DOY,
):
    if suncos <= 0.0:
        return 0.0
    sin_beta = suncos
    pac_i = pardr * WM2_TO_UMOL + pardf * WM2_TO_UMOL
    pac_daily = pardr_history * WM2_TO_UMOL + pardf_history * WM2_TO_UMOL
    bbb = GP_C1 + GP_C2 * (pac_daily - 400.0)
    ptoa = PTOA_C1 + PTOA_C2 * math.cos(2.0 * math.pi * (doy - PTOA_DOY_OFFSET) / 365.0)
    phi = pac_i / (sin_beta * ptoa)
    aaa = GP_C3 * bbb * phi - GP_C4 * phi**2
    gamma = sin_beta * aaa
    beta_degrees = math.asin(sin_beta) * 180.0 / math.pi
    if beta_degrees < 1.0 and gamma > 0.1:
        gamma = 0.0
    return max(gamma, 0.0)


def gamma_lai(lai):
    if lai <= 0.0:
        return 0.0
    return LAI_C1 * lai / math.sqrt(1.0 + LAI_C2 * lai**2)


def gamma_age(
    cml,
    pml,
    temperature_history=TEMPERATURE_HISTORY_K,
    days_between_lai=DAYS_BETWEEN_LAI,
):
    ti = (
        (5.0 + 0.7 * (300.0 - temperature_history))
        if temperature_history <= 303.0
        else 2.9
    )
    tm = 2.3 * ti
    if cml == pml:
        fnew, fgro, fmat, fold = 0.0, 0.1, 0.8, 0.1
    elif cml > pml:
        fnew = (
            (ti / days_between_lai) * (1 - pml / cml)
            if days_between_lai > ti
            else (1 - pml / cml)
        )
        fmat = (
            (pml / cml) + ((days_between_lai - tm) / days_between_lai) * (1 - pml / cml)
            if days_between_lai > tm
            else pml / cml
        )
        fgro = 1 - fnew - fmat
        fold = 0.0
    else:
        fnew, fgro = 0.0, 0.0
        fold = (pml - cml) / pml
        fmat = 1.0 - fold
    return max(fnew * ANEW + fgro * AGRO + fmat * AMAT + fold * AOLD, 0.0)


def isoprene_emission_factor(
    T,
    lai,
    lprev,
    pardr,
    pardf,
    suncos,
    co2=390.0,
    apply_co2_inhibition=True,
    temperature_history=TEMPERATURE_HISTORY_K,
    pardr_history=PARDR_HISTORY_WM2,
    pardf_history=PARDF_HISTORY_WM2,
    days_between_lai=DAYS_BETWEEN_LAI,
    doy=REF_DOY,
):
    if lai <= 0.0:
        return 0.0
    gc = gamma_co2(co2) if apply_co2_inhibition else 1.0
    glai = gamma_lai(lai)
    gage = gamma_age(lai, lprev, temperature_history, days_between_lai)
    gtli = gamma_t_li(T)
    gtld = gamma_t_ld(T, temperature_history)
    gpar = gamma_par(pardr, pardf, suncos, pardr_history, pardf_history, doy)
    comb = (1.0 - LDF) * gtli + LDF * gpar * gtld
    return NORM_FAC * gage * glai * gc * comb


# ── Oracle cases ────────────────────────────────────────────────────────────

CASES = [
    # (case_id, T_K, lai, lai_prev, pardr_Wm2, pardf_Wm2, suncos, gwetroot, co2_ppm)
    ("std_daytime", 303.0, 4.0, 4.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("nighttime", 303.0, 4.0, 4.0, 0.0, 0.0, -0.5, 0.5, 390.0),
    ("zero_lai", 303.0, 0.0, 0.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("cold_t", 260.0, 4.0, 4.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("hot_t", 330.0, 4.0, 4.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("low_co2", 303.0, 4.0, 4.0, 200.0, 100.0, 0.7, 0.5, 280.0),
    ("high_co2", 303.0, 4.0, 4.0, 200.0, 100.0, 0.7, 0.5, 560.0),
    ("growing_lai", 303.0, 5.0, 3.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("senescing_lai", 303.0, 3.0, 5.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("low_lai", 303.0, 1.0, 1.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("high_lai", 303.0, 8.0, 8.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("zero_suncos", 303.0, 4.0, 4.0, 200.0, 100.0, 0.0, 0.5, 390.0),
    ("high_par", 303.0, 4.0, 4.0, 500.0, 300.0, 0.9, 0.5, 390.0),
    ("t_opt", 313.0, 4.0, 4.0, 200.0, 100.0, 0.7, 0.5, 390.0),
    ("warm_midday", 298.0, 3.5, 3.5, 300.0, 150.0, 0.8, 0.5, 400.0),
    (
        "effective_previous_lai_precision",
        307.0,
        4.123456789,
        2.234567891,
        240.0,
        65.0,
        0.78,
        0.5,
        415.0,
    ),
]


def main():
    header = (
        "case_id,T_K,lai_m2m2,lai_prev_m2m2,pardr_Wm2,pardf_Wm2,"
        "suncos,gwetroot,co2_ppm,expected_emission_per_aef"
    )
    print(header)
    for cid, T, lai, lprev, pdr, pdf, sc, gw, co2 in CASES:
        v = isoprene_emission_factor(T, lai, lprev, pdr, pdf, sc, co2=co2)
        print(f"{cid},{T},{lai},{lprev},{pdr},{pdf},{sc},{gw},{co2},{v:.18e}")


if __name__ == "__main__":
    main()
