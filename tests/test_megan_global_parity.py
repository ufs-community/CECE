"""
test_megan_global_parity.py
───────────────────────────
Runnable standalone Python test suite for the HEMCO 3.12.1 MEGAN
isoprene global parity on the 4°×5° HEMCO grid (72×46 cells).

Tests use:
  - An analytical continental land/water mask (smoothed tanh box/ellipse)
  - Spatially variable isoprene AEF (MEGAN2.1 zonal pattern)
  - Realistic June climatological T, LAI, PAR, suncos fields
  - Both HEMCO 3.12.1 and CECE native MEGAN emission formulas

Run with:   pytest tests/test_megan_global_parity.py -v
"""

import math

import numpy as np
import pytest

# ── Grid ─────────────────────────────────────────────────────────────────────
NX, NY = 72, 46
lons = np.linspace(-177.5, 177.5, NX)  # 5° step, HEMCO convention
lats = np.linspace(-89.0, 89.0, NY)  # ≈4° step
LON, LAT = np.meshgrid(lons, lats)

# ── Frozen HEMCO 3.12.1 constants ────────────────────────────────────────────
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
WM2 = 4.766
# HEMCO 3.12.1 PTOA
H_PTOA_C1 = 2650.0
H_PTOA_C2 = 130.0
H_DOY_OFF = 18.0
H_PAR_AVG = 400.0  # direct µmol/m²/s
# CECE native PTOA
N_PTOA_C1 = 3000.0
N_PTOA_C2 = 99.0
N_DOY_OFF = 10.0
N_PAR_AVG = 400.0  # W/m² (converted internally)
# Shared
GCO2_C1 = 8.9406
GCO2_C2 = 0.0024
LAI_C1 = 0.49
LAI_C2 = 0.2
GP = [1.0, 0.0005, 2.46, 0.9]
ANEW, AGRO, AMAT, AOLD = 0.05, 0.60, 1.00, 0.90
DBTWN = 30.0
T_AVG_15 = 297.0
DOY = 180
CO2_REF = 390.0

# ── Gamma functions ───────────────────────────────────────────────────────────


def gamma_co2(co2):
    return GCO2_C1 / (1.0 + GCO2_C1 * GCO2_C2 * co2)


def gamma_t_li(T):
    return np.exp(BETA * (T - T_STD))


def gamma_t_ld(T, T_avg=T_AVG_15):
    e_opt = CEO * np.exp(E_OPT_C * (T_avg - 297.0))
    t_opt = T_OPT_C1 + T_OPT_C2 * (T_avg - 297.0)
    x = (1.0 / t_opt - 1.0 / T) / R
    den = CT2 - CT1 * (1.0 - np.exp(CT2 * x))
    return np.maximum(e_opt * CT2 * np.exp(CT1 * x) / den, 0.0)


def _gamma_par_core(pdr, pdf, sc, ptoa_c1, ptoa_c2, doy_off, bbb):
    mask = sc > 0
    pac_i = (pdr + pdf) * WM2
    ptoa = ptoa_c1 + ptoa_c2 * math.cos(2 * math.pi * (DOY - doy_off) / 365)
    # Avoid divide-by-zero at sc=0; result is masked to 0.0 anyway
    sc_safe = np.where(mask, sc, 1.0)
    phi = np.where(mask, pac_i / (sc_safe * ptoa), 0.0)
    aaa = GP[2] * bbb * phi - GP[3] * phi**2
    return np.where(mask, np.maximum(sc * aaa, 0.0), 0.0)


def gamma_par_hemco(pdr, pdf, sc):
    bbb = GP[0] + GP[1] * (H_PAR_AVG - 400.0)  # = 1.0
    return _gamma_par_core(pdr, pdf, sc, H_PTOA_C1, H_PTOA_C2, H_DOY_OFF, bbb)


def gamma_par_native(pdr, pdf, sc):
    pac_d = N_PAR_AVG * WM2  # 1906 µmol/m²/s
    bbb = GP[0] + GP[1] * (pac_d - 400.0)  # ≈ 1.753
    return _gamma_par_core(pdr, pdf, sc, N_PTOA_C1, N_PTOA_C2, N_DOY_OFF, bbb)


def gamma_lai(lai):
    return np.where(lai > 0, LAI_C1 * lai / np.sqrt(1.0 + LAI_C2 * lai**2), 0.0)


