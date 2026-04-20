# BDSNP Soil NO Emissions

## Overview

The BDSNP module computes soil nitrogen oxide (NO) emissions and writes them to the export state for consumption by MEGAN3 or other schemes. It replaces the previous `soil_nox` scheme with a more comprehensive parameterization.

Two algorithms are supported:

- **BDSNP** (default) — Berkeley-Dalhousie Soil NO Parameterization with biome-specific emission factors, piecewise-linear soil moisture dependence, nitrogen deposition fertilization, and canopy reduction
- **YL95** — Yienger and Levy (1995) empirical model with exponential temperature response and Poisson-like moisture function

Both modes set emissions to zero when soil temperature is below 0°C.

References:
- Hudman et al. (2012), Berkeley-Dalhousie Soil NO Parameterization
- Yienger, J.J. and H. Levy II (1995), *JGR*, 100(D6), 11447–11464

## Registration Names

- Native C++: `"bdsnp"`
- Fortran bridge: `"bdsnp_fortran"`

## Configuration

```yaml
physics_schemes:
  - name: bdsnp
    options:
      soil_no_method: bdsnp       # "bdsnp" (default) or "yl95"
      # BDSNP-specific parameters
      fert_emission_factor: 1.0
      wet_dep_scaling: 1.0
      dry_dep_scaling: 1.0
      pulse_decay_constant: 0.5
      # YL95 parameters (also used as fallback)
      biome_coefficient_wet: 0.5
      temp_limit: 30.0
      temp_exp_coeff: 0.103
      wet_coeff_1: 5.5
      wet_coeff_2: -5.55
    input_mapping:
      soil_temperature: TSOIL
      soil_moisture: GWETTOP
      nitrogen_deposition: NDEP
      land_use_type: LANDTYPE
      leaf_area_index: LAI
      biome_emission_factors: BIOME_EF
    output_mapping:
      soil_nox_emissions: SOIL_NO
```

### Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `soil_no_method` | string | `"bdsnp"` | Algorithm: `"bdsnp"` or `"yl95"` |
| `fert_emission_factor` | double | 1.0 | Fertilizer emission factor scaling (BDSNP) |
| `wet_dep_scaling` | double | 1.0 | Wet deposition scaling factor (BDSNP) |
| `dry_dep_scaling` | double | 1.0 | Dry deposition scaling factor (BDSNP) |
| `pulse_decay_constant` | double | 0.5 | Pulsing decay constant (BDSNP) |
| `biome_coefficient_wet` | double | 0.5 | Biome emission coefficient (YL95) |
| `temp_limit` | double | 30.0 | Max temperature for emission [°C] (YL95) |
| `temp_exp_coeff` | double | 0.103 | Exponential temperature coefficient (YL95) |
| `wet_coeff_1` | double | 5.5 | Moisture response coefficient 1 (YL95) |
| `wet_coeff_2` | double | -5.55 | Moisture response coefficient 2 (YL95) |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `soil_temperature` | K | Soil temperature |
| `soil_moisture` | fraction | Soil moisture [0–1] |
| `nitrogen_deposition` | kg N/m²/s | N deposition rate (BDSNP only) |
| `land_use_type` | — | Land use category (BDSNP only) |
| `leaf_area_index` | m²/m² | LAI for canopy reduction (BDSNP only) |
| `biome_emission_factors` | — | Biome-specific base emission (BDSNP only) |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `soil_nox_emissions` | kg NO/m²/s | Soil NO emission flux |

## Algorithms

### YL95 Mode

1. Convert temperature: `tc = T_soil − 273.15`
2. If `tc ≤ 0`: emission = 0 (freezing cutoff)
3. Temperature factor: `t_term = exp(0.103 × min(30, tc))`
4. Moisture factor: `w_term = 5.5 × gw × exp(−5.55 × gw²)`
5. Emission: `soil_NO = a_biome × UNITCONV × t_term × w_term`

### BDSNP Mode

1. Convert temperature: `tc = T_soil − 273.15`
2. If `tc ≤ 0`: emission = 0 (freezing cutoff)
3. Temperature response: `t_response = exp(0.103 × min(30, tc))`
4. Moisture factor (piecewise linear):
   - `SM ≤ 0`: 0
   - `SM ≤ 0.3`: `SM / 0.3`
   - `SM > 0.3`: `1.0 − 0.5 × (SM − 0.3) / 0.7`
5. N-deposition fertilization: `fert = 1 + fert_ef × ndep × (wet_dep + dry_dep)`
6. Canopy reduction: `canopy = exp(−0.24 × LAI)`
7. Emission: `soil_NO = base_ef × UNITCONV × t_response × sm_factor × fert × canopy`

## Integration with MEGAN3

BDSNP writes to the `soil_nox_emissions` export field. MEGAN3 reads this field for the NO emission class. The stacking engine must execute BDSNP before MEGAN3:

```yaml
physics_schemes:
  - name: bdsnp        # Runs first
  - name: megan3       # Runs second, reads soil_nox_emissions
```

If `soil_nox_emissions` is not present when MEGAN3 runs, the NO class contribution is set to zero with a warning.

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- The pulse factor in BDSNP mode is a stateless placeholder (no rain history tracking)
- Both modes produce numerically identical results between C++ and Fortran within 1e-6 tolerance
