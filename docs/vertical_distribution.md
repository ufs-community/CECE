# Vertical Distribution in ACES

Vertical distribution is a critical capability in ACES that maps 2D emission fields to 3D atmospheric grids. This process is essential for accurately representing emission sources at appropriate atmospheric levels, from surface-based anthropogenic emissions to elevated aircraft and lightning sources.

## Overview

Most emission inventories provide data as 2D surface fluxes, but atmospheric chemistry models require 3D emission fields that specify emissions at each vertical level. ACES provides multiple algorithms to distribute these 2D emissions vertically based on physical principles and source characteristics.

## Distribution Methods

### `SINGLE` - Single Layer Placement

Places all emissions in a specific vertical layer index.

**Use Cases:**
- Aircraft emissions at specific flight levels
- Elevated point sources at known heights
- Satellite-observed emission layers

**Configuration:**
```yaml
species:
  aircraft_co:
    - field: "aircraft_emissions"
      vdist_method: "SINGLE"
      vdist_layer_start: 25    # Place all emissions in layer 25
```

**Algorithm:**
```
emissions_3d[i,j,k] = 0.0                    ∀ k ≠ layer_start
emissions_3d[i,j,layer_start] = emissions_2d[i,j]
```

### `RANGE` - Uniform Layer Range Distribution

Distributes emissions evenly across a range of layer indices.

**Use Cases:**
- Industrial stack emissions over typical plume heights
- Biomass burning emissions in the mixed layer
- Distributed vertical sources

**Configuration:**
```yaml
species:
  industrial_nox:
    - field: "stack_emissions"
      vdist_method: "RANGE"
      vdist_layer_start: 1     # Start at surface layer
      vdist_layer_end: 5       # End at layer 5
```

**Algorithm:**
```
num_layers = vdist_layer_end - vdist_layer_start + 1
fraction = 1.0 / num_layers

for k = vdist_layer_start to vdist_layer_end:
    emissions_3d[i,j,k] = emissions_2d[i,j] * fraction
```

### `PRESSURE` - Pressure-Based Distribution

Distributes emissions based on atmospheric pressure bounds.

**Use Cases:**
- Lightning NOx production (100-400 hPa)
- Free troposphere sources
- Stratospheric injection events

**Configuration:**
```yaml
species:
  lightning_nox:
    - field: "lightning_production"
      vdist_method: "PRESSURE"
      vdist_p_start: 10000.0   # 100 hPa (upper troposphere)
      vdist_p_end: 40000.0     # 400 hPa (mid troposphere)
```

**Algorithm:**
```
total_pressure_range = vdist_p_end - vdist_p_start
total_fraction = 0.0

for each layer k:
    p_top = pressure_interface[k+1]
    p_bot = pressure_interface[k]

    # Calculate overlap with target pressure range
    overlap_start = max(vdist_p_start, p_top)
    overlap_end = min(vdist_p_end, p_bot)

    if overlap_end > overlap_start:
        layer_fraction = (overlap_end - overlap_start) / total_pressure_range
        emissions_3d[i,j,k] = emissions_2d[i,j] * layer_fraction
        total_fraction += layer_fraction

# Normalize to ensure mass conservation
for each layer k:
    emissions_3d[i,j,k] *= (1.0 / total_fraction)
```

### `HEIGHT` - Altitude-Based Distribution

Distributes emissions based on geometric height above surface.

**Use Cases:**
- Aviation emissions at cruise altitudes
- Tall stack emissions with known injection heights
- Topographically-referenced sources

**Configuration:**
```yaml
species:
  aircraft_nox:
    - field: "cruise_emissions"
      vdist_method: "HEIGHT"
      vdist_h_start: 9000.0    # 9 km altitude
      vdist_h_end: 12000.0     # 12 km altitude
```

**Algorithm:**
```
total_height_range = vdist_h_end - vdist_h_start
total_fraction = 0.0

for each layer k:
    h_bot = height_interface[k]
    h_top = height_interface[k+1]

    # Calculate overlap with target height range
    overlap_start = max(vdist_h_start, h_bot)
    overlap_end = min(vdist_h_end, h_top)

    if overlap_end > overlap_start:
        layer_fraction = (overlap_end - overlap_start) / total_height_range
        emissions_3d[i,j,k] = emissions_2d[i,j] * layer_fraction
        total_fraction += layer_fraction

# Normalize for mass conservation
for each layer k:
    emissions_3d[i,j,k] *= (1.0 / total_fraction)
```

### `PBL` - Planetary Boundary Layer Distribution

Distributes emissions within the planetary boundary layer based on meteorological conditions.

**Use Cases:**
- Surface-based anthropogenic emissions
- Dust emissions from arid regions
- Biogenic emissions from vegetation

**Configuration:**
```yaml
species:
  surface_co:
    - field: "anthropogenic_co"
      vdist_method: "PBL"
      # No additional parameters needed - uses PBL height field
```

**Algorithm:**
```
for each grid cell (i,j):
    pbl_height_local = pbl_height[i,j]
    total_fraction = 0.0

    for each layer k:
        h_bot = height[i,j,k]
        h_top = height[i,j,k+1]

        if h_bot < pbl_height_local:
            # Layer is partially or fully within PBL
            overlap = min(h_top, pbl_height_local) - h_bot
            layer_thickness = h_top - h_bot

            if layer_thickness > 0:
                layer_fraction = overlap / layer_thickness
                emissions_3d[i,j,k] = emissions_2d[i,j] * layer_fraction
                total_fraction += layer_fraction

    # Normalize for mass conservation
    if total_fraction > 0:
        for each layer k:
            emissions_3d[i,j,k] *= (1.0 / total_fraction)
```

## Mass Conservation

All vertical distribution methods in ACES ensure strict mass conservation:

