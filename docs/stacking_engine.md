# The CECE Stacking Engine

The **Stacking Engine** is the computational core of CECE, responsible for combining multiple emission data layers into final emission fields for each chemical species. It implements a sophisticated hierarchy-based processing system that mimics and extends the HEMCO (Harmonized Emissions Component) approach while providing significant performance optimizations for modern HPC architectures.

## Overview

The Stacking Engine processes emission data through several key phases:

1. **Configuration Analysis** - Parse and validate the emission layer configuration
2. **Field Binding** - Connect to data sources (TIDE streams, ESMF fields)
3. **Hierarchy Processing** - Apply priority-based layer combination rules
4. **Kernel Fusion** - Generate optimized compute kernels for performance
5. **Temporal Scaling** - Apply time-dependent scaling factors
6. **Vertical Distribution** - Map 2D emissions to 3D atmospheric grids
7. **Provenance Tracking** - Record the complete emission calculation history

## Architecture and Performance

### Kernel Fusion Optimization

Unlike traditional approach of processing each layer sequentially, CECE uses **kernel fusion** to generate a single, optimized compute kernel per species. This provides several key advantages:

- **Reduced Memory Bandwidth**: Minimizes data movement between GPU/CPU memory and compute units
- **Lower Kernel Launch Overhead**: Single kernel launch per species instead of multiple
- **Better Cache Utilization**: Maximizes data reuse within compute kernels
- **Improved Parallelization**: Better load balancing across computational resources

### Hierarchy-Based Processing

The engine processes layers according to a two-level hierarchy system:

1. **Categories**: Logical groupings (e.g., `anthropogenic`, `biomass_burning`, `biogenic`)
2. **Hierarchy Levels**: Numerical priorities within categories (higher numbers take precedence)

```yaml
species:
  co:
    - field: "global_co_inventory"
      category: "anthropogenic"
      hierarchy: 1              # Base layer
      operation: "add"
    - field: "regional_co_override"
      category: "anthropogenic"
      hierarchy: 10             # Higher priority - replaces base
      operation: "replace"
```

## Layer Operations

### Basic Operations

The Stacking Engine supports four fundamental operations for combining emission layers:

| Operation | Description | Behavior |
|-----------|-------------|----------|
| `add` | Accumulative | Adds layer emissions to current accumulated value |
| `multiply` | Multiplicative | Multiplies current accumulated value by layer |
| `replace` | Override | Replaces accumulated value with layer emissions |
| `set` | Initialization | Sets initial value (typically used for first layer) |

### Masking and Scaling

Each layer can be modified through multiple mechanisms applied in sequence:

1. **Base Scale Factor**: Simple numerical multiplier applied to raw emission data
2. **Scale Fields**: Dynamic field-based scaling (e.g., temperature dependence)
3. **Geographical Masks**: 2D or 3D fields that spatially restrict emissions
4. **Temporal Profiles**: Time-varying scale factors (diurnal, weekly, seasonal)

```yaml
species:
  isoprene:
    - field: "base_isoprene"
      scale: 1.5                           # Base scale factor
      mask: "vegetation_mask"              # Apply only over vegetation
      scale_fields: ["temperature", "lai"] # Scale with temp and leaf area
      diurnal_cycle: "biogenic_diurnal"    # Apply diurnal variation
      operation: "add"
```

## Vertical Distribution

CECE supports multiple algorithms for distributing 2D surface emissions into 3D atmospheric volumes:

### Distribution Methods

| Method | Description | Use Cases |
|--------|-------------|-----------|
| `SINGLE` | All emissions in one vertical level | Aircraft emissions, elevated sources |
| `RANGE` | Even distribution over layer range | Industrial stacks, biomass burning |
| `PRESSURE` | Distribution based on pressure bounds | Free troposphere sources |
| `HEIGHT` | Distribution based on altitude bounds | Topography-dependent sources |
| `PBL` | Distribution within planetary boundary layer | Surface-based anthropogenic sources |

### Mass Conservation

All vertical distribution methods ensure strict mass conservation:

```
∑(emissions_3d[i,j,:]) = emissions_2d[i,j]  ∀ i,j
```

### Configuration Example

```yaml
species:
  nox:
    - field: "aircraft_nox"
      vdist_method: "HEIGHT"
      vdist_h_start: 8000.0    # 8 km altitude
      vdist_h_end: 12000.0     # 12 km altitude
      operation: "add"
    - field: "surface_nox"
      vdist_method: "PBL"      # Distribute in boundary layer
      operation: "add"
```

## Temporal Scaling

The Stacking Engine applies time-dependent scale factors to account for temporal emission variability:

### Cycle Types

