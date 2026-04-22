# DMS

## Overview

Computes dimethyl sulfide (DMS) sea-air exchange fluxes based on seawater DMS concentrations, wind speed, and a gas transfer velocity parameterization. DMS is the dominant natural source of sulfur to the marine atmosphere and plays a critical role in marine aerosol formation and cloud processes.

The gas transfer velocity uses a Schmidt number formulation with wind-speed dependence following Nightingale et al. (2000).

References:
- Nightingale, P.D., et al. (2000), In situ evaluation of air-sea gas exchange parameterizations using novel conservative and volatile tracers, *Global Biogeochem. Cycles*, 14(1), 373–387.

## Registration Names

- Native C++: `"dms"`
- Fortran bridge: `"dms_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `schmidt_coeff` | sequence[4] | [2674.0, -147.12, 3.726, -0.038] | Schmidt number polynomial coefficients (c0–c3 vs. temperature in °C) |
| `kw_coeff` | sequence[2] | [0.222, 0.333] | Transfer velocity coefficients [c0 for u², c1 for u] |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `wind_speed` | m/s | 10-meter wind speed |
| `tskin` | K | Sea surface skin temperature |
| `seawater_conc` | mol/L or kg/m³ | Seawater DMS concentration |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `dms_emissions` | kg/m²/s | DMS sea-air flux |

## Algorithm

1. Convert skin temperature to Celsius: `tc = T − 273.15`. Skip cells where `tc < −10`.
2. Compute Schmidt number using Horner's method: `Sc = c0 + tc*(c1 + tc*(c2 + tc*c3))`.
3. Compute gas transfer velocity: `k_w = (c0*u² + c1*u) * (Sc/600)^(−0.5)` in cm/hr, then convert to m/s by dividing by 360000.
4. Compute flux: `emission = k_w * seawater_conc`.

## YAML Configuration Example

```yaml
physics:
  - name: dms
    config:
      schmidt_coeff: [2674.0, -147.12, 3.726, -0.038]
      kw_coeff: [0.222, 0.333]
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- Schmidt number coefficients are specific to DMS solubility; different gases require different coefficients
- The scheme operates on 2D (surface) fields only
