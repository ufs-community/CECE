# HEMCO 3.12.1 MEGAN Isoprene Stateless Source Conformance

CECE's `megan_method: hemco_3_12_1` evaluates a source-transcribed subset of
the HEMCO 3.12.1 MEGAN isoprene calculation. It is useful for deterministic
regression tests and controlled comparisons with CECE's native `megan` and
`megan3` schemes.

The checked-in tests do **not** execute HEMCO or contain HEMCO-produced
gridded output. Accordingly, they establish source conformance for the stated
stateless arithmetic, not end-to-end HEMCO-versus-CECE runtime parity. Native
MEGAN and MEGAN3 results are diagnostic comparisons, not parity references.

## Pinned source

- Repository: [geoschem/HEMCO](https://github.com/geoschem/HEMCO)
- Release: `3.12.1`
- Commit: `07da3c29fd85abc3824cb6288578b0b68c2395a3`
- File: `src/Extensions/hcox_megan_mod.F90`
- Source SHA-256: `a298e4003210c7dba86c53cdd37f85a868dcb3a89b3de56ab175257e04614f31`

The source transcription is in
`include/cece/physics/hemco_megan_stateless.hpp`. A separate Python
transcription generates the scalar regression vectors in
`tests/data/hemco_megan/`.

## Implemented source contract

| Quantity | HEMCO 3.12.1 treatment represented here |
|---|---|
| ISOP parameters | `LDF=1`, `CT1=95`, `CEO=2`, leaf-age weights `0.05/0.60/1.00/0.90` |
| Instantaneous PAR | separate direct and diffuse W m⁻² inputs, each converted by `4.766` |
| PTOA | `3000 + 99 cos(2π(DOY-10)/365)` µmol m⁻² s⁻¹ |
| PAR history | separate `PARDR_DAVG` and `PARDF_DAVG` values in W m⁻² |
| Temperature history | `T_DAVG` in K drives `gamma_T_LD` and leaf age |
| Previous LAI | exact effective previous-day `PMISOLAI` after HEMCO storage and optional PFT normalization |
| LAI interval | one day, as fixed by HEMCO 3.12.1 |
| CO₂ inhibition | explicit on/off setting; when enabled, Possell and Hewitt (2011) |
| Normalization | full `CALC_NORM_FAC` expression (`0.9899364002107353`) |

The HEMCO no-restart initialization values are `T_DAVG=288.15 K`,
`PARDR_DAVG=30 W m-2`, and `PARDF_DAVG=48 W m-2`. HEMCO stores those history
arrays and the raw `LAI_PREVDAY` field as `REAL(sp)`, then may normalize current
and previous LAI by the PFT sum. CECE projects the configured history values
through single precision, but consumes `leaf_area_index_prev` as the exact
post-preprocessing effective `PMISOLAI`; it does not round that value again.

The included 20 June 2021 analytical reference selects DOY 171, enables CO₂
inhibition, and uses 390 ppm. The date and CO₂ choices are fixture settings,
not universal HEMCO constants.

## Runtime inputs

The scheme consumes these CECE fields:

| CECE field | Meaning | Units |
|---|---|---|
| `temperature` | instantaneous temperature | K |
| `leaf_area_index` | effective current LAI | m² m⁻² |
| `leaf_area_index_prev` | required exact effective previous-day `PMISOLAI`, after storage and optional PFT normalization | m² m⁻² |
| `par_direct` | instantaneous direct PAR | W m⁻² |
| `par_diffuse` | instantaneous diffuse PAR | W m⁻² |
| `solar_cosine` | finite effective sine of solar elevation in [-1, 1], matching HEMCO's solar-angle result; nonpositive values represent night | 1 |

`aef` is currently one scalar configuration value in kg compound m⁻² s⁻¹,
not a gridded CECE input. It must be an effective AEF for the controlled case,
including upstream HEMCO PFT/AEF processing and `ISOP_SCALING` where relevant.
If HEMCO LAI normalization is desired, both current and previous LAI must be
supplied after that preprocessing.

The configured AEF and temperature/direct-PAR/diffuse-PAR history values are
spatially uniform scalars for the controlled source-conformance case. HEMCO's
corresponding AEF and evolving history state can vary by grid cell, so this
mode is not arbitrary global field-for-field conformance.

## Configuration

```yaml
physics_schemes:
  - name: megan
    language: cpp
    options:
      megan_method: hemco_3_12_1
      aef: 1.5e-9
      export_field_name: isoprene_hemco_source_reference
      hemco_co2_inhibition: true
      hemco_co2_ppm: 390.0
      hemco_par_direct_history_wm2: 30.0
      hemco_par_diffuse_history_wm2: 48.0
      hemco_temperature_history_k: 288.15
      hemco_day_of_year: 171
```

| Option | Default | Notes |
|---|---:|---|
| `megan_method` | `native` | Select `hemco_3_12_1` for this path |
| `aef` | required | Nonnegative scalar effective AEF, kg m⁻² s⁻¹ |
| `species_name` | `isoprene` | Isoprene only; HEMCO name `ISOP` is also accepted |
| `hemco_co2_inhibition` | required | HEMCO configuration switch |
| `hemco_co2_ppm` | required if enabled | Used only when inhibition is enabled; finite range 150-1250 ppm |
| `hemco_par_direct_history_wm2` | `30.0` | Cold-start `PARDR_DAVG`, W m⁻² |
| `hemco_par_diffuse_history_wm2` | `48.0` | Cold-start `PARDF_DAVG`, W m⁻² |
| `hemco_temperature_history_k` | `REAL(sp)(288.15)` | Cold-start `T_DAVG`, K |
| `hemco_day_of_year` | required | Controlled-case DOY; valid range 1-366 |

`co2_concentration` remains accepted as a compatibility alias for
`hemco_co2_ppm`.

All required runtime fields, including previous-day LAI, and the output must
share identical horizontal extents and contain exactly one level.
The source-conformance path rejects incompatible shapes before kernel launch.

The files under `examples/` are templates: replace their placeholder input
paths and provide a gridspec containing the exact target coordinates before a
real run. A uniform `lat_min/lat_max/ny` grid does not reproduce the GEOS 4x5
layout: longitude centers are `-180, -175, ..., 175`, while the polar
half-cell latitude centers are `-89, -86, -82, ..., 86, 89`. Use
`mapalgo: passthrough` only when source and target coordinates match exactly.

Run `cece_config_hemco_megan_parity.yaml` and
`cece_config_megan_hemco_comparison.yaml` separately when comparing the two
single-species methods. Both register as `megan`, and the current dispatcher
selects only the first configured instance with a requested name. The MEGAN3
comparison template can schedule `megan3` and `megan` together because those
registration names differ.

The two schemes use different AEF bases. The single-species `megan` AEF is a
mass flux in kg compound m⁻² s⁻¹, whereas a MEGAN3 class AEF is an amount flux
in kmol class m⁻² s⁻¹ and is multiplied by the mechanism-species molecular
weight during speciation. The MEGAN3 comparison template therefore uses
`1.4679976512037582e-11` kmol ISOP m⁻² s⁻¹, which maps to exactly `1.0e-9`
kg isoprene m⁻² s⁻¹ with the configured 68.12 kg kmol⁻¹ molecular weight.

## Tests

```console
cmake --build build --target test_hemco_megan_runtime test_hemco_megan_global
ctest --test-dir build -R 'HEMCO3121|HemcoMeganGlobal' --output-on-failure
python tests/test_megan_global_parity.py
```

The historical Python filename is retained for compatibility. Its tests are
synthetic global invariants, despite the word `parity` in the filename.

Regenerate the scalar vectors with:

```console
python scripts/generate_hemco_megan_oracle.py \
  > tests/data/hemco_megan/hemco_3_12_1_megan_reference.csv
```

## Deliberately deferred

- restart-state evolution: 5-day temperature/PAR histories and the 12-hour
  previous-day LAI/temperature updates (`T_PREVDAY` is currently unused by
  the represented source gamma calculation)
- restart equivalence and multi-timestep validation
- HEMCO solar angle calculated from latitude, local time, and model clock
- gridded PFT fractions, AEF generation, LAI normalization, and dynamic land inputs
- executed HEMCO-output comparison using identical effective inputs
- real-meteorology validation
- non-isoprene compounds and full MEGAN3 speciation

An end-to-end parity claim requires a pinned HEMCO run, matching effective
inputs and coordinate order, cellwise output comparison, and documented
precision projection. Until then, plots produced from the example templates
must be labeled as CECE source-reference comparisons.