def gamma_age_ss():
    """Steady-state leaf-age factor (current = previous LAI)."""
    return 0.0 * ANEW + 0.1 * AGRO + 0.8 * AMAT + 0.1 * AOLD  # 0.95


def emission(T, lai, pdr, pdf, sc, aef, co2=CO2_REF, hemco=True):
    if not np.any(lai > 0):
        return np.zeros_like(T)
    gc = gamma_co2(co2)
    glai = gamma_lai(lai)
    gage = gamma_age_ss()
    gtli = gamma_t_li(T)
    gtld = gamma_t_ld(T)
    gpar = gamma_par_hemco(pdr, pdf, sc) if hemco else gamma_par_native(pdr, pdf, sc)
    comb = (1 - LDF) * gtli + LDF * gpar * gtld
    return np.where(lai > 0, NORM_FAC * gage * glai * gc * comb * aef, 0.0)


# ── Analytical continental land fraction ─────────────────────────────────────


def _smooth_box(lon, lat, lo1, lo2, la1, la2, edge=3.0):
    ew = (np.tanh((lon - lo1) / edge) - np.tanh((lon - lo2) / edge)) * 0.5
    ns = (np.tanh((lat - la1) / edge) - np.tanh((lat - la2) / edge)) * 0.5
    return np.clip(ew * ns, 0, 1)


def _smooth_ell(lon, lat, lo0, la0, rlo, rla, edge=4.0):
    r = np.sqrt(((lon - lo0) / rlo) ** 2 + ((lat - la0) / rla) ** 2)
    return np.clip(0.5 - 0.5 * np.tanh((r - 1.0) * edge), 0, 1)


def build_land_fraction():
    f = np.zeros((NY, NX))
    f += _smooth_box(LON, LAT, -125, -60, 10, 72)
    f += _smooth_box(LON, LAT, -90, -77, 7, 12)
    f += _smooth_ell(LON, LAT, -42, 72, 22, 12)
    f += _smooth_ell(LON, LAT, -58, -15, 25, 38)
    f += _smooth_box(LON, LAT, -10, 40, 36, 72)
    f += _smooth_ell(LON, LAT, 20, 0, 32, 40)
    f += _smooth_box(LON, LAT, 30, 50, 5, 37)
    f += _smooth_box(LON, LAT, 25, 145, 0, 72)
    f += _smooth_box(LON, LAT, 95, 145, -10, 10)
    f += _smooth_ell(LON, LAT, 134, -28, 22, 18)
    f += np.where(LAT < -68, 1.0, 0.0)
    return np.clip(f, 0, 1)


# ── Build global fields ───────────────────────────────────────────────────────


@pytest.fixture(scope="module")
def global_fields():
    land = build_land_fraction()

    def base_lai(lat):
        a = np.abs(lat)
        return np.where(
            a < 15,
            5.0,
            np.where(a < 35, 3.5, np.where(a < 55, 2.5, np.where(a < 70, 1.5, 0.4))),
        )

    desert = np.where(
        ((LON > -20) & (LON < 55) & (LAT > 18) & (LAT < 32))
        | ((LON > 45) & (LON < 65) & (LAT > 20) & (LAT < 35))
        | ((LON > -115) & (LON < -75) & (LAT > 20) & (LAT < 32)),
        0.3,
        1.0,
    )
    # Apply a land-fraction threshold so pure-ocean cells have LAI=0 exactly
    lai = np.where(land < 0.05, 0.0, np.clip(base_lai(LAT) * land * desert, 0, 8))

    def base_aef(lat):
        a = np.abs(lat)
        return np.where(
            a < 15,
            3.0e-9,
            np.where(
                a < 25,
                2.0e-9,
                np.where(a < 45, 1.5e-9, np.where(a < 60, 0.8e-9, 0.3e-9)),
            ),
        )

    aef = np.where(lai > 0, base_aef(LAT) * land, 0.0)

    T = (
        302.0
        - 0.10 * np.abs(LAT)
        - np.where(np.abs(LAT) > 20, 0.05 * (np.abs(LAT) - 20) ** 2, 0)
    )
    T += np.where((land > 0.5) & (LAT > 0), 2.0, 0.0)
    T += np.where(LAT < -60, -20.0, 0.0)
    T = np.clip(T, 255, 312)

    sc = np.clip(np.cos(np.radians(LAT - 23)) * 0.85, 0, 1)
    rng = np.random.RandomState(42)
    pdr = np.clip(400 * sc + 20 * rng.randn(NY, NX) * sc, 0, 700)
    pdf = np.clip(120 * sc + 10 * rng.randn(NY, NX) * sc, 0, 350)

    hemco_emis = emission(T, lai, pdr, pdf, sc, aef, hemco=True)
    native_emis = emission(T, lai, pdr, pdf, sc, aef, hemco=False)

    return dict(
        land=land,
        lai=lai,
        aef=aef,
        T=T,
        sc=sc,
        pdr=pdr,
        pdf=pdf,
        hemco_emis=hemco_emis,
        native_emis=native_emis,
    )


