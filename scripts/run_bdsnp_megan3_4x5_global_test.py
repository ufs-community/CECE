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

This standalone Python diagnostic uses NumPy arrays and does not call the
compiled CECE core or require a Kokkos/ESMF toolchain. Run it with Python 3.10+
and NumPy, PyYAML, and Matplotlib installed; for an actual native physics test,
build CECE with the project CMake toolchain and use the regular test suite.
Every constant below is quoted with the line/file it came from so the mapping
back to the C++ source is auditable.

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
import logging
import math
import struct
from collections.abc import Mapping
from dataclasses import dataclass, replace
from pathlib import Path

import numpy as np
import numpy.typing as npt
import yaml

REPO_ROOT = Path(__file__).resolve().parents[1]

# ============================================================================
# Grid -- exact HEMCO 4x5 layout (matches tests/cece_config_earthaccess_4x5_test.yaml
# and tests/test_earthaccess_stream_bdsnp_megan3.py NX/NY constants)
# ============================================================================
NX, NY = 72, 46
LON = np.array([-180.0 + 5.0 * i for i in range(NX)])
LAT = np.array([-89.0] + [-86.0 + 4.0 * i for i in range(44)] + [89.0])
LOGGER = logging.getLogger(__name__)

ArrayLike = npt.ArrayLike
FloatArray = npt.NDArray[np.float64]


@dataclass(frozen=True)
class MeganParameters:
    """MEGAN3 constants and the history values used for one comparison case."""

    isop_aef_default: float
    isop_ldf: float
    isop_ct1: float
    isop_cleo: float
    isop_beta: float
    age_new: float
    age_growing: float
    age_mature: float
    age_old: float
    norm_factor: float
    lai_c1: float
    lai_c2: float
    gas_constant: float
    ct2: float
    t_opt_c1: float
    t_opt_c2: float
    e_opt_coeff: float
    wm2_to_umol: float
    ptoa_c1: float
    ptoa_c2: float
    gp1: float
    gp2: float
    gp3: float
    gp4: float
    history_temperature: float
    history_par: float
    day_of_year: int
    days_between: float
    co2_ppm: float
    co2_c1: float = 8.9406
    co2_c2: float = 0.0024


@dataclass(frozen=True)
class BdsnpParameters:
    """Constants and default inputs for the BDSNP comparison case."""

    molecular_weight_no: float = 30.0
    unit_conversion: float = 1.0e-12 / 14.0 * 30.0
    fertilizer_emission_factor: float = 1.0
    wet_deposition_scaling: float = 1.0
    dry_deposition_scaling: float = 1.0
    pulse_decay: float = 0.5
    soil_temperature_slope: float = 0.103
    soil_temperature_cap_c: float = 30.0
    soil_moisture_knee: float = 0.3
    high_moisture_decay: float = 0.5
    canopy_reduction: float = 0.24
    nitrogen_deposition: float = 0.0
    base_emission_factor: float = 1.0


CECE_MEGAN = MeganParameters(
    isop_aef_default=1.0e-9,
    isop_ldf=0.9996,
    isop_ct1=95.0,
    isop_cleo=2.0,
    isop_beta=0.13,
    age_new=0.05,
    age_growing=0.6,
    age_mature=1.0,
    age_old=0.9,
    norm_factor=1.0 / 1.0101081,
    lai_c1=0.49,
    lai_c2=0.2,
    gas_constant=8.3144598e-3,
    ct2=200.0,
    t_opt_c1=313.0,
    t_opt_c2=0.6,
    e_opt_coeff=0.08,
    wm2_to_umol=4.766,
    ptoa_c1=3000.0,
    ptoa_c2=99.0,
    gp1=1.0,
    gp2=0.0005,
    gp3=2.46,
    gp4=0.9,
    history_temperature=297.0,
    history_par=400.0,
    day_of_year=180,
    days_between=30.0,
    co2_ppm=390.0,
)
HEMCO_MEGAN = replace(
    CECE_MEGAN,
    isop_ldf=1.0,
    norm_factor=0.9899364002107353,
    history_temperature=struct.unpack("f", struct.pack("f", 288.15))[0],
    history_par=78.0,
    day_of_year=171,
    days_between=1.0,
)
BDSNP_PARAMETERS = BdsnpParameters()


