# Ginoux (GOCART2G)

## Overview

Implements the Ginoux dust emission algorithm from the GOCART2G framework. This multi-bin scheme computes size-resolved dust emissions using the Marticorena (1997) dry-soil threshold friction velocity with Ginoux moisture modification. Unlike the legacy [`dust`](dust.md) scheme, this version resolves multiple particle size bins and uses component wind vectors.

References:
- Marticorena, B., et al. (1997), Modeling wind erosion, *Annales Geophysicae*, 15, 1381–1388.
- Ginoux, P., et al. (2001), Sources and distributions of dust aerosols, *JGR*, 106, 20255–20273.

## Registration Names

- Native C++: `"ginoux"`
- Fortran bridge: `"ginoux_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `ch_du` | double | 0.8e-9 | Dust emission tuning constant |
| `grav` | double | 9.81 | Gravitational acceleration [m/s²] |
| `num_bins` | int | 5 | Number of dust size bins |
| `frozen_soil_threshold` | double | 273.15 | Soil temperature [K] below which emissions are suppressed |
| `particle_radii` | sequence[double] | *(none)* | Particle radii per bin [m]; overrides `num_bins` if provided |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `u10m` | m/s | 10-meter zonal wind component |
| `v10m` | m/s | 10-meter meridional wind component |
| `surface_soil_wetness` | fraction | Surface soil wetness [0–1] |
| `land_mask` | dimensionless | Land mask (1 = land) |
| `lake_fraction` | fraction | Lake fraction [0–1] |
| `dust_source` | dimensionless | Dust source / erodibility map |
| `particle_radius` | m | Particle radius per bin (3D: 1 × 1 × nbins) |
| `soil_temperature` | K | Soil temperature (optional; used for frozen ground check) |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `ginoux_dust_emissions` | kg/m²/s | Size-resolved dust emission flux (3D: nx × ny × nbins) |

## Algorithm

For each grid cell on land:

1. Compute 10-m wind speed from components: `w10m = sqrt(u² + v²)`.
2. For each size bin:
   a. Compute particle diameter: `d = 2 * radius`.
   b. Compute Marticorena dry-soil threshold friction velocity:
      - Reynolds number: `Re = 1331 * (100*d)^1.56 + 0.38`
      - `u_thresh0 = 0.13 * sqrt(ρ_soil * g * d / ρ_air) * sqrt(1 + 6e-7/(ρ_soil * g * d^2.5)) / sqrt(1.928 * Re^0.092 − 1)`
   c. Apply Ginoux moisture modification:
      - If `gwettop ≥ 0.5`: no emission (threshold = 0)
      - Otherwise: `u_thresh = max(0, u_thresh0 * (1.2 + 0.2 * log10(max(1e-3, gwettop))))`
   d. If `w10m > u_thresh`:
      - `emission = ch_du * dust_source * (1 − fraclake) * w10m² * (w10m − u_thresh)`

## YAML Configuration Example

```yaml
physics:
  - name: ginoux
    config:
      ch_du: 0.8e-9
      grav: 9.81
      num_bins: 5
      particle_radii: [0.73e-6, 1.4e-6, 2.4e-6, 4.5e-6, 8.0e-6]
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- Produces multi-bin (3D) output; the third dimension is the size bin index
- Uses `Kokkos::pow`, `Kokkos::sqrt`, `Kokkos::log10`, `Kokkos::max`, and `Kokkos::round` for GPU portability
- Particle radii can be provided via config or read from the `particle_radius` import field
- The `particle_radius` import field is indexed as `(0, 0, n)` — it stores per-bin values in the third dimension
- Physical constants: soil density = 2650 kg/m³, air density = 1.25 kg/m³ (hard-coded in kernel)
