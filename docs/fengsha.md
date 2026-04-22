# FENGSHA

## Overview

Implements the FENGSHA dust emission scheme, a physically-based saltation model that computes size-resolved dust emissions using friction velocity, soil texture, moisture corrections, and drag partitioning. The scheme includes Fécan moisture correction, Marticorena & Bergametti (1995) vertical-to-horizontal flux ratio, and distributes emissions across multiple size bins using a Kok distribution.

References:
- Fécan, F., et al. (1999), *Annales Geophysicae*, 17, 149–157.
- Marticorena, B. and Bergametti, G. (1995), *JGR*, 100(D8), 16415–16430.
- Webb, N.P., et al. (2020), *Current Opinion in Environmental Sustainability*, 44, 138–146.

## Registration Names

- Native C++: `"fengsha"`
- Fortran bridge: `"fengsha_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `alpha` | double | 1.0 | Sandblasting efficiency parameter |
| `gamma` | double | 1.0 | Erodibility exponent for SSM |
| `kvhmax` | double | 2.45e-4 | Maximum vertical-to-horizontal flux ratio |
| `grav` | double | 9.81 | Gravitational acceleration [m/s²] |
| `drylimit_factor` | double | 1.0 | Fécan dry-limit scaling factor |
| `frozen_soil_threshold` | double | 273.15 | Soil temperature [K] below which emissions are suppressed |
| `num_bins` | int | 5 | Number of dust size bins |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `friction_velocity` | m/s | Surface friction velocity (u*) |
| `threshold_velocity` | m/s | Threshold friction velocity |
| `soil_moisture` | fraction | Volumetric soil liquid water content |
| `clay_fraction` | fraction | Fractional clay content [0–1] |
| `sand_fraction` | fraction | Fractional sand content [0–1] |
| `silt_fraction` | fraction | Fractional silt content [0–1] |
| `erodibility` | dimensionless | Sediment supply map (SSM) |
| `drag_partition` | dimensionless | Drag partition correction factor |
| `air_density` | kg/m³ | Surface air density |
| `lake_fraction` | fraction | Lake fraction [0–1] |
| `snow_fraction` | fraction | Snow cover fraction [0–1] |
| `land_mask` | dimensionless | Land mask (1 = land) |
| `soil_temperature` | K | Soil temperature (optional; used for frozen ground check) |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `fengsha_dust_emissions` | kg/m²/s | Size-resolved dust emission flux (3D: nx × ny × nbins) |

## Algorithm

1. Skip non-land cells and cells with erodibility (SSM) below threshold (0.01).
2. Skip cells where soil temperature is below `frozen_soil_threshold` (default 273.15 K), if soil temperature is provided.
3. Compute land fraction accounting for lake and snow cover.
3. Compute vertical-to-horizontal flux ratio (MB95): `kvh = 10^(13.4*clay − 6)` for clay < 0.2, capped at `kvhmax`.
4. Compute total emission scaling: `(alpha/grav) * fracland * SSM^gamma * air_density * kvh`.
5. Adjust friction velocity by drag partition: `rustar = rdrag * u*`.
6. Convert volumetric to gravimetric soil moisture, then compute Fécan moisture correction factor `H` from clay content and dry limit.
7. Adjust threshold: `u_thresh = threshold_velocity * H`.
8. Compute horizontal saltation flux (Webb et al. 2020, Eq. 9): `q = max(0, rustar − u_thresh) * (rustar + u_thresh)²`.
9. Distribute across bins using hard-coded Kok distribution: [0.1, 0.25, 0.25, 0.25, 0.15].

## YAML Configuration Example

```yaml
physics:
  - name: fengsha
    config:
      alpha: 1.0
      gamma: 1.0
      kvhmax: 2.45e-4
      drylimit_factor: 1.0
      num_bins: 5
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- Produces multi-bin (3D) output; the third dimension is the size bin index
- The Kok bin distribution is hard-coded for up to 5 bins
- Requires 12 import fields — the most input-intensive scheme alongside K14