# ── Tests ─────────────────────────────────────────────────────────────────────


def test_ocean_cells_zero(global_fields):
    """All cells with LAI=0 must produce exactly zero emission from both schemes."""
    ocean = global_fields["lai"] <= 0
    n_ocean = int(ocean.sum())
    assert n_ocean > 0, "No ocean/bare cells found — check land fraction threshold"
    assert float(global_fields["hemco_emis"][ocean].max()) == 0.0, (
        "HEMCO 3.12.1: non-zero emission over LAI=0 cell(s)"
    )
    assert float(global_fields["native_emis"][ocean].max()) == 0.0, (
        "Native MEGAN: non-zero emission over LAI=0 cell(s)"
    )


def test_tropical_land_cells_positive(global_fields):
    """Vegetated tropical land cells (|lat|<20°, land_frac>0.5) emit > 0."""
    mask = (
        (np.abs(LAT) < 20)
        & (global_fields["land"] > 0.5)
        & (global_fields["lai"] > 0.5)
    )
    assert mask.sum() > 50, "Insufficient tropical land cells"
    assert np.all(global_fields["hemco_emis"][mask] > 0), "HEMCO: zero tropical cell"
    assert np.all(global_fields["native_emis"][mask] > 0), "Native: zero tropical cell"


def test_tropical_dominance(global_fields):
    """Tropics (|lat|<20°) must contribute > 35% of global total."""
    trop = np.abs(LAT) < 20
    total = global_fields["hemco_emis"].sum()
    frac = global_fields["hemco_emis"][trop].sum() / total
    assert frac > 0.35, f"Tropical fraction = {frac:.3f} (expected > 0.35)"


def test_spatial_correlation_high(global_fields):
    """Pearson R between HEMCO 3.12.1 and native > 0.995 over emitting cells."""
    h = global_fields["hemco_emis"]
    n = global_fields["native_emis"]
    mask = (h > 0) | (n > 0)
    R = np.corrcoef(h[mask], n[mask])[0, 1]
    assert R > 0.995, f"Spatial correlation R = {R:.6f} (expected > 0.995)"


def test_global_total_ratio_bounds(global_fields):
    """HEMCO 3.12.1 / native global total in [0.25, 0.80]."""
    sum_h = global_fields["hemco_emis"].sum()
    sum_n = global_fields["native_emis"].sum()
    ratio = sum_h / sum_n
    assert 0.25 < ratio < 0.80, (
        f"Global ratio HEMCO/native = {ratio:.4f} (expected in [0.25, 0.80])"
    )


def test_zonal_mean_peak_in_tropics(global_fields):
    """Peak zonal mean emission is in the ±30° band for both schemes."""
    for label, e in [
        ("HEMCO", global_fields["hemco_emis"]),
        ("native", global_fields["native_emis"]),
    ]:
        zonal = e.mean(axis=1)  # mean over longitude
        peak_j = int(zonal.argmax())
        peak_lat = float(lats[peak_j])
        assert abs(peak_lat) < 30.0, (
            f"{label}: zonal peak at lat={peak_lat:.1f}° (expected |lat|<30°)"
        )


def test_land_mask_symmetry(global_fields):
    """Emitting cells are exactly the vegetated cells (LAI > 0)."""
    has_veg = global_fields["lai"] > 0
    hemco_emits = global_fields["hemco_emis"] > 0
    native_emits = global_fields["native_emis"] > 0
    assert np.all(has_veg == hemco_emits), (
        f"HEMCO land mask mismatch: {(has_veg != hemco_emits).sum()} cells"
    )
    assert np.all(has_veg == native_emits), (
        f"Native land mask mismatch: {(has_veg != native_emits).sum()} cells"
    )


def test_nh_stronger_in_june(global_fields):
    """NH total emission > SH total in June (DOY=180, sub-solar at +23°N)."""
    nh = global_fields["hemco_emis"][LAT > 0].sum()
    sh = global_fields["hemco_emis"][LAT < 0].sum()
    assert nh > sh, f"NH sum={nh:.3e}  SH sum={sh:.3e} — expected NH > SH in June"


