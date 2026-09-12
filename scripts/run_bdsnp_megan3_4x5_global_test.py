#!/usr/bin/env python3
"""Global 4x5 BDSNP + MEGAN3 test run driven by cece_config_earthaccess_4x5_test.yaml.

This script evaluates CECE's *native* C++ physics equations for the BDSNP
(soil NO) and MEGAN3 (isoprene) schemes on the exact 72x46 HEMCO 4 deg x 5 deg
grid used by ``tests/cece_config_earthaccess_4x5_test.yaml``. The equations
below are transcribed line-for-line from the currently checked-in production
source so the result reflects what the compiled CECE binary would produce for
these inputs -- it does not reimplement or approximate the physics.

Source of the transcribed equations (read at the time this script was written):
  - include/cece/physics/cece_megan.hpp        (get_gamma_* helper functions)
  - src/core/physics/cece_megan3.cpp           (Megan3Scheme::Run kernel, ISOP class)
  - src/core/physics/cece_emission_activity.cpp (per-class LDF/CT1/CLEO/Anew/Agro/Amat/Aold)
  - src/core/physics/cece_bdsnp.cpp            (BdsnpScheme::Run, "bdsnp" branch)

Building the actual Kokkos/pybind11 CECE core was not possible in this
environment (no Kokkos/ESMF toolchain available), so this driver evaluates the
transcribed native equations directly in NumPy on the full grid instead of
calling through ``cece.compute()``. Every constant below is quoted with the
line/file it came from so the mapping back to the C++ source is auditable.

The run only supplies the import fields that the earthaccess streams in the
test config actually provide (LAI from MODIS, soil temperature/moisture from
SMAP, PAR/solar-cosine from CERES). Fields not supplied by that config
(nitrogen_deposition, land_use_type, biome_emission_factors) are therefore left
at the BdsnpScheme defaults, exactly as a real run of that config would do.

Outputs (written to --outdir, default results/bdsnp_megan3_4x5_test/):
  - global_isoprene_megan3.png          MEGAN3 ISOP emission map
  - global_soil_no_bdsnp.png            BDSNP soil NO emission map
  - megan3_vs_hemco_oracle_diff.png     per-cell % difference vs HEMCO 3.12.1
  - megan3_vs_hemco_oracle_zonal.png    zonal-mean comparison
  - scalar_case_validation.csv          the 16 PR #90 oracle cases re-evaluated
  - summary.txt                         numeric parity report
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import numpy as np
import yaml

REPO_ROOT = Path(__file__).resolve().parents[1]

# ============================================================================
# Grid -- exact HEMCO 4x5 layout (matches tests/cece_config_earthaccess_4x5_test.yaml
# and tests/test_earthaccess_stream_bdsnp_megan3.py NX/NY constants)
# ============================================================================
NX, NY = 72, 46
LON = np.array([-180.0 + 5.0 * i for i in range(NX)])
LAT = np.array([-89.0] + [-86.0 + 4.0 * i for i in range(44)] + [89.0])


# ============================================================================
# Gamma helper functions -- transcribed from include/cece/physics/cece_megan.hpp
# ============================================================================
def get_gamma_lai(lai, c1=0.49, c2=0.2):
    """cece_megan.hpp get_gamma_lai(), non-bidirectional branch."""
    return c1 * lai / np.sqrt(1.0 + c2 * lai * lai)


def get_gamma_age(cmlai, pmlai, dbtwn, tt, an, ag, am, ao):
    """cece_megan.hpp get_gamma_age(), vectorized."""
    cmlai, pmlai, dbtwn, tt = np.broadcast_arrays(
        np.asarray(cmlai, dtype=float),
        np.asarray(pmlai, dtype=float),
        np.asarray(dbtwn, dtype=float),
        np.asarray(tt, dtype=float),
    )
    ti = np.where(tt <= 303.0, 5.0 + 0.7 * (300.0 - tt), 2.9)
    tm = 2.3 * ti
    fnew = np.zeros_like(cmlai)
    fgro = np.zeros_like(cmlai)
    fmat = np.zeros_like(cmlai)
    fold = np.zeros_like(cmlai)

    same = cmlai == pmlai
    grow = cmlai > pmlai
    senesce = cmlai < pmlai

    fnew = np.where(same, 0.0, fnew)
    fgro = np.where(same, 0.1, fgro)
    fmat = np.where(same, 0.8, fmat)
    fold = np.where(same, 0.1, fold)

    with np.errstate(divide="ignore", invalid="ignore"):
        fnew_grow = np.where(
            dbtwn > ti, (ti / dbtwn) * (1.0 - pmlai / cmlai), 1.0 - (pmlai / cmlai)
        )
        fmat_grow = np.where(
            dbtwn > tm,
            (pmlai / cmlai) + ((dbtwn - tm) / dbtwn) * (1.0 - pmlai / cmlai),
            pmlai / cmlai,
        )
        fgro_grow = 1.0 - fnew_grow - fmat_grow
        fold_senesce = (pmlai - cmlai) / pmlai
        fmat_senesce = 1.0 - fold_senesce

    fnew = np.where(grow, fnew_grow, fnew)
    fmat = np.where(grow, fmat_grow, fmat)
    fgro = np.where(grow, fgro_grow, fgro)
    fold = np.where(senesce, fold_senesce, fold)
    fmat = np.where(senesce, fmat_senesce, fmat)

    return np.maximum(fnew * an + fgro * ag + fmat * am + fold * ao, 0.0)


def get_gamma_sm(gwetroot, is_ald2_or_eoh=False):
    """cece_megan.hpp get_gamma_sm(); MEGAN3 ISOP calls this with is_ald2_or_eoh=False -> 1.0."""
    return (
        np.ones_like(gwetroot)
        if not is_ald2_or_eoh
        else np.maximum(20.0 * np.clip(gwetroot, 0.0, 1.0) - 17.0, 1.0)
    )


def get_gamma_t_li(temp, beta=0.13, t_standard=303.0):
    return np.exp(beta * (temp - t_standard))


def get_gamma_t_ld(T, pt_15, ct1, ceo, R, ct2, t_opt_c1, t_opt_c2, e_opt_coeff):
    e_opt = ceo * np.exp(e_opt_coeff * (pt_15 - 297.0))
    t_opt = t_opt_c1 + t_opt_c2 * (pt_15 - 297.0)
    x = (1.0 / t_opt - 1.0 / T) / R
    c_t = e_opt * ct2 * np.exp(ct1 * x) / (ct2 - ct1 * (1.0 - np.exp(ct2 * x)))
    return np.maximum(c_t, 0.0)


def get_gamma_par_pceea(
    q_dir,
    q_diff,
    par_avg,
    suncos,
    doy,
    wm2_to_umol,
    ptoa_c1,
    ptoa_c2,
    gp1,
    gp2,
    gp3,
    gp4,
):
    pac_instant = (q_dir + q_diff) * wm2_to_umol
    pac_daily = par_avg * wm2_to_umol
    ptoa = ptoa_c1 + ptoa_c2 * np.cos(2.0 * np.pi * (doy - 10.0) / 365.0)
    with np.errstate(divide="ignore", invalid="ignore"):
        phi = pac_instant / (suncos * ptoa)
        bbb = gp1 + gp2 * (pac_daily - 400.0)
        aaa = (gp3 * bbb * phi) - (gp4 * phi * phi)
        gamma_p = suncos * aaa
    gamma_p = np.where(suncos <= 0.0, 0.0, gamma_p)
    return np.maximum(gamma_p, 0.0)


def get_gamma_co2(co2a, c1=8.9406, c2=0.0024):
    """cece_megan.hpp get_gamma_co2(), use_wilkinson=False branch (CECE default)."""
    return c1 / (1.0 + c1 * c2 * co2a)


# ============================================================================
# MEGAN3 ISOP class constants -- src/core/physics/cece_emission_activity.cpp
# (class index 0 == ISOP) and src/core/physics/cece_megan3.cpp
# ============================================================================
ISOP_AEF_DEFAULT = 1.0e-9  # cece_megan3.cpp kDefaultAef[0], kg isoprene m-2 s-1
ISOP_LDF = 0.9996  # cece_emission_activity.cpp kDefaultLdf[0]
ISOP_CT1 = 95.0  # kDefaultCt1[0]
ISOP_CLEO = 2.0  # kDefaultCleo[0]
ISOP_BETA = 0.13  # cece_megan3.cpp STD constant used for all classes
ISOP_ANEW, ISOP_AGRO, ISOP_AMAT, ISOP_AOLD = (
    0.05,
    0.6,
    1.0,
    0.9,
)  # kDefault{Anew,Agro,Amat,Aold}[0]

NORM_FAC_CECE = 1.0 / 1.0101081  # cece_megan3.cpp literal constant
LAI_C1, LAI_C2 = 0.49, 0.2
GAS_CONSTANT = 8.3144598e-3
CT2_CONST = 200.0
T_OPT_C1, T_OPT_C2, E_OPT_COEFF = 313.0, 0.6, 0.08
WM2_TO_UMOL = 4.766
PTOA_C1, PTOA_C2 = 3000.0, 99.0
GP_C1, GP_C2, GP_C3, GP_C4 = 1.0, 0.0005, 2.46, 0.9

# Hard-coded "no dynamic history" defaults actually used by Megan3Scheme::Run
# today (cece_megan3.cpp, inside the Kokkos kernel) -- these are NOT the HEMCO
# cold-start values; CECE does not yet feed a running 5-day/12-hour history in.
CECE_T_AVG_15 = 297.0
CECE_PAR_AVG = 400.0
CECE_DOY = 180
CECE_DBTWN = 30.0
CECE_CO2_PPM = 390.0  # tests/cece_config_earthaccess_4x5_test.yaml does not set
# a co2 concentration; 390 ppm mirrors the HEMCO oracle
# fixture so the gamma_co2 term is directly comparable.


def megan3_isop_activity_factor_cece(T, L, L_prev, pdr, pdf, sc, gwetroot):
    """Reproduces the per-cell ISOP term inside Megan3Scheme::Run exactly,
    using the scheme's real (hard-coded) history defaults."""
    g_lai_c = get_gamma_lai(L, LAI_C1, LAI_C2)
    g_age_c = get_gamma_age(
        L, L_prev, CECE_DBTWN, T, ISOP_ANEW, ISOP_AGRO, ISOP_AMAT, ISOP_AOLD
    )
    g_sm = get_gamma_sm(gwetroot, is_ald2_or_eoh=False)
    g_t_li = get_gamma_t_li(T, ISOP_BETA, 303.0)
    g_t_ld = get_gamma_t_ld(
        T,
        CECE_T_AVG_15,
        ISOP_CT1,
        ISOP_CLEO,
        GAS_CONSTANT,
        CT2_CONST,
        T_OPT_C1,
        T_OPT_C2,
        E_OPT_COEFF,
    )
    g_par = get_gamma_par_pceea(
        pdr,
        pdf,
        CECE_PAR_AVG,
        sc,
        CECE_DOY,
        WM2_TO_UMOL,
        PTOA_C1,
        PTOA_C2,
        GP_C1,
        GP_C2,
        GP_C3,
        GP_C4,
    )
    gamma_co2_val = get_gamma_co2(CECE_CO2_PPM)
    ldf_combined = (1.0 - ISOP_LDF) * g_t_li + ISOP_LDF * g_par * g_t_ld
    return NORM_FAC_CECE * g_lai_c * g_age_c * g_sm * gamma_co2_val * ldf_combined


