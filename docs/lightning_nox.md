# Lightning NOx

## Overview

Computes lightning-produced nitrogen oxide (NOx) emissions based on convective cloud top height and empirical flash rate–yield relationships. Lightning is a significant natural source of NOx in the middle and upper troposphere, affecting ozone chemistry and atmospheric composition.

The flash rate is parameterized as a power law of cloud top height, with separate NOx yield factors for land and ocean regions.

References:
- Price, C., et al. (1997), Vertical distributions of lightning NOx for use in regional and global chemical transport models, *JGR*, 102(D5), 5943–5941.

## Registration Names

- Native C++: `"lightning"`
- Fortran bridge: `"lightning_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `yield_land` | double | 3.011e26 | NOx yield factor for land regions [molecules/flash] |
| `yield_ocean` | double | 1.566e26 | NOx yield factor for ocean regions [molecules/flash] |
| `flash_rate_coeff` | double | 3.44e-5 | Flash rate coefficient |
| `flash_rate_power` | double | 4.9 | Flash rate power-law exponent |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `cloud_top_height` | m | Convective cloud top height |
| `land_mask` | dimensionless | Land fraction (>0.5 = land); optional |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `lightning_nox_emissions` | kg/s/grid_cell | Lightning NOx production rate (3D, vertically distributed) |

## Algorithm

1. Convert cloud top height to km: `h_km = h / 1000`.
2. Compute flash rate: `flash_rate = coeff * h_km^power`.
3. Select yield based on land mask (land vs. ocean).
4. Compute total NOx yield: `yield = (flash_rate * yield_factor) * (MW_NO / 1000) / (Avogadro * 1e6)`.
5. Distribute uniformly across vertical levels: `level_yield = total_yield / nz`.

## YAML Configuration Example

```yaml
physics:
  - name: lightning
    config:
      yield_land: 3.011e26
      yield_ocean: 1.566e26
      flash_rate_coeff: 3.44e-5
      flash_rate_power: 4.9
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- The land mask is optional; if absent, all cells are treated as land
- Vertical distribution is currently uniform across all levels (Ott et al. proxy); a more sophisticated C-shape profile could be added
- The scheme produces 3D output (emissions distributed across vertical levels)
