# Soil NOx

## Overview

Computes soil nitrogen oxide (NOx) emissions from microbial nitrification and denitrification processes. The parameterization uses exponential temperature dependence and a Poisson-like soil moisture response function based on water-filled pore space (WFPS).

Based on the Yienger and Levy (1995) empirical model with Hudman et al. (2012) updates.

References:
- Yienger, J.J. and H. Levy II (1995), Empirical model of global soil-biogenic NOx emissions, *JGR*, 100(D6), 11447–11464.

## Registration Names

- Native C++: `"soil_nox"`
- Fortran bridge: `"soil_nox_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `biome_coefficient_wet` | double | 0.5 | Biome-dependent emission coefficient for wet conditions |
| `temp_limit` | double | 30.0 | Maximum temperature for emission calculation [°C] |
| `temp_exp_coeff` | double | 0.103 | Exponential temperature coefficient [1/°C] |
| `wet_coeff_1` | double | 5.5 | Moisture response coefficient 1 |
| `wet_coeff_2` | double | -5.55 | Moisture response coefficient 2 [1/WFPS²] |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `temperature` | K | Surface air temperature |
| `soil_moisture` | fraction | Water-filled pore space fraction [0–1] |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `soil_nox_emissions` | kg NO/m²/s | Soil NOx emission flux |

## Algorithm

1. Convert temperature to Celsius: `tc = T − 273.15`.
2. Compute temperature factor: `t_term = exp(coeff * min(tc_max, tc))` (zero below freezing).
3. Compute moisture factor: `w_term = c1 * gw * exp(c2 * gw²)` (Poisson-like, peaks near WFPS ≈ 0.3).
4. Apply unit conversion: `UNITCONV = 1e-12 / 14 * 30` (ng N → kg NO).
5. Compute emission: `emiss = a_biome * UNITCONV * t_term * w_term * pulse`.

## YAML Configuration Example

```yaml
physics:
  - name: soil_nox
    config:
      biome_coefficient_wet: 0.5
      temp_limit: 30.0
      temp_exp_coeff: 0.103
      wet_coeff_1: 5.5
      wet_coeff_2: -5.55
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- The pulse factor (rain-induced emission burst) is currently a placeholder set to 1.0; HEMCO uses complex stateful pulsing logic
- The scheme operates on 2D (surface) fields only
