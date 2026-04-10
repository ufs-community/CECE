# CECE Configuration Examples

This directory contains example YAML configuration files demonstrating various CECE capabilities and use cases.

## Example Descriptions

### cece_config_ex1.yaml
**Basic Single Species Example**
- Simple CO emissions from a single MACCITY stream
- Global grid (4x4) with full longitude/latitude coverage
- 3-hour simulation (2020-01-01 00:00:00 to 03:00:00)
- Basic diagnostics output
- Good starting point for new users

### cece_config_ex2.yaml
**Regional Masking Example**
- CO emissions with hierarchical data sources (MACCITY + EMEP)
- European regional grid (8x6) focusing on Europe
- 2-hour simulation (2020-01-01 00:00:00 to 02:00:00)
- Demonstrates masking capability for regional overrides
- Shows field hierarchy and replacement operations

### cece_config_ex3.yaml
**Minimal Testing Configuration**
- Simplified CO emissions setup
- Minimal grid (2x2) for fast testing
- 1-hour simulation (2020-01-01 00:00:00 to 01:00:00)
- Basic stream configuration without timing parameters
- Useful for debugging and quick testing

### cece_config_ex4.yaml
**Physics Schemes Demonstration**
- NO emissions with physics scheme processing
- Demonstrates DayCycleAndSeasonVariation scheme
- 24-hour simulation (2020-01-01 00:00:00 to 2020-01-02 00:00:00)
- Shows physics scheme parameter configuration
- Medium resolution grid (6x4)

### cece_config_ex5.yaml
**Multi-Timestep Execution**
- Multi-species (CO + NO) emissions
- Two distinct data streams with different temporal handling
- 6-hour simulation (2020-01-01 00:00:00 to 06:00:00)
- Regional grid configuration (-135° to 135°)
- Production-ready configuration for multi-timestep runs

### cece_config_ex6.yaml
**Multi-Stream Emissions**
- Multiple NO emission sources (EDGAR + CEDS)
- Demonstrates additive emission operations
- 4-hour simulation (2020-01-01 00:00:00 to 04:00:00)
- Different data source temporal configurations
- Regional grid matching ex5

## Configuration Sections

All examples include these configurable sections:

### Driver Configuration (`driver:`)
- `start_time`: Simulation start time (ISO8601 format, e.g. "2020-01-01T00:00:00")
- `end_time`: Simulation end time (ISO8601 format)
- `timestep_seconds`: Timestep in seconds (typically 3600 for 1-hour steps)

### Grid Configuration (`grid:`)
- `nx`, `ny`: Grid dimensions
- `lon_min`, `lon_max`: Longitude bounds
- `lat_min`, `lat_max`: Latitude bounds

This allows full configurability without hardcoded values for both timing and spatial domains.

## Usage

**Simplified Command (Recommended):**
```bash
./setup.sh -c "cd /work && ./build/bin/cece_nuopc_app examples/cece_config_exN.yaml"
```

**Legacy Command (Still Supported):**
```bash
./setup.sh -c "cd /work && ./build/bin/cece_nuopc_app driver.cfg examples/cece_config_exN.yaml"
```

Replace `N` with the example number (1-6).

## Notes

- **Fully configurable** - Both timing and grid parameters read from YAML
- **Examples are self-contained** - Just specify the YAML file
- **Flexible timing** - Each example has different simulation durations for different use cases
- **No `.cfg` files needed** - All configuration in YAML format