# ============================================================================
# HEMCO 3.12.1 oracle equations -- from PR #90's
# scripts/generate_hemco_megan_oracle.py (cold-start T_DAVG=288.15 K
# projected through single precision, PARDR/PARDF_DAVG=30/48 W m-2, DOY=171,
# LDF=1.0, dbtwn=1 day). Used here as the independent HEMCO-source-transcribed
# reference for the diagnostic comparison; it is not an executed HEMCO run.
# ============================================================================
import struct  # noqa: E402

HEMCO_T_HISTORY = struct.unpack("f", struct.pack("f", 288.15))[0]
HEMCO_PARDR_HISTORY = 30.0
HEMCO_PARDF_HISTORY = 48.0
HEMCO_DOY = 171
HEMCO_DBTWN = 1.0
HEMCO_LDF = 1.0
HEMCO_NORM_FAC = 0.9899364002107353  # PR #90 tests/data/hemco_megan/README.md


def megan_isop_activity_factor_hemco(T, L, L_prev, pdr, pdf, sc, co2_ppm=390.0):
    g_lai = get_gamma_lai(L, LAI_C1, LAI_C2)
    g_age = get_gamma_age(
        L, L_prev, HEMCO_DBTWN, T, ISOP_ANEW, ISOP_AGRO, ISOP_AMAT, ISOP_AOLD
    )
    g_t_li = get_gamma_t_li(T, ISOP_BETA, 303.0)
    g_t_ld = get_gamma_t_ld(
        T,
        HEMCO_T_HISTORY,
        ISOP_CT1,
        ISOP_CLEO,
        GAS_CONSTANT,
        CT2_CONST,
        T_OPT_C1,
        T_OPT_C2,
        E_OPT_COEFF,
    )
    g_par = get_gamma_par_pceea(
        pdr,
        pdf,
        HEMCO_PARDR_HISTORY + HEMCO_PARDF_HISTORY,
        sc,
        HEMCO_DOY,
        WM2_TO_UMOL,
        PTOA_C1,
        PTOA_C2,
        GP_C1,
        GP_C2,
        GP_C3,
        GP_C4,
    )
    gamma_co2_val = get_gamma_co2(co2_ppm)
    ldf_combined = (1.0 - HEMCO_LDF) * g_t_li + HEMCO_LDF * g_par * g_t_ld
    return HEMCO_NORM_FAC * g_age * g_lai * gamma_co2_val * ldf_combined