```
∑(k=1 to nz) emissions_3d[i,j,k] = emissions_2d[i,j]  ∀ i,j
```

This is achieved through normalization factors that account for:
- Partial layer overlaps with target ranges
- Varying layer thicknesses
- Numerical precision effects

## Coordinate System Support

ACES supports multiple vertical coordinate systems for distribution calculations:

### Terrain-Following Coordinates
```yaml
vertical_config:
  type: "FV3"                    # FV3/GFDL coordinate system
  ak_field: "hybrid_ak"          # Hybrid coordinate coefficients
  bk_field: "hybrid_bk"
  p_surf_field: "surface_pressure"
```

### Height Coordinates
```yaml
vertical_config:
  type: "WRF"                    # Height-based coordinates
  z_field: "height_levels"       # Geometric heights
  pbl_field: "pbl_height"
```

### MPAS Coordinates
```yaml
vertical_config:
  type: "MPAS"                   # MPAS mesh coordinates
  z_field: "zgrid"               # Height levels
  pbl_field: "pbl_height"
```

## Performance Considerations

### Memory Layout
Vertical distribution uses optimized Kokkos parallel algorithms:

```cpp
// Efficient parallel execution across grid points
Kokkos::parallel_for("vertical_distribution",
    Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0,0}, {nx,ny}),
    KOKKOS_LAMBDA(const int i, const int j) {
        // Distribute emissions for grid cell (i,j)
        distribute_vertical(i, j, emissions_2d, emissions_3d,
                          vdist_params, vertical_coords);
    });
```

### Computational Complexity
| Method | Complexity | Memory Access Pattern |
|--------|------------|----------------------|
| `SINGLE` | O(nx×ny) | Minimal - direct assignment |
| `RANGE` | O(nx×ny×Δk) | Linear in layer range |
| `PRESSURE` | O(nx×ny×nz) | Full vertical profile |
| `HEIGHT` | O(nx×ny×nz) | Full vertical profile |
| `PBL` | O(nx×ny×nz_pbl) | PBL-limited vertical access |

## Advanced Usage Examples

### Multi-Method Species
Different emission components can use different vertical distribution methods:

```yaml
species:
  nox:
    # Surface traffic emissions in PBL
    - field: "surface_traffic_nox"
      category: "transportation"
      hierarchy: 1
      vdist_method: "PBL"

    # Shipping emissions in marine boundary layer
    - field: "shipping_nox"
      category: "transportation"
      hierarchy: 2
      vdist_method: "HEIGHT"
      vdist_h_start: 0.0
      vdist_h_end: 100.0         # Stack height ~100m

    # Aircraft emissions at cruise altitude
    - field: "aircraft_nox"
      category: "transportation"
      hierarchy: 3
      vdist_method: "HEIGHT"
      vdist_h_start: 9000.0      # Cruise altitude 9-12 km
      vdist_h_end: 12000.0

    # Lightning production in free troposphere
    - field: "lightning_nox"
      category: "natural"
      hierarchy: 1
      vdist_method: "PRESSURE"
      vdist_p_start: 10000.0     # 100-400 hPa
      vdist_p_end: 40000.0
```

### Seasonal PBL Variations
Combine vertical distribution with temporal scaling:

```yaml
temporal_profiles:
  pbl_seasonal: [0.7, 0.8, 1.0, 1.3, 1.5, 1.8, 1.9, 1.7, 1.4, 1.1, 0.9, 0.7]

species:
  dust:
    - field: "dust_emissions"
      vdist_method: "PBL"
      seasonal_cycle: "pbl_seasonal"  # Account for seasonal PBL height changes
```

### Altitude-Dependent Aircraft Emissions
Model different flight phases with separate altitude ranges:

```yaml
species:
  aircraft_co:
    # Takeoff and landing (0-3 km)
    - field: "airport_co"
      category: "aviation"
      hierarchy: 1
      vdist_method: "HEIGHT"
      vdist_h_start: 0.0
      vdist_h_end: 3000.0

    # Climb and descent (3-9 km)
    - field: "climb_descent_co"
      category: "aviation"
      hierarchy: 2
      vdist_method: "HEIGHT"
      vdist_h_start: 3000.0
      vdist_h_end: 9000.0

    # Cruise (9-12 km)
    - field: "cruise_co"
      category: "aviation"
      hierarchy: 3
      vdist_method: "HEIGHT"
      vdist_h_start: 9000.0
      vdist_h_end: 12000.0
```

## Validation and Quality Assurance

### Mass Conservation Checking
ACES automatically validates mass conservation for all vertical distribution operations:

```cpp
// Automatic mass conservation validation
double total_2d = sum(emissions_2d);
double total_3d = sum(emissions_3d);
double conservation_error = abs(total_3d - total_2d) / total_2d;

if (conservation_error > 1.0e-12) {
    ACES_LOG_WARNING("Mass conservation error: " + std::to_string(conservation_error));
}
```

### Diagnostic Output
Enable vertical distribution diagnostics to validate results:

```yaml
diagnostics:
  output_interval_seconds: 3600
  variables:
    - "emissions_2d_input"      # Original 2D emissions
    - "emissions_3d_output"     # Final 3D emissions
    - "pbl_height"              # PBL height used for distribution
    - "pressure_levels"         # Pressure coordinate information
    - "height_levels"           # Height coordinate information
```

## Best Practices

1. **Choose Appropriate Methods**: Match distribution method to emission source physics
2. **Validate Mass Conservation**: Always check that vertical integration matches 2D input
3. **Consider Seasonal Variations**: Use temporal profiles for seasonally-varying vertical structure
4. **Monitor Performance**: Use simpler methods (SINGLE, RANGE) when appropriate for speed
5. **Test with Realistic Data**: Validate against observational vertical profiles when available