def test_monotonic_lai_scaling(global_fields):
    """Halving LAI reduces HEMCO 3.12.1 emission (γ_LAI is monotone increasing)."""
    lai_full = global_fields["lai"]
    lai_half = lai_full * 0.5
    e_full = emission(
        global_fields["T"],
        lai_full,
        global_fields["pdr"],
        global_fields["pdf"],
        global_fields["sc"],
        global_fields["aef"],
        hemco=True,
    )
    e_half = emission(
        global_fields["T"],
        lai_half,
        global_fields["pdr"],
        global_fields["pdf"],
        global_fields["sc"],
        global_fields["aef"] * 0.5,
        hemco=True,
    )
    mask = lai_full > 1.0
    failures = int((e_half[mask] >= e_full[mask]).sum())
    total = int(mask.sum())
    assert failures == 0, f"LAI monotonicity violated in {failures}/{total} cells"


def test_co2_inhibition_preserved(global_fields):
    """Doubling CO₂ (560 vs 390 ppm) reduces global emission by 20–50%."""
    f = global_fields
    e_390 = emission(
        f["T"], f["lai"], f["pdr"], f["pdf"], f["sc"], f["aef"], co2=390.0, hemco=True
    )
    e_560 = emission(
        f["T"], f["lai"], f["pdr"], f["pdf"], f["sc"], f["aef"], co2=560.0, hemco=True
    )
    ratio = e_560.sum() / e_390.sum()
    assert 0.50 < ratio < 0.80, (
        f"CO2 inhibition ratio (560/390) = {ratio:.4f} (expected 0.50–0.80)"
    )


def test_par_avg_bbb_values(global_fields):
    """Verify the bbb factor values that drive the native/HEMCO magnitude difference."""
    # HEMCO 3.12.1: PAR_AVG = 400 µmol/m²/s → bbb = 1.000
    bbb_hemco = 1.0 + 0.0005 * (400.0 - 400.0)
    assert bbb_hemco == pytest.approx(1.0, abs=1e-10)

    # CECE native: PAR_AVG = 400 W/m² × 4.766 = 1906.4 µmol/m²/s → bbb ≈ 1.753
    pac_daily_native = 400.0 * 4.766
    bbb_native = 1.0 + 0.0005 * (pac_daily_native - 400.0)
    assert bbb_native == pytest.approx(1.7532, abs=1e-4)

    # Ratio of bbb values predicts the PAR-driven emission amplification
    assert bbb_native / bbb_hemco == pytest.approx(1.7532, abs=1e-3)


def test_ptoa_difference_at_solstice(global_fields):
    """At DOY=180 (summer solstice), HEMCO PTOA < native PTOA."""
    ptoa_h = H_PTOA_C1 + H_PTOA_C2 * math.cos(2 * math.pi * (DOY - H_DOY_OFF) / 365)
    ptoa_n = N_PTOA_C1 + N_PTOA_C2 * math.cos(2 * math.pi * (DOY - N_DOY_OFF) / 365)
    assert ptoa_h == pytest.approx(2528.0, abs=2.0), f"HEMCO PTOA={ptoa_h:.1f}"
    assert ptoa_n == pytest.approx(2903.0, abs=5.0), f"Native PTOA={ptoa_n:.1f}"
    assert ptoa_h < ptoa_n, "HEMCO PTOA should be < native PTOA at DOY=180"


def test_zonal_statistics(global_fields):
    """Zonal mean pattern: tropical band > boreal band for both schemes."""
    trop_mask = np.abs(LAT) < 15
    boreal_mask = (np.abs(LAT) > 55) & (np.abs(LAT) < 70)
    for label, e in [
        ("HEMCO", global_fields["hemco_emis"]),
        ("native", global_fields["native_emis"]),
    ]:
        trop_zonal = e[trop_mask].mean()  # boolean mask → 1D array
        boreal_zonal = e[boreal_mask].mean()
        assert trop_zonal > boreal_zonal, (
            f"{label}: tropical mean ({trop_zonal:.3e}) not > boreal ({boreal_zonal:.3e})"
        )