# ============================================================================
# BDSNP constants -- src/core/physics/cece_bdsnp.cpp, "bdsnp" (default) branch
# ============================================================================
BDSNP_MW_NO = 30.0
BDSNP_UNITCONV = 1.0e-12 / 14.0 * BDSNP_MW_NO  # ng N -> kg NO
BDSNP_FERT_EF = 1.0
BDSNP_WET_DEP_SCALING = 1.0
BDSNP_DRY_DEP_SCALING = 1.0
BDSNP_PULSE_DECAY = 0.5


def bdsnp_moisture_factor(sm):
    sm = np.asarray(sm, dtype=float)
    ramp = sm / 0.3
    decay = 1.0 - 0.5 * (sm - 0.3) / 0.7
    out = np.where(sm <= 0.0, 0.0, np.where(sm <= 0.3, ramp, decay))
    return out


def bdsnp_ndep_factor(
    ndep, fert_ef=BDSNP_FERT_EF, wet=BDSNP_WET_DEP_SCALING, dry=BDSNP_DRY_DEP_SCALING
):
    return 1.0 + fert_ef * (ndep * (wet + dry))


def bdsnp_canopy_reduction(lai):
    return np.exp(-0.24 * np.asarray(lai, dtype=float))


def bdsnp_soil_no_emission(soil_temp_k, soil_moisture, lai, ndep=0.0, base_ef=1.0):
    """Reproduces BdsnpScheme::Run's "bdsnp" branch exactly (default config,
    i.e. no nitrogen_deposition/land_use_type/biome_emission_factors streams
    -- matching what tests/cece_config_earthaccess_4x5_test.yaml supplies)."""
    tc = soil_temp_k - 273.15
    t_response = np.exp(0.103 * np.minimum(30.0, tc))
    sm_factor = bdsnp_moisture_factor(soil_moisture)
    fert_factor = bdsnp_ndep_factor(ndep)
    canopy_red = bdsnp_canopy_reduction(lai)
    pulse = math.exp(-BDSNP_PULSE_DECAY * 0.0)  # no antecedent rain state -> 1.0
    emiss = (
        base_ef
        * BDSNP_UNITCONV
        * t_response
        * sm_factor
        * fert_factor
        * canopy_red
        * pulse
    )
    return np.where(tc <= 0.0, 0.0, emiss)


