# Using the Provided Examples

ACES includes several example configurations that demonstrate common emission stacking scenarios, modeled after examples in the HEMCO guide and showcasing advanced ACES features.

## Example Scenarios

The `examples/` directory contains several YAML configuration files:

-   `aces_config_ex1.yaml`: Basic single CO species with TIDE data ingestion
-   `aces_config_ex2.yaml`: Overlaying a regional European CO inventory on a global background
-   `aces_config_ex3.yaml`: Simple testing configuration with minimal grid
-   `aces_config_ex4.yaml`: Using the GFED4 extension for biomass burning
-   `aces_config_ex5.yaml`: Multi-species (CO and NO) emissions with multi-timestep execution
-   `aces_config_ex6.yaml`: Handling non-separated inventories
-   `aces_config_advanced.yaml`: **NEW** - Comprehensive example demonstrating advanced Stacking Engine features

### Advanced Example Highlights

The `aces_config_advanced.yaml` example showcases sophisticated emission processing capabilities:

- **Hierarchical Layer Processing**: Multiple priority levels within categories
- **Temporal Scaling**: Diurnal, weekly, and seasonal emission cycles
- **Vertical Distribution**: Multiple algorithms (PBL, HEIGHT, PRESSURE) for different source types
- **Environmental Dependencies**: Temperature, PAR, and LAI-dependent scaling
- **Geographical Masking**: Land/ocean/vegetation/regional masks
- **Physics Scheme Integration**: Active MEGAN, sea salt, and dust schemes
- **Multi-Source Integration**: TIDE streams from multiple emission inventories

For complete technical details about how these features work, see the [Stacking Engine Documentation](stacking_engine.md).

---

## Configuration Features by Example

| Example | Grid Size | Species | Key Features |
|---------|-----------|---------|---------------|
| ex1 | 4×4 | CO | Basic TIDE integration, simple stacking |
| ex2 | (varies) | CO | Regional override with hierarchy |
| ex3 | 2×2 | CO | Minimal test configuration |
| ex4 | (varies) | Multiple | Biomass burning with GFED4 |
| ex5 | 4×4 | CO, NO | Multi-species, multi-timestep execution |
| ex6 | (varies) | Multiple | Non-separated inventory handling |
| **advanced** | **144×91** | **CO, NOx, Isoprene** | **All advanced features demonstrated** |

---

## Setting Up Examples

To run these examples, you need the associated NetCDF data files. ACES provides a script to automate the setup process.

### 1. Run the Setup Script
```bash
./scripts/setup_hemco_examples.sh
```
This script will:
-   Create the `examples/` directory if it doesn't exist.
-   Copy or generate the example configuration files.
-   Create a `scripts/data_download/` directory with shell scripts to download the required data from S3.

### 2. Download Data
Choose an example to run and execute its data download script:
```bash
# Example: Download data for Example 1
./scripts/data_download/download_ex1.sh
```
This will download the necessary NetCDF files into the `data/` directory.

### 3. Run the Example
You can use the standalone NUOPC driver to run any of the example configurations:
```bash
# Example: Run Example 1
./build/bin/aces_nuopc_driver --config examples/aces_config_ex1.yaml
```
The driver will perform the simulation steps and produce diagnostic output as configured in the YAML file.

---

## Visualizing Example Plans

To better understand the stacking hierarchy of an example, use the visualization utility:
```bash
python scripts/visualize_stack.py --config examples/aces_config_ex2.yaml --output ex2_stack.png
```
This will generate a graph showing how the different layers (global background and regional override) are prioritized and combined.
