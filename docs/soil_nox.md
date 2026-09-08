# Berkeley-Dalhousie Soil NOx Parameterization (BDSNP) Emissions

## Overview

The native C++ `bdsnp` scheme implements the Berkeley-Dalhousie Soil NOx
Parameterization (BDSNP), computes soil nitrogen oxide (NO) emissions, and
writes them to the export state for consumption by MEGAN3 or other schemes.
Two methods are supported through `soil_no_method`:

- **`bdsnp`** (default) — the validated, stateless effective-input BDSNP
  calculation. It applies 24-biome weighting, temperature and moisture
  responses, canopy reduction, pulse scaling, fertilizer, and deposited
  nitrogen.
- **`yl95`** — the Yienger and Levy (1995) empirical calculation retained as a
  lightweight fallback.

The scheme consumes prepared effective fields.

References:

- Hudman, R. C., et al. (2012), “Steps towards a mechanistic model of global
  soil nitric oxide emissions: implementation and space based-constraints,”
  *Atmospheric Chemistry and Physics*, 12, 7779–7795,
  [doi:10.5194/acp-12-7779-2012](https://doi.org/10.5194/acp-12-7779-2012).
- Yienger, J. J. and H. Levy II (1995), “Empirical model of global soil-biogenic
  NOx emissions,” *Journal of Geophysical Research: Atmospheres*, 100(D6),
  11447–11464,
  [doi:10.1029/95JD00370](https://doi.org/10.1029/95JD00370).

## Registration Names

- Native C++: `bdsnp` — authoritative implementation described here
- Fortran bridge: `bdsnp_fortran` — legacy simplified BDSNP/YL95 interface;
  it does not implement the effective-input contract and is not numerically
  equivalent to the canonical `bdsnp` method

## Configuration

### Canonical BDSNP

```yaml
physics_schemes:
  - name: bdsnp
    options:
      soil_no_method: bdsnp
      use_soil_temperature: false
```

`use_soil_temperature: false` selects `surface_temperature`; setting it to
`true` selects `soil_temperature`. All other BDSNP quantities are required
fields rather than scalar tuning options.

### YL95 fallback

```yaml
physics_schemes:
  - name: bdsnp
    options:
      soil_no_method: yl95
      biome_coefficient_wet: 0.5
      temp_limit: 30.0
      temp_exp_coeff: 0.103
      wet_coeff_1: 5.5
      wet_coeff_2: -5.55
```

### Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `soil_no_method` | string | `bdsnp` | `bdsnp` or `yl95`; unknown values are rejected |
| `use_soil_temperature` | boolean | `false` | For `bdsnp`, use `soil_temperature` instead of `surface_temperature` |
| `biome_coefficient_wet` | double | 0.5 | Wet-biome emission coefficient used by `yl95` |
| `temp_limit` | double | 30.0 | Maximum temperature [°C] used by `yl95` |
| `temp_exp_coeff` | double | 0.103 | Exponential temperature coefficient [1/°C] used by `yl95` |
| `wet_coeff_1` | double | 5.5 | First moisture-response coefficient used by `yl95` |
| `wet_coeff_2` | double | -5.55 | Second moisture-response coefficient used by `yl95` |

The removed `fert_emission_factor`, `wet_dep_scaling`, `dry_dep_scaling`, and
`pulse_decay_constant` options belonged to the superseded simplified
calculation and are not accepted as canonical BDSNP controls.

## Canonical BDSNP field contract

Every scalar field must have shape `nx × ny × 1`. The two layered fields must
have shape `nx × ny × 24`, with the same ordered biome layers in both fields.
Missing fields or incompatible shapes cause the scheme to fail.

### Import fields

| Field | Levels | Units/basis | Description |
| --- | ---: | --- | --- |
| `surface_temperature` | 1 | K | Required when `use_soil_temperature: false` |
| `soil_temperature` | 1 | K | Required when `use_soil_temperature: true` |
| `soil_moisture` | 1 | fraction | Effective grid-cell wetness used by the BDSNP response |
| `soilnox_land_fractions` | 24 | fraction | Ordered biome fractions; class 1 is the no-soil class |
| `soilnox_arid_fraction` | 1 | fraction | Effective arid fraction used to select the moisture response |
| `soilnox_nonarid_fraction` | 1 | fraction | Effective non-arid fraction used to select the moisture response |
| `leaf_area_index` | 1 | m² m⁻² | Leaf area index used by canopy reduction |
| `soilnox_canopy_nox` | 24 | effective source-contract units | Ordered biome canopy-NOx uptake terms |
| `wind_speed_squared` | 1 | m² s⁻² | Squared wind speed used by canopy ventilation |
| `solar_zenith_cosine` | 1 | 1 | Solar-zenith cosine used by the day/night ventilation branch |
| `soil_fertilizer` | 1 | effective source-contract units | Effective fertilizer-N amount, not a raw application flux |
| `deposited_nitrogen` | 1 | effective source-contract units | Effective available deposited N, not raw dry/wet deposition |
| `soilnox_pulse_factor` | 1 | 1 | Effective wetting-pulse multiplier |

`soilnox_canopy_nox`, `soil_fertilizer`, `deposited_nitrogen`, and
`soilnox_pulse_factor` are upstream effective inputs rather than raw
canopy, deposition, or precipitation fields.

### Export fields

| Field | Levels | Units | Description |
| --- | ---: | --- | --- |
| `soil_nox_emissions` | 1 | kg NO m⁻² s⁻¹ | Total Soil NO flux |
| `soil_nox_fertilizer_emissions` | 1 | kg NO m⁻² s⁻¹ | Fertilizer plus deposited-N contribution |

## Algorithms

### Canonical BDSNP mode

For each grid cell, the scheme:

1. Applies the exact no-soil class-1 gate.
2. Selects the configured surface- or soil-temperature response.
3. Selects the arid or non-arid wetness response.
4. Computes canopy reduction independently for each of the 24 biome layers.
5. Combines the biome background and effective fertilizer/deposited-N terms.
6. Applies the supplied pulse factor and biome fraction, then sums all 24
   contributions.
7. Writes the total and fertilizer-plus-deposited-N component separately.

### YL95 mode

1. Convert temperature: `tc = T_soil − 273.15`.
2. If `tc ≤ 0`, set emission to zero.
3. Compute `t_term = exp(temp_exp_coeff × min(temp_limit, tc))`.
4. Compute `w_term = wet_coeff_1 × gw × exp(wet_coeff_2 × gw²)`.
5. Compute `soil_NO = biome_coefficient_wet × UNITCONV × t_term × w_term`.

## Integration with MEGAN3

BDSNP must execute before MEGAN3 so `soil_nox_emissions` is available to the
MEGAN3 NO class:

```yaml
physics_schemes:
  - name: bdsnp
  - name: megan3
```

If `soil_nox_emissions` is absent when MEGAN3 runs, MEGAN3 logs a warning and
sets its NO-class contribution to zero. See
[`examples/cece_config_megan3.yaml`](../examples/cece_config_megan3.yaml) for a
compact coupling example that selects `yl95`; production `bdsnp` runs must
provide the complete effective-input contract above.