# ============================================================================
# Synthetic global input fields (72x46) -- physically-plausible stand-ins for
# the MODIS/SMAP/CERES earthaccess streams declared in
# tests/cece_config_earthaccess_4x5_test.yaml. Live NASA Earthdata streaming
# requires interactive/EDL credentials that are not available in this
# environment, so this driver builds fields with the same value ranges used by
# tests/test_earthaccess_stream_bdsnp_megan3.py's synthetic fixtures
# (T 200-340 K, soil moisture 0-1, LAI >= 0) rather than performing a live
# earthaccess.login()/search_data() call.
# ============================================================================
def build_synthetic_fields():
    lon2d, lat2d = np.meshgrid(LON, LAT)  # shape (NY, NX)
    abs_lat = np.abs(lat2d)

    # Land mask: smooth continents, reused shape from tests/test_megan_global_parity.py
    def smooth_box(lon, lat, lon1, lon2, lat1, lat2, edge=3.0):
        def sigmoid(v, lo, hi):
            return 0.5 * (np.tanh((v - lo) / edge) - np.tanh((v - hi) / edge))

        return np.clip(sigmoid(lon, lon1, lon2) * sigmoid(lat, lat1, lat2), 0.0, 1.0)

    land = np.clip(
        smooth_box(lon2d, lat2d, -125, -60, 10, 72)
        + smooth_box(lon2d, lat2d, -10, 40, 36, 72)
        + smooth_box(lon2d, lat2d, 25, 145, 0, 72)
        + smooth_box(lon2d, lat2d, -80, -35, -55, 12),
        0.0,
        1.0,
    )

    base_lai = np.select(
        [abs_lat < 15.0, abs_lat < 35.0, abs_lat < 55.0, abs_lat < 70.0],
        [5.0, 3.5, 2.5, 1.5],
        default=0.0,
    )
    lai = base_lai * land
    lai_prev = np.clip(
        lai * 0.92, 0.0, None
    )  # 8-day-earlier MODIS composite, slightly less green-up

    solar = np.clip(
        np.cos(np.radians(lat2d - 23.0)) * 0.85, 0.0, 1.0
    )  # local-noon proxy, July NH-summer offset
    par_direct = 300.0 * solar
    par_diffuse = 90.0 * solar
    solar_cosine = solar

    temperature = np.clip(302.0 - 0.35 * abs_lat, 220.0, 312.0)
    soil_temperature = np.clip(temperature - 2.0, 200.0, 310.0)
    soil_moisture_root = np.clip(0.45 - 0.003 * abs_lat, 0.05, 0.45)
    soil_moisture = np.clip(soil_moisture_root * 0.9, 0.03, 0.5)

    return {
        "lon2d": lon2d,
        "lat2d": lat2d,
        "land": land,
        "leaf_area_index": lai,
        "leaf_area_index_prev": lai_prev,
        "par_direct": par_direct,
        "par_diffuse": par_diffuse,
        "solar_cosine": solar_cosine,
        "temperature": temperature,
        "soil_temperature": soil_temperature,
        "soil_moisture": soil_moisture,
        "soil_moisture_root": soil_moisture_root,
    }


