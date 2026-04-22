# MEGAN

## Overview

Implements the Model of Emissions of Gases and Aerosols from Nature (MEGAN) for biogenic isoprene emissions. The scheme computes emission rates using activity factors for leaf area index (LAI), temperature (light-dependent and light-independent pathways), photosynthetically active radiation (PAR via PCEEA), and CO₂ inhibition.

Ported from HEMCO's `hcox_megan_mod.F90` with Kokkos optimizations.

## Registration Names

- Native C++: `"megan"`
- Fortran bridge: `"megan_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `beta` | double | 0.13 | Temperature response coefficient (light-independent) |
| `ct1` | double | 95.0 | MEGAN temperature coefficient 1 |
| `ceo` | double | 2.0 | MEGAN emission coefficient |
| `ldf` | double | 1.0 | Light-dependent fraction [0–1] |
| `aef_isop` | double | 1.0e-9 | Isoprene activity emission factor |
| `lai_coeff_1` | double | 0.49 | LAI correction coefficient 1 |
| `lai_coeff_2` | double | 0.2 | LAI correction coefficient 2 |
| `standard_temp` | double | 303.0 | Standard reference temperature [K] |
| `gas_constant` | double | 8.3144598e-3 | Gas constant [kJ mol⁻¹ K⁻¹] |
| `ct2_const` | double | 200.0 | MEGAN temperature coefficient 2 |
| `t_opt_coeff_1` | double | 313.0 | Optimal temperature coefficient 1 [K] |
| `t_opt_coeff_2` | double | 0.6 | Optimal temperature coefficient 2 |
| `e_opt_coeff` | double | 0.08 | Optimal emission coefficient |
| `wm2_to_umolm2s` | double | 4.766 | W/m² to μmol/m²/s conversion factor |
| `ptoa_coeff_1` | double | 3000.0 | Top-of-atmosphere PAR coefficient 1 |
| `ptoa_coeff_2` | double | 99.0 | Top-of-atmosphere PAR coefficient 2 |
| `gamma_p_coeff_1` | double | 1.0 | PAR gamma coefficient 1 |
| `gamma_p_coeff_2` | double | 0.0005 | PAR gamma coefficient 2 |
| `gamma_p_coeff_3` | double | 2.46 | PAR gamma coefficient 3 |
| `gamma_p_coeff_4` | double | 0.9 | PAR gamma coefficient 4 |
| `co2_concentration` | double | 400.0 | Ambient CO₂ concentration [ppm] |
| `gamma_co2_coeff_1` | double | 8.9406 | CO₂ gamma coefficient 1 (Possell) |
| `gamma_co2_coeff_2` | double | 0.0024 | CO₂ gamma coefficient 2 (Possell) |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `temperature` | K | Surface air temperature |
| `leaf_area_index` | m²/m² | Leaf area index |
| `par_direct` | W/m² | Direct photosynthetically active radiation |
| `par_diffuse` | W/m² | Diffuse photosynthetically active radiation |
| `solar_cosine` | dimensionless | Cosine of solar zenith angle |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `isoprene_emissions` | kg/m²/s | Isoprene emission flux |

## Algorithm

1. Compute LAI correction: `γ_LAI = c1 * LAI / sqrt(1 + c2 * LAI²)`.
2. Compute light-independent temperature factor: `γ_T_LI = exp(β * (T − T_std))`.
3. Compute light-dependent temperature factor using optimal temperature and emission potential derived from 15-day average temperature.
4. Compute PAR factor via PCEEA algorithm using direct/diffuse PAR, solar zenith angle, and day of year.
5. Compute CO₂ inhibition factor (Possell relationship) once during initialization.
6. Combine: `emission = NORM_FAC * AEF * γ_LAI * γ_CO2 * ((1−LDF)*γ_T_LI + LDF*γ_PAR*γ_T_LD)`.

## YAML Configuration Example

```yaml
physics:
  - name: megan
    config:
      beta: 0.13
      ldf: 1.0
      aef_isop: 1.0e-9
      co2_concentration: 400.0
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- 15-day average temperature (`T_AVG_15`) and daily average PAR (`PAR_AVG`) are currently set to constant placeholders (297 K and 400 W/m², day-of-year 180) inside the kernel
- CO₂ gamma factor is computed once during `Initialize` and held constant
