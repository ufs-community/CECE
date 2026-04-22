# Dust (Ginoux Legacy)

## Overview

Computes mineral dust emissions using the Ginoux et al. (2001) parameterization. This is the legacy GOCART dust scheme that calculates dust flux based on wind speed exceeding a moisture-adjusted threshold velocity, scaled by a surface erodibility source function.

Ported from HEMCO's `hcox_dustginoux_mod.F90`.

References:
- Ginoux, P., et al. (2001), Sources and distributions of dust aerosols simulated with the GOCART model, *JGR*, 106, 20255–20273.

## Registration Names

- Native C++: `"dust"`
- Fortran bridge: `"dust_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `g_constant` | double | 980.665 | Gravitational acceleration [cm/s²] |
| `air_density` | double | 1.25e-3 | Air density [g/cm³] |
| `particle_density` | double | 2500.0 | Particle density [kg/m³] (converted internally to g/cm³) |
| `particle_diameter` | double | 1.46e-6 | Particle diameter [m] (converted internally to cm) |
| `tuning_factor` | double | 9.375e-10 | Emission tuning constant (CH_DUST) |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `wind_speed` | m/s | 10-meter wind speed |
| `soil_moisture` | fraction | Surface soil wetness [0–1] |
| `erodibility` | dimensionless | Surface erodibility / dust source function |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `dust_emissions` | kg/m²/s | Dust emission flux |

## Algorithm

1. During initialization, compute dry-soil threshold velocity `u_ts0` using the Ginoux formulation based on particle density, diameter, gravity, and air density.
2. At each grid cell, adjust threshold for soil moisture: if `gw < 0.2`, `u_ts = u_ts0 * (1.2 + 0.2 * log10(max(1e-3, gw)))`; otherwise `u_ts = 100` (no emission).
3. If `u10 > u_ts`: `flux = tuning * erodibility * u10² * (u10 − u_ts)`.
4. Emission is clamped to non-negative values.

## YAML Configuration Example

```yaml
physics:
  - name: dust
    config:
      tuning_factor: 9.375e-10
      particle_density: 2500.0
      particle_diameter: 1.46e-6
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- This is the legacy single-bin Ginoux scheme; for multi-bin GOCART2G Ginoux, see the `ginoux` scheme
- Threshold velocity is computed once during `Initialize` and reused each timestep
- The scheme operates on 2D (surface) fields only