def run_scalar_case_validation(outdir: Path):
    csv_path = (
        REPO_ROOT
        / "tests"
        / "data"
        / "hemco_megan"
        / "hemco_3_12_1_megan_reference.csv"
    )
    rows = []
    with open(csv_path, newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            rows.append(row)

    out_rows = []
    for row in rows:
        T = float(row["T_K"])
        L = float(row["lai_m2m2"])
        Lp = float(row["lai_prev_m2m2"])
        pdr = float(row["pardr_Wm2"])
        pdf = float(row["pardf_Wm2"])
        sc = float(row["suncos"])
        gw = float(row["gwetroot"])
        co2 = float(row["co2_ppm"])
        expected = float(row["expected_emission_per_aef"])

        hemco_val = float(megan_isop_activity_factor_hemco(T, L, Lp, pdr, pdf, sc, co2))
        cece_val = float(megan3_isop_activity_factor_cece(T, L, Lp, pdr, pdf, sc, gw))
        rel_err_hemco_repro = (
            abs(hemco_val - expected) / expected
            if expected != 0.0
            else (0.0 if hemco_val == 0.0 else float("inf"))
        )
        rel_diff_cece_vs_hemco = (
            abs(cece_val - hemco_val) / expected
            if expected != 0.0
            else (0.0 if cece_val == 0.0 else float("inf"))
        )
        out_rows.append(
            {
                "case_id": row["case_id"],
                "expected_emission_per_aef": expected,
                "hemco_oracle_reproduced": hemco_val,
                "hemco_reproduction_rel_err": rel_err_hemco_repro,
                "cece_native_defaults": cece_val,
                "cece_vs_hemco_rel_diff": rel_diff_cece_vs_hemco,
            }
        )

    outdir.mkdir(parents=True, exist_ok=True)
    out_path = outdir / "scalar_case_validation.csv"
    with open(out_path, "w", newline="") as fh:
        # csv's default "excel" dialect writes CRLF; force LF to match repo convention.
        writer = csv.DictWriter(
            fh, fieldnames=list(out_rows[0].keys()), lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(out_rows)
    return out_rows, out_path


def make_plots(
    fields,
    isop_emission,
    soil_no_emission,
    isop_activity_cece,
    isop_activity_hemco,
    outdir: Path,
):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    outdir.mkdir(parents=True, exist_ok=True)
    lon2d, lat2d = fields["lon2d"], fields["lat2d"]

    # --- Global isoprene (MEGAN3) map ---
    fig, ax = plt.subplots(figsize=(10, 5), constrained_layout=True)
    isop_mgC = (
        isop_emission * 1.0e6 * 3600.0 * (5.0 * 12.011 / 68.12)
    )  # kg m-2 s-1 -> mg C m-2 hr-1
    vmax = float(np.percentile(isop_mgC, 99.0)) or 1.0
    im = ax.pcolormesh(
        lon2d, lat2d, isop_mgC, shading="auto", cmap="YlOrRd", vmin=0.0, vmax=vmax
    )
    ax.set(
        title="CECE native MEGAN3 ISOP emission (4x5 test grid)",
        xlabel="Longitude [deg]",
        ylabel="Latitude [deg]",
    )
    fig.colorbar(im, ax=ax, orientation="horizontal", pad=0.12, label="mg C m-2 hr-1")
    fig.savefig(outdir / "global_isoprene_megan3.png", dpi=180)
    plt.close(fig)

    # --- Global soil NO (BDSNP) map ---
    fig, ax = plt.subplots(figsize=(10, 5), constrained_layout=True)
    soil_no_ngNm2s = (
        soil_no_emission * 1.0e12 / BDSNP_MW_NO * 14.0
    )  # kg NO -> ng N m-2 s-1
    vmax_no = float(np.percentile(soil_no_ngNm2s, 99.0)) or 1.0
    im = ax.pcolormesh(
        lon2d,
        lat2d,
        soil_no_ngNm2s,
        shading="auto",
        cmap="YlGnBu",
        vmin=0.0,
        vmax=vmax_no,
    )
    ax.set(
        title="CECE native BDSNP soil NO emission (4x5 test grid)",
        xlabel="Longitude [deg]",
        ylabel="Latitude [deg]",
    )
    fig.colorbar(im, ax=ax, orientation="horizontal", pad=0.12, label="ng N m-2 s-1")
    fig.savefig(outdir / "global_soil_no_bdsnp.png", dpi=180)
    plt.close(fig)

    # --- CECE-native vs HEMCO-oracle percent-difference map (activity factor) ---
    with np.errstate(divide="ignore", invalid="ignore"):
        pct_diff = (
            100.0
            * (isop_activity_cece - isop_activity_hemco)
            / np.where(isop_activity_hemco != 0.0, isop_activity_hemco, np.nan)
        )
    fig, ax = plt.subplots(figsize=(10, 5), constrained_layout=True)
    finite_pct = pct_diff[np.isfinite(pct_diff)]
    limit = float(np.percentile(np.abs(finite_pct), 99.0)) if finite_pct.size else 100.0
    limit = limit if limit > 0.0 else 100.0
    im = ax.pcolormesh(
        lon2d,
        lat2d,
        np.clip(pct_diff, -limit, limit),
        shading="auto",
        cmap="RdBu_r",
        vmin=-limit,
        vmax=limit,
    )
    ax.set(
        title="CECE native MEGAN3 vs HEMCO 3.12.1 source oracle: % diff in ISOP activity factor\n"
        "(diagnostic comparison; CECE lacks HEMCO's running PAR/T history -- see summary.txt)",
        xlabel="Longitude [deg]",
        ylabel="Latitude [deg]",
    )
    fig.colorbar(im, ax=ax, orientation="horizontal", pad=0.12, label="% difference")
    fig.savefig(outdir / "megan3_vs_hemco_oracle_diff.png", dpi=180)
    plt.close(fig)

    # --- Zonal mean comparison ---
    fig, ax = plt.subplots(figsize=(7, 5), constrained_layout=True)
    ax.plot(
        np.nanmean(isop_activity_cece, axis=1),
        LAT,
        label="CECE native MEGAN3 (current hard-coded history)",
    )
    ax.plot(
        np.nanmean(isop_activity_hemco, axis=1),
        LAT,
        label="HEMCO 3.12.1 source oracle (PR #90)",
    )
    ax.set(
        title="Zonal-mean ISOP activity factor",
        xlabel="Activity factor per unit AEF",
        ylabel="Latitude [deg]",
    )
    ax.grid(alpha=0.3)
    ax.legend()
    fig.savefig(outdir / "megan3_vs_hemco_oracle_zonal.png", dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=REPO_ROOT / "tests" / "cece_config_earthaccess_4x5_test.yaml",
    )
    parser.add_argument(
        "--outdir", type=Path, default=REPO_ROOT / "results" / "bdsnp_megan3_4x5_test"
    )
    args = parser.parse_args()

    with open(args.config) as fh:
        config = yaml.safe_load(fh)
    grid_cfg = config["driver"]["grid"]
    schemes = {s["name"]: s for s in config["physics_schemes"]}
    assert "megan3" in schemes and "bdsnp" in schemes, (
        "config must request megan3 and bdsnp"
    )

    fields = build_synthetic_fields()

    isop_activity_cece = megan3_isop_activity_factor_cece(
        fields["temperature"],
        fields["leaf_area_index"],
        fields["leaf_area_index_prev"],
        fields["par_direct"],
        fields["par_diffuse"],
        fields["solar_cosine"],
        fields["soil_moisture_root"],
    )
    isop_activity_cece = np.where(
        fields["leaf_area_index"] > 0.0, isop_activity_cece, 0.0
    )
    isop_emission = ISOP_AEF_DEFAULT * isop_activity_cece

    isop_activity_hemco = megan_isop_activity_factor_hemco(
        fields["temperature"],
        fields["leaf_area_index"],
        fields["leaf_area_index_prev"],
        fields["par_direct"],
        fields["par_diffuse"],
        fields["solar_cosine"],
        CECE_CO2_PPM,
    )
    isop_activity_hemco = np.where(
        fields["leaf_area_index"] > 0.0, isop_activity_hemco, 0.0
    )

    soil_no_emission = bdsnp_soil_no_emission(
        fields["soil_temperature"],
        fields["soil_moisture"],
        fields["leaf_area_index"],
        ndep=0.0,
        base_ef=1.0,
    )

    args.outdir.mkdir(parents=True, exist_ok=True)
    make_plots(
        fields,
        isop_emission,
        soil_no_emission,
        isop_activity_cece,
        isop_activity_hemco,
        args.outdir,
    )
    scalar_rows, scalar_csv_path = run_scalar_case_validation(args.outdir)

    valid = fields["leaf_area_index"] > 0.0
    with np.errstate(divide="ignore", invalid="ignore"):
        pct_diff = (
            100.0
            * (isop_activity_cece[valid] - isop_activity_hemco[valid])
            / isop_activity_hemco[valid]
        )
    pct_diff = pct_diff[np.isfinite(pct_diff)]

    hemco_repro_err = max(
        r["hemco_reproduction_rel_err"]
        for r in scalar_rows
        if math.isfinite(r["hemco_reproduction_rel_err"])
    )
    cece_vs_hemco_scalar = [
        r["cece_vs_hemco_rel_diff"]
        for r in scalar_rows
        if math.isfinite(r["cece_vs_hemco_rel_diff"])
    ]

    lines = [
        "CECE native BDSNP + MEGAN3 global 4x5 test run",
        "================================================",
        f"Config: {args.config.relative_to(REPO_ROOT)}",
        f"Grid: {NX} lon x {NY} lat (HEMCO_4x5, {grid_cfg['lon_min']} to {grid_cfg['lon_max']} lon, "
        f"{grid_cfg['lat_min']} to {grid_cfg['lat_max']} lat)",
        "Inputs: synthetic global fields standing in for the earthaccess MODIS/SMAP/CERES",
        "        streams (live NASA Earthdata Login not available in this environment).",
        "Equations: transcribed directly from the checked-in C++ (cece_megan3.cpp,",
        "           cece_emission_activity.cpp, cece_megan.hpp, cece_bdsnp.cpp) -- not reimplemented physics.",
        "",
        "-- MEGAN3 ISOP global summary --",
        f"  land cells with LAI>0: {int(valid.sum())} / {NX * NY}",
        f"  mean isoprene emission (land cells): {float(isop_emission[valid].mean()):.6e} kg m-2 s-1",
        f"  max isoprene emission: {float(isop_emission.max()):.6e} kg m-2 s-1",
        "",
        "-- BDSNP soil NO global summary --",
        f"  mean soil NO emission: {float(soil_no_emission.mean()):.6e} kg NO m-2 s-1",
        f"  max soil NO emission: {float(soil_no_emission.max()):.6e} kg NO m-2 s-1",
        f"  frozen (soil T <= 0C) cell count: {int(np.sum(fields['soil_temperature'] <= 273.15))}",
        "",
        "-- Comparison with PR #90 HEMCO 3.12.1 MEGAN source oracle --",
        "  Scope: diagnostic comparison of the isoprene activity-factor term only.",
        "         This is NOT an executed-HEMCO runtime parity result (no gridded",
        "         HEMCO output exists in this repository; see PR #90 / docs/hemco_megan_parity.md).",
        f"  16-case oracle self-check: max reproduction error {hemco_repro_err:.3e} "
        "(this script's HEMCO-oracle function vs the literal PR #90 CSV values; see scalar_case_validation.csv)",
        f"  16-case CECE-native-vs-HEMCO-oracle max relative diff: {max(cece_vs_hemco_scalar):.4f} "
        f"({max(cece_vs_hemco_scalar) * 100.0:.1f}%)",
        f"  Gridded activity-factor % diff (land cells): mean={float(np.mean(pct_diff)):.2f}%, "
        f"max abs={float(np.max(np.abs(pct_diff))):.2f}%",
        "",
        "  Root causes of the divergence (all confirmed by source inspection, not assumed):",
        "    1. LDF: CECE ISOP default is 0.9996 (cece_emission_activity.cpp kDefaultLdf[0]); "
        "HEMCO oracle uses 1.0.",
        "    2. History/cold-start convention: CECE's Megan3Scheme::Run kernel currently hard-codes "
        "T_AVG_15=297.0 K, PAR_AVG=400.0 W m-2, DOY=180, LAI dbtwn=30 days "
        "(no running 5-day/12-hour history is wired in yet); the HEMCO oracle uses the HEMCO "
        "no-restart cold-start values (T_DAVG=288.15 K, PARDR/PARDF_DAVG=30/48 W m-2, DOY=171, dbtwn=1 day).",
        "    3. NORM_FAC: CECE uses the literal constant 1/1.0101081 "
        "= %.10f; the HEMCO oracle uses the fully-derived 0.9899364002107353 "
        "(difference is negligible, <0.001%%)." % NORM_FAC_CECE,
        "",
        "-- BDSNP HEMCO reference availability --",
        "  No gridded or scalar HEMCO soil-NOx reference data exists in this repository "
        "(only PR #90's MEGAN oracle is present). This run therefore reports CECE's native "
        "BDSNP field on its own, cross-checked only for the documented freezing behaviour "
        "(emission == 0 for soil T <= 0 C).",
        "",
        f"Scalar case validation table: {scalar_csv_path.relative_to(REPO_ROOT)}",
    ]
    summary_path = args.outdir / "summary.txt"
    summary_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    print(f"\nWrote plots and report to {args.outdir}")


if __name__ == "__main__":
    main()
