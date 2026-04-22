# Volcano

## Overview

Computes volcanic sulfur dioxide (SO₂) emissions as a point source with vertical distribution based on eruption plume height. The scheme injects SO₂ into the atmospheric column at a specified grid location, distributing emissions vertically between the volcanic vent elevation and cloud top height, concentrated in the upper third of the plume (eruptive profile).

## Registration Names

- Native C++: `"volcano"`
- Fortran bridge: `"volcano_fortran"`

## Configuration Parameters

| YAML Key | Type | Default | Description |
| --- | --- | --- | --- |
| `target_i` | int | 1 | Grid i-index for volcanic source location |
| `target_j` | int | 1 | Grid j-index for volcanic source location |
| `sulfur_emission` | double | 1.0 | SO₂ emission rate [kg/s] |
| `elevation` | double | 600.0 | Volcanic vent elevation [m] |
| `cloud_top` | double | 2000.0 | Cloud top / plume top height [m] |

## Import Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `surface_altitude` | m | Surface elevation |
| `layer_thickness` | m | Thickness of each vertical layer |

## Export Fields

| Field Name | Units | Description |
| --- | --- | --- |
| `volcanic_so2` | kg/s | Volcanic SO₂ emission rate (3D) |

## Algorithm

1. Determine plume vertical extent: bottom = `max(elevation, surface_altitude)`, top = `max(cloud_top, surface_altitude)`.
2. For eruptive profile, concentrate emissions in the upper 1/3 of the plume: `z_bot_volc = z_top − (z_top − z_bot) / 3`.
3. For each vertical level, compute the fractional overlap between the model layer and the plume injection zone.
4. Distribute `sulfur_emission * fraction` into each overlapping level.
5. If plume height is zero or negative, all emissions go into the surface level.

## YAML Configuration Example

```yaml
physics:
  - name: volcano
    config:
      target_i: 50
      target_j: 30
      sulfur_emission: 1000.0
      elevation: 1500.0
      cloud_top: 8000.0
```

## Implementation Notes

- Available as both native C++ (Kokkos) and Fortran bridge implementations
- The scheme is a point source — it only modifies a single (i, j) column
- The Kokkos kernel iterates over vertical levels only (not the full 3D grid)
- In a real coupled model, `target_i`/`target_j` would need to be mapped to local process bounds
