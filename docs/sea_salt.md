# Sea Salt

## Overview

Computes size-resolved sea salt aerosol emissions from ocean surfaces using the Gong (2003) source function. The scheme integrates the normalized particle source function over accumulation mode (SALA) and coarse mode (SALC) size ranges, then scales by wind speed and sea surface temperature (SST).

References:
- Gong, S.L., et al. (2003), A parameterization of sea-salt aerosol source function for sub- and super-micron particles, *Global Biogeochem. Cycles*, 17(4).

## Registration Names

- Native C++: `"sea_salt"`
- Fortran bridge: `"sea_salt_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `integration_step` | double | 0.05 | Integration step size for Gong source function [μm] |
| `r80_dry_ratio` | double | 2.0 | Ratio of r80 (radius at 80% RH) to dry radius |
| `sea_salt_density` | double | 2200.0 | Sea salt particle density [kg/m³] |
| `r_sala_min` | double | 0.01 | Minimum dry radius for accumulation mode [μm] |
| `r_sala_max` | double | 0.5 | Maximum dry radius for accumulation mode [μm] |
| `r_salc_min` | double | 0.5 | Minimum dry radius for coarse mode [μm] |
| `r_salc_max` | double | 8.0 | Maximum dry radius for coarse mode [μm] |
| `sst_coeff` | sequence[4] | [0.329, 0.0904, -0.00717, 0.000207] | SST scaling polynomial coefficients |
| `u_power` | double | 3.41 | Wind speed power-law exponent |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `wind_speed` | m/s | 10-meter wind speed |
| `tskin` | K | Sea surface skin temperature |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `secondary_input` | kg/m²/s | Accumulation mode (SALA) sea salt emissions |
| `coarse_input` | kg/m²/s | Coarse mode (SALC) sea salt emissions |

## Algorithm

1. During initialization, integrate the Gong (2003) normalized source function `dF/dr80` over the SALA and SALC size ranges to compute reference mass emission rates (`srrc_SALA_`, `srrc_SALC_`).
2. At each grid cell, compute SST scaling using a cubic polynomial (Horner's method): `scale = c0 + SST*(c1 + SST*(c2 + SST*c3))`, with SST clamped to [0, 30] °C.
3. Compute wind factor as `u^u_pow` (default u^3.41).
4. Emission = `scale * u_factor * reference_rate` for each mode.

## YAML Configuration Example

```yaml
physics:
  - name: sea_salt
    config:
      r_sala_min: 0.01
      r_sala_max: 0.5
      r_salc_min: 0.5
      r_salc_max: 8.0
      u_power: 3.41
      sst_coeff: [0.329, 0.0904, -0.00717, 0.000207]
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- SALA and SALC are computed in separate Kokkos kernels and independently `MarkModified`
- The Gong source function integration is performed once during `Initialize`; only the wind/SST scaling runs per timestep
