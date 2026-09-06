#!/usr/bin/env python3
"""
generate_hemco_megan_oracle.py
──────────────────────────────
Regenerates tests/data/hemco_megan/hemco_3_12_1_megan_reference.csv
from the frozen HEMCO 3.12.1 MEGAN scalar equations.

This script is authoritative — the CSV it produces is the oracle used by
tests/test_hemco_megan_runtime.cpp.  Run it again to confirm byte-identical
output:

    python scripts/generate_hemco_megan_oracle.py \\
        > tests/data/hemco_megan/hemco_3_12_1_megan_reference.csv

If any value changes, the pinned constants in hemco_megan_stateless.hpp
must be audited before updating the CSV.
"""

import math

# ── Frozen HEMCO 3.12.1 constants ──────────────────────────────────────────

NORM_FAC = 1.0 / 1.0101081
LDF = 0.9996
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
PTOA_C1 = 2650.0  # differs from CECE default 3000
PTOA_C2 = 130.0  # differs from CECE default 99
PTOA_DOY_OFFSET = 18.0  # differs from CECE default 10
PAR_AVG_UMOL = 400.0  # direct µmol/m²/s; NOT W/m² × 4.766
T_AVG_15 = 297.0
REF_DOY = 180
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
DBTWN = 30.0


# ── Scalar gamma functions ──────────────────────────────────────────────────


def gamma_co2(co2):
    return GCO2_C1 / (1.0 + GCO2_C1 * GCO2_C2 * co2)


def gamma_t_li(T):
    return math.exp(BETA * (T - T_STD))


def gamma_t_ld(T, T_avg=T_AVG_15):
    e_opt = CEO * math.exp(E_OPT_C * (T_avg - 297.0))
    t_opt = T_OPT_C1 + T_OPT_C2 * (T_avg - 297.0)
    x = (1.0 / t_opt - 1.0 / T) / R
    num = e_opt * CT2 * math.exp(CT1 * x)
    den = CT2 - CT1 * (1.0 - math.exp(CT2 * x))
    return max(num / den, 0.0)


def gamma_par(pardr, pardf, suncos, doy=REF_DOY):
    if suncos <= 0.0:
        return 0.0
    pac_i = (pardr + pardf) * WM2_TO_UMOL
    bbb = GP_C1 + GP_C2 * (PAR_AVG_UMOL - 400.0)  # = 1.0
    ptoa = PTOA_C1 + PTOA_C2 * math.cos(2.0 * math.pi * (doy - PTOA_DOY_OFFSET) / 365.0)
    phi = pac_i / (suncos * ptoa)
    aaa = GP_C3 * bbb * phi - GP_C4 * phi**2
    return max(suncos * aaa, 0.0)


def gamma_lai(lai):
    if lai <= 0.0:
        return 0.0
    return LAI_C1 * lai / math.sqrt(1.0 + LAI_C2 * lai**2)


def gamma_age(cml, pml, T):
    ti = (5.0 + 0.7 * (300.0 - T)) if T <= 303.0 else 2.9
    tm = 2.3 * ti
    if cml == pml:
        fnew, fgro, fmat, fold = 0.0, 0.1, 0.8, 0.1
    elif cml > pml:
        fnew = (ti / DBTWN) * (1 - pml / cml) if DBTWN > ti else (1 - pml / cml)
        fmat = (
            (pml / cml) + ((DBTWN - tm) / DBTWN) * (1 - pml / cml)
            if DBTWN > tm
            else pml / cml
        )
        fgro = 1 - fnew - fmat
        fold = 0.0
    else:
        fnew, fgro = 0.0, 0.0
        fold = (pml - cml) / pml
        fmat = 1.0 - fold
    return max(fnew * ANEW + fgro * AGRO + fmat * AMAT + fold * AOLD, 0.0)


def isoprene_emission_factor(T, lai, lprev, pardr, pardf, suncos, co2=390.0):
    if lai <= 0.0:
        return 0.0
    gc = gamma_co2(co2)
    glai = gamma_lai(lai)
    gage = gamma_age(lai, lprev, T)
    gtli = gamma_t_li(T)
    gtld = gamma_t_ld(T)
    gpar = gamma_par(pardr, pardf, suncos)
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