- **Diurnal**: 24-hour cycle (hourly scale factors)
- **Weekly**: 7-day cycle (daily scale factors)
- **Seasonal**: 12-month cycle (monthly scale factors)

### Implementation

Temporal profiles are defined globally and referenced by layers:

```yaml
temporal_profiles:
  traffic_diurnal: [0.5, 0.3, 0.2, 0.3, 0.6, 1.2, 1.8, 1.5, 1.2, 1.0, 1.1, 1.2,
                    1.3, 1.2, 1.3, 1.5, 1.8, 2.0, 1.8, 1.5, 1.2, 1.0, 0.8, 0.6]

  weekday_pattern: [1.2, 1.3, 1.3, 1.3, 1.3, 0.8, 0.7]  # Mon-Sun

species:
  co:
    - field: "traffic_co"
      diurnal_cycle: "traffic_diurnal"
      weekly_cycle: "weekday_pattern"
      operation: "add"
```

## Performance Considerations

### Memory Layout

The Stacking Engine uses Kokkos Views with optimized memory layouts:

```cpp
// Device-optimized layout for GPU execution
using DeviceView3D = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;

// Host mirror for CPU-GPU data transfer
using HostMirror3D = Kokkos::View<double***, Kokkos::LayoutLeft>::HostMirror;
```

### Execution Patterns

- **Parallel Field Operations**: All grid points processed simultaneously
- **Hierarchical Layer Processing**: Layers processed in priority order
- **Fused Kernel Execution**: Single kernel per species combines all operations
- **Asynchronous Data Transfer**: Overlapped CPU-GPU memory transfers

### Scaling Performance

Typical performance characteristics on modern HPC systems:

| Grid Size | CPU Cores | GPU | Throughput |
|-----------|-----------|-----|------------|
| 1440×721×72 | 40 (Intel Xeon) | - | ~50 species/sec |
| 1440×721×72 | - | V100 | ~200 species/sec |
| 3600×1801×72 | - | A100 | ~150 species/sec |

## Provenance Tracking

The Stacking Engine provides complete scientific traceability through its provenance system:

### Tracked Information

- **Layer Contributions**: Which fields contribute to each species
- **Hierarchy Application**: Order and priority of layer processing
- **Scaling Factors**: All applied temporal and spatial scale factors
- **Operation History**: Complete record of mathematical operations
- **Field Sources**: Data source identification (TIDE/ESMF/computed)

### Provenance Output

```yaml
# Example provenance report excerpt
species: CO
time_context: hour=14 day_of_week=2 month=7
contributing_layers:
  - field: global_co_base
    operation: add
    hierarchy: 1
    category: anthropogenic
    effective_scale: 1.25  # Base scale × temporal factors
    masks: [land_mask]
  - field: regional_co_override
    operation: replace
    hierarchy: 10
    effective_scale: 0.85
    geographic_bounds: [lon_min: -125, lon_max: -65, lat_min: 25, lat_max: 50]
```

## Advanced Configuration

### Multi-Scale Processing

Complex emission scenarios can use multiple scale factors simultaneously:

```yaml
species:
  biogenic_voc:
    - field: "base_biogenic"
      scale: 2.0                           # Literature adjustment factor
      scale_fields: ["temperature", "par", "lai"]  # Environmental dependencies
      masks: ["vegetation_mask", "growing_season"] # Geographic/temporal masks
      operation: "add"
```

### Category-Based Hierarchies

Organize related emission sources using categories:

```yaml
species:
  nox:
    # Transportation category
    - field: "road_transport"
      category: "transportation"
      hierarchy: 1
    - field: "aviation"
      category: "transportation"
      hierarchy: 2
    - field: "shipping"
      category: "transportation"
      hierarchy: 3

    # Industrial category
    - field: "power_plants"
      category: "industrial"
      hierarchy: 1
    - field: "cement_production"
      category: "industrial"
      hierarchy: 2
```

## Integration with Physics Schemes

The Stacking Engine coordinates with CECE physics schemes:

1. **Base Field Processing**: Stacking Engine processes static/inventory emissions
2. **Physics Augmentation**: Physics schemes modify or add to stacked emissions
3. **Final Assembly**: Combined results synchronized to ESMF export state

### Execution Order

```
1. Parse Configuration → Validate layers and hierarchy
2. Bind Data Fields → Connect to TIDE and ESMF data sources
3. Stack Base Emissions → Apply hierarchy and scaling rules
4. Execute Physics Schemes → Run active emission parameterizations
5. Apply Diagnostics → Capture intermediate and final results
6. Synchronize Output → Transfer to ESMF for coupled model use
```

This architecture ensures that both inventory-based and process-based emissions are properly integrated while maintaining high computational performance and complete scientific traceability.