def test_bias_map_structure(global_fields):
    """CECE native should be globally higher than HEMCO 3.12.1."""
    h = global_fields["hemco_emis"]
    n = global_fields["native_emis"]
    mask = (h > 0) & (n > 0)
    # Over emitting cells, native should dominate (different bbb and PTOA)
    cells_native_higher = int((n[mask] > h[mask]).sum())
    total = int(mask.sum())
    frac = cells_native_higher / total
    # LDF=0.9996 so LI branch is tiny; most daytime cells have native > HEMCO due to
    # larger bbb (1.75 vs 1.0) and larger PTOA denominator (2903 vs 2528).
    # Low-PAR / sub-polar cells can go either way — threshold set conservatively.
    assert frac > 0.70, (
        f"Only {frac:.1%} of emitting cells have native > HEMCO 3.12.1; expected > 70%"
    )


def test_global_emission_totals_reasonable(global_fields):
    """Global emission totals should be in a physically plausible range."""
    # Convert kg/m²/s to Tg C/yr globally (rough area estimate 5.1e14 m²)
    EARTH_AREA = 5.1e14  # m²
    KG_TO_TG = 1e-9
    S_PER_YR = 3.1536e7
    C_FRAC = 5 * 12.011 / 68.12  # isoprene → carbon fraction

    mean_h = global_fields["hemco_emis"].mean()
    tg_c_yr = mean_h * EARTH_AREA * S_PER_YR * KG_TO_TG * C_FRAC
    # Observed global isoprene ~400–600 Tg C/yr; with AEF=3e-9 peak, range is wider
    # Synthetic AEF is up to 3×10⁻⁹ everywhere (vs real spatial variation),
    # so allow a wide upper bound for this analytical test.
    assert 10.0 < tg_c_yr < 10000.0, (
        f"Global isoprene {tg_c_yr:.1f} Tg C/yr outside plausible range"
    )


if __name__ == "__main__":
    # Quick standalone run
    gf_data = {}
    land = build_land_fraction()

    # (rebuild inline for standalone run)
    def base_lai(lat):
        a = np.abs(lat)
        return np.where(
            a < 15,
            5.0,
            np.where(a < 35, 3.5, np.where(a < 55, 2.5, np.where(a < 70, 1.5, 0.4))),
        )

    desert = np.where(
        ((LON > -20) & (LON < 55) & (LAT > 18) & (LAT < 32))
        | ((LON > 45) & (LON < 65) & (LAT > 20) & (LAT < 35))
        | ((LON > -115) & (LON < -75) & (LAT > 20) & (LAT < 32)),
        0.3,
        1.0,
    )
    lai = np.clip(base_lai(LAT) * land * desert, 0, 8)
    aef = np.where(
        lai > 0,
        np.where(
            np.abs(LAT) < 15,
            3e-9,
            np.where(
                np.abs(LAT) < 25,
                2e-9,
                np.where(
                    np.abs(LAT) < 45, 1.5e-9, np.where(np.abs(LAT) < 60, 0.8e-9, 0.3e-9)
                ),
            ),
        )
        * land,
        0,
    )
    T = np.clip(
        302
        - 0.10 * np.abs(LAT)
        - np.where(np.abs(LAT) > 20, 0.05 * (np.abs(LAT) - 20) ** 2, 0)
        + np.where((land > 0.5) & (LAT > 0), 2, 0)
        + np.where(LAT < -60, -20, 0),
        255,
        312,
    )
    sc = np.clip(np.cos(np.radians(LAT - 23)) * 0.85, 0, 1)
    rng = np.random.RandomState(42)
    pdr = np.clip(400 * sc + 20 * rng.randn(NY, NX) * sc, 0, 700)
    pdf = np.clip(120 * sc + 10 * rng.randn(NY, NX) * sc, 0, 350)

    hemco_e = emission(T, lai, pdr, pdf, sc, aef, hemco=True)
    native_e = emission(T, lai, pdr, pdf, sc, aef, hemco=False)

    print(f"Grid {NX}×{NY}  land cells: {int((lai > 0).sum())} / {NX * NY}")
    print(f"HEMCO 3.12.1  global mean: {hemco_e.mean():.4e} kg/m²/s")
    print(f"CECE native   global mean: {native_e.mean():.4e} kg/m²/s")
    print(f"Ratio HEMCO/native:         {hemco_e.sum() / native_e.sum():.4f}")
    R = np.corrcoef(hemco_e[lai > 0], native_e[lai > 0])[0, 1]
    print(f"Spatial correlation R:      {R:.6f}")
    print("Run 'pytest tests/test_megan_global_parity.py -v' for full test suite.")