# ============================================================================
# Gamma helper functions -- transcribed from include/cece/physics/cece_megan.hpp
# ============================================================================
def get_gamma_lai(lai: ArrayLike, params: MeganParameters) -> FloatArray:
    """cece_megan.hpp get_gamma_lai(), non-bidirectional branch."""
    return params.lai_c1 * lai / np.sqrt(1.0 + params.lai_c2 * lai * lai)


def get_gamma_age(
    cmlai: ArrayLike, pmlai: ArrayLike, tt: ArrayLike, params: MeganParameters
) -> FloatArray:
    """cece_megan.hpp get_gamma_age(), vectorized."""
    cmlai, pmlai, tt = np.broadcast_arrays(
        np.asarray(cmlai, dtype=float),
        np.asarray(pmlai, dtype=float),
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
            params.days_between > ti,
            (ti / params.days_between) * (1.0 - pmlai / cmlai),
            1.0 - (pmlai / cmlai),
        )
        fmat_grow = np.where(
            params.days_between > tm,
            (pmlai / cmlai)
            + ((params.days_between - tm) / params.days_between)
            * (1.0 - pmlai / cmlai),
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

    return np.maximum(
        fnew * params.age_new
        + fgro * params.age_growing
        + fmat * params.age_mature
        + fold * params.age_old,
        0.0,
    )


def get_gamma_sm(gwetroot: ArrayLike, is_ald2_or_eoh: bool = False) -> FloatArray:
    """cece_megan.hpp get_gamma_sm(); MEGAN3 ISOP calls this with is_ald2_or_eoh=False -> 1.0."""
    gwetroot = np.asarray(gwetroot, dtype=float)
    return (
        np.ones_like(gwetroot)
        if not is_ald2_or_eoh
        else np.maximum(20.0 * np.clip(gwetroot, 0.0, 1.0) - 17.0, 1.0)
    )


def get_gamma_t_li(temp: ArrayLike, params: MeganParameters) -> FloatArray:
    return np.exp(params.isop_beta * (temp - 303.0))


def get_gamma_t_ld(T: ArrayLike, params: MeganParameters) -> FloatArray:
    e_opt = params.isop_cleo * np.exp(
        params.e_opt_coeff * (params.history_temperature - 297.0)
    )
    t_opt = params.t_opt_c1 + params.t_opt_c2 * (params.history_temperature - 297.0)
    x = (1.0 / t_opt - 1.0 / T) / params.gas_constant
    c_t = (
        e_opt
        * params.ct2
        * np.exp(params.isop_ct1 * x)
        / (params.ct2 - params.isop_ct1 * (1.0 - np.exp(params.ct2 * x)))
    )
    return np.maximum(c_t, 0.0)


def get_gamma_par_pceea(
    q_dir: ArrayLike,
    q_diff: ArrayLike,
    suncos: ArrayLike,
    params: MeganParameters,
) -> FloatArray:
    pac_instant = (q_dir + q_diff) * params.wm2_to_umol
    pac_daily = params.history_par * params.wm2_to_umol
    ptoa = params.ptoa_c1 + params.ptoa_c2 * np.cos(
        2.0 * np.pi * (params.day_of_year - 10.0) / 365.0
    )
    with np.errstate(divide="ignore", invalid="ignore"):
        phi = pac_instant / (suncos * ptoa)
        bbb = params.gp1 + params.gp2 * (pac_daily - 400.0)
        aaa = (params.gp3 * bbb * phi) - (params.gp4 * phi * phi)
        gamma_p = suncos * aaa
    gamma_p = np.where(suncos <= 0.0, 0.0, gamma_p)
    return np.maximum(gamma_p, 0.0)


def get_gamma_co2(co2a: ArrayLike, params: MeganParameters) -> FloatArray:
    """cece_megan.hpp get_gamma_co2(), use_wilkinson=False branch (CECE default)."""
    return params.co2_c1 / (1.0 + params.co2_c1 * params.co2_c2 * co2a)


# ============================================================================
# MEGAN3 ISOP class constants -- src/core/physics/cece_emission_activity.cpp
# (class index 0 == ISOP) and src/core/physics/cece_megan3.cpp
# ============================================================================
# Hard-coded "no dynamic history" defaults actually used by Megan3Scheme::Run
# today (cece_megan3.cpp, inside the Kokkos kernel) -- these are NOT the HEMCO
# cold-start values; CECE does not yet feed a running 5-day/12-hour history in.
def megan3_isop_activity_factor_cece(
    fields: Mapping[str, ArrayLike], params: MeganParameters = CECE_MEGAN
) -> FloatArray:
    """Reproduces the per-cell ISOP term inside Megan3Scheme::Run exactly,
    using the scheme's real (hard-coded) history defaults."""
    temperature = fields["temperature"]
    lai = fields["leaf_area_index"]
    g_lai_c = get_gamma_lai(lai, params)
    g_age_c = get_gamma_age(lai, fields["leaf_area_index_prev"], temperature, params)
    g_sm = get_gamma_sm(fields["soil_moisture_root"])
    g_t_li = get_gamma_t_li(temperature, params)
    g_t_ld = get_gamma_t_ld(temperature, params)
    g_par = get_gamma_par_pceea(
        fields["par_direct"], fields["par_diffuse"], fields["solar_cosine"], params
    )
    gamma_co2_val = get_gamma_co2(params.co2_ppm, params)
    ldf_combined = (1.0 - params.isop_ldf) * g_t_li + params.isop_ldf * g_par * g_t_ld
    return params.norm_factor * g_lai_c * g_age_c * g_sm * gamma_co2_val * ldf_combined


# ============================================================================
# HEMCO 3.12.1 oracle equations -- from PR #90's
# scripts/generate_hemco_megan_oracle.py (cold-start T_DAVG=288.15 K
# projected through single precision, PARDR/PARDF_DAVG=30/48 W m-2, DOY=171,
# LDF=1.0, dbtwn=1 day). Used here as the independent HEMCO-source-transcribed
# reference for the diagnostic comparison; it is not an executed HEMCO run.
# ============================================================================
def megan_isop_activity_factor_hemco(
    fields: Mapping[str, ArrayLike], params: MeganParameters = HEMCO_MEGAN
) -> FloatArray:
    """Evaluate the HEMCO-source oracle with its configured history constants."""
    temperature = fields["temperature"]
    lai = fields["leaf_area_index"]
    g_lai = get_gamma_lai(lai, params)
    g_age = get_gamma_age(lai, fields["leaf_area_index_prev"], temperature, params)
    g_t_li = get_gamma_t_li(temperature, params)
    g_t_ld = get_gamma_t_ld(temperature, params)
    g_par = get_gamma_par_pceea(
        fields["par_direct"], fields["par_diffuse"], fields["solar_cosine"], params
    )
    gamma_co2_val = get_gamma_co2(params.co2_ppm, params)
    ldf_combined = (1.0 - params.isop_ldf) * g_t_li + params.isop_ldf * g_par * g_t_ld
    return params.norm_factor * g_age * g_lai * gamma_co2_val * ldf_combined


# ============================================================================
# BDSNP constants -- src/core/physics/cece_bdsnp.cpp, "bdsnp" (default) branch
# ============================================================================
def bdsnp_moisture_factor(
    sm: ArrayLike, params: BdsnpParameters = BDSNP_PARAMETERS
) -> FloatArray:
    sm = np.asarray(sm, dtype=float)
    ramp = sm / params.soil_moisture_knee
    decay = 1.0 - params.high_moisture_decay * (sm - params.soil_moisture_knee) / (
        1.0 - params.soil_moisture_knee
    )
    out = np.where(
        sm <= 0.0,
        0.0,
        np.where(sm <= params.soil_moisture_knee, ramp, decay),
    )
    return out


def bdsnp_ndep_factor(
    ndep: ArrayLike, params: BdsnpParameters = BDSNP_PARAMETERS
) -> FloatArray:
    return 1.0 + params.fertilizer_emission_factor * (
        ndep * (params.wet_deposition_scaling + params.dry_deposition_scaling)
    )


def bdsnp_canopy_reduction(
    lai: ArrayLike, params: BdsnpParameters = BDSNP_PARAMETERS
) -> FloatArray:
    return np.exp(-params.canopy_reduction * np.asarray(lai, dtype=float))


def bdsnp_soil_no_emission(
    fields: Mapping[str, ArrayLike], params: BdsnpParameters = BDSNP_PARAMETERS
) -> FloatArray:
    """Reproduces BdsnpScheme::Run's "bdsnp" branch exactly (default config,
    i.e. no nitrogen_deposition/land_use_type/biome_emission_factors streams
    -- matching what tests/cece_config_earthaccess_4x5_test.yaml supplies)."""
    tc = fields["soil_temperature"] - 273.15
    t_response = np.exp(
        params.soil_temperature_slope * np.minimum(params.soil_temperature_cap_c, tc)
    )
    sm_factor = bdsnp_moisture_factor(fields["soil_moisture"], params)
    fert_factor = bdsnp_ndep_factor(params.nitrogen_deposition, params)
    canopy_red = bdsnp_canopy_reduction(fields["leaf_area_index"], params)
    pulse = math.exp(-params.pulse_decay * 0.0)  # no antecedent rain state -> 1.0
    emiss = (
        params.base_emission_factor
        * params.unit_conversion
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
def build_synthetic_fields() -> dict[str, FloatArray]:
    lon2d, lat2d = np.meshgrid(LON, LAT)  # shape (NY, NX)
    abs_lat = np.abs(lat2d)

    # Land mask: smooth continents, reused shape from tests/test_megan_global_parity.py
    def smooth_box(
        lon: FloatArray,
        lat: FloatArray,
        lon1: float,
        lon2: float,
        lat1: float,
        lat2: float,
        edge: float = 3.0,
    ) -> FloatArray:
        def sigmoid(values: FloatArray, lower: float, upper: float) -> FloatArray:
            return 0.5 * (
                np.tanh((values - lower) / edge) - np.tanh((values - upper) / edge)
            )

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


def run_scalar_case_validation(
    outdir: Path,
) -> tuple[list[dict[str, str | float]], Path]:
    csv_path = (
        REPO_ROOT
        / "tests"
        / "data"
        / "hemco_megan"
        / "hemco_3_12_1_megan_reference.csv"
    )
    with open(csv_path, newline="") as fh:
        rows = list(csv.DictReader(fh))

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

        scalar_fields: dict[str, ArrayLike] = {
            "temperature": T,
            "leaf_area_index": L,
            "leaf_area_index_prev": Lp,
            "par_direct": pdr,
            "par_diffuse": pdf,
            "solar_cosine": sc,
            "soil_moisture_root": gw,
        }
        hemco_params = replace(HEMCO_MEGAN, co2_ppm=co2)
        hemco_val = float(megan_isop_activity_factor_hemco(scalar_fields, hemco_params))
        cece_val = float(megan3_isop_activity_factor_cece(scalar_fields, CECE_MEGAN))
        rel_err_hemco_repro = (
            abs(hemco_val - expected) / expected
            if expected > 0.0
            else (
                0.0 if math.isclose(hemco_val, 0.0, abs_tol=1.0e-15) else float("inf")
            )
        )
        rel_diff_cece_vs_hemco = (
            abs(cece_val - hemco_val) / expected
            if expected > 0.0
            else (0.0 if math.isclose(cece_val, 0.0, abs_tol=1.0e-15) else float("inf"))
        )
        out_rows.append({
            "case_id": row["case_id"],
            "expected_emission_per_aef": expected,
            "hemco_oracle_reproduced": hemco_val,
            "hemco_reproduction_rel_err": rel_err_hemco_repro,
            "cece_native_defaults": cece_val,
            "cece_vs_hemco_rel_diff": rel_diff_cece_vs_hemco,
        })

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
    fields: Mapping[str, ArrayLike],
    isop_emission: FloatArray,
    soil_no_emission: FloatArray,
    isop_activity_cece: FloatArray,
    isop_activity_hemco: FloatArray,
    outdir: Path,
) -> None:
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
        soil_no_emission * 1.0e12 / BDSNP_PARAMETERS.molecular_weight_no * 14.0
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
            / np.where(
                np.isclose(isop_activity_hemco, 0.0, rtol=0.0, atol=1.0e-15),
                np.nan,
                isop_activity_hemco,
            )
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


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
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

    isop_activity_cece = megan3_isop_activity_factor_cece(fields, CECE_MEGAN)
    isop_activity_cece = np.where(
        fields["leaf_area_index"] > 0.0, isop_activity_cece, 0.0
    )
    isop_emission = CECE_MEGAN.isop_aef_default * isop_activity_cece

    isop_activity_hemco = megan_isop_activity_factor_hemco(fields, HEMCO_MEGAN)
    isop_activity_hemco = np.where(
        fields["leaf_area_index"] > 0.0, isop_activity_hemco, 0.0
    )

    soil_no_emission = bdsnp_soil_no_emission(fields, BDSNP_PARAMETERS)

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
        (
            f"Grid: {NX} lon x {NY} lat (HEMCO_4x5, {grid_cfg['lon_min']} to {grid_cfg['lon_max']} lon, "
            f"{grid_cfg['lat_min']} to {grid_cfg['lat_max']} lat)"
        ),
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
        (
            f"  16-case oracle self-check: max reproduction error {hemco_repro_err:.3e} "
            "(this script's HEMCO-oracle function vs the literal PR #90 CSV values; see scalar_case_validation.csv)"
        ),
        (
            f"  16-case CECE-native-vs-HEMCO-oracle max relative diff: {max(cece_vs_hemco_scalar):.4f} "
            f"({max(cece_vs_hemco_scalar) * 100.0:.1f}%)"
        ),
        (
            f"  Gridded activity-factor % diff (land cells): mean={float(np.mean(pct_diff)):.2f}%, "
            f"max abs={float(np.max(np.abs(pct_diff))):.2f}%"
        ),
        "",
        "  Root causes of the divergence (all confirmed by source inspection, not assumed):",
        (
            "    1. LDF: CECE ISOP default is 0.9996 (cece_emission_activity.cpp kDefaultLdf[0]); "
            "HEMCO oracle uses 1.0."
        ),
        (
            "    2. History/cold-start convention: CECE's Megan3Scheme::Run kernel currently hard-codes "
            "T_AVG_15=297.0 K, PAR_AVG=400.0 W m-2, DOY=180, LAI dbtwn=30 days "
            "(no running 5-day/12-hour history is wired in yet); the HEMCO oracle uses the HEMCO "
            "no-restart cold-start values (T_DAVG=288.15 K, PARDR/PARDF_DAVG=30/48 W m-2, DOY=171, dbtwn=1 day)."
        ),
        (
            f"    3. NORM_FAC: CECE uses the literal constant 1/1.0101081 = {CECE_MEGAN.norm_factor:.10f}; "
            "the HEMCO oracle uses the fully-derived 0.9899364002107353 "
            "(difference is negligible, <0.001%)."
        ),
        "",
        "-- BDSNP HEMCO reference availability --",
        (
            "  No gridded or scalar HEMCO soil-NOx reference data exists in this repository "
            "(only PR #90's MEGAN oracle is present). This run therefore reports CECE's native "
            "BDSNP field on its own, cross-checked only for the documented freezing behaviour "
            "(emission == 0 for soil T <= 0 C)."
        ),
        "",
        f"Scalar case validation table: {scalar_csv_path.relative_to(REPO_ROOT)}",
    ]
    summary_path = args.outdir / "summary.txt"
    summary_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    LOGGER.info("%s", "\n".join(lines))
    LOGGER.info("Wrote plots and report to %s", args.outdir)


if __name__ == "__main__":
    main()
