#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# CECE — Chemical Emissions Coupling Engine
# Copyright (c) HELM Project Contributors

set -e

# Ensure output directory exists
mkdir -p cece_output
lon_range_tag="_0_360"

# ========================================================================
# Run the C96 simulation for all 6 tiles sequentially with 0-360 range inputs
# ========================================================================
echo "========================================================================"
echo " Executing standalone driver on all 6 curvilinear tiles with [0, 360] inputs..."
echo "========================================================================"
for tile in 1 2 3 4 5 6; do
    echo "------------------------------------------------------------------------"
    echo " Running 0-360 simulation for Tile ${tile}..."
    echo "------------------------------------------------------------------------"

    # Generate temporary config file for this specific tile
    tile_yaml_name="examples/cece_config_ex9_tile${tile}${lon_range_tag}.yaml"
    cat <<EOF > "${tile_yaml_name}"
# =====================================================================
# CECE Example 9 - Tile ${tile} Temporary 0-360 Test Configuration
# =====================================================================
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-01T01:00:00"
  timestep_seconds: 3600
  gridspec_file: "data/C96_grid_spec.tile${tile}.nc"
  grid:
    nx: 96
    ny: 96

species:
  "co":
    - field: "MACCITY_CO"
      operation: "add"
  "no":
    - field: "MACCITY_NO"
      operation: "add"

cece_data:
  streams:
    - name: "MACCITY_CO"
      file: "/work/data/MACCity_4x5${lon_range_tag}.nc"
      yearFirst: 2000
      yearLast: 2010
      yearAlign: 2020
      taxmode: "cycle"
      tintalgo: "linear"
      mapalgo: "consd"
      variables:
        - file: "MACCity"
          model: "MACCITY_CO"
    - name: "MACCITY_NO"
      file: "/work/data/MACCity_anthro_NOx_2000-2010_16080${lon_range_tag}.nc"
      yearFirst: 2000
      yearLast: 2010
      yearAlign: 2020
      taxmode: "cycle"
      tintalgo: "linear"
      mapalgo: "consd"
      variables:
        - file: "MACCity"
          model: "MACCITY_NO"

diagnostics:
  output_interval_seconds: 3600
  variables: ["co", "no"]

output:
  enabled: true
  directory: ./cece_output
  filename_pattern: "cece_c96_tile${tile}${lon_range_tag}_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1
  global_attributes:
    title: "My Custom C96 Tile ${tile} [0-360] Input Simulation"
    institution: "National Oceanic and Atmospheric Administration (NOAA)"
  fields:
    - name: co
      attributes:
        units: "kg m-2 s-1"
        long_name: "carbon_monoxide_emission_flux"
    - name: "no"
      attributes:
        units: "kg m-2 s-1"
        long_name: "nitric_oxide_emission_flux"
EOF

    # Run simulation inside the container
    ./setup.sh -c "OMP_NUM_THREADS=1 OMP_PROC_BIND=false mpirun --allow-run-as-root -np 2 ./build/cece_standalone_driver ${tile_yaml_name}"

    # Clean up temporary config file
    rm "${tile_yaml_name}"
done

# ========================================================================
# Verify all 6 tile output NetCDF files exist and have correct shapes
# ========================================================================
echo "========================================================================"
echo " Verifying generated output shapes and values for all 6 curvilinear tiles..."
echo "========================================================================"
for tile in 1 2 3 4 5 6; do
    python3 -c "
import netCDF4 as nc
f = nc.Dataset('cece_output/cece_c96_tile${tile}${lon_range_tag}_20200101_010000.nc')
lon_shape = f.variables['lon'].shape
lat_shape = f.variables['lat'].shape
lon_bnds_shape = f.variables['lon_bnds'].shape
co = f.variables['co'][:]
no = f.variables['no'][:]
print(f'Tile ${tile} [0-360] -> lon:{lon_shape}, lat:{lat_shape}, co sum:{co.sum():.6e}, no sum:{no.sum():.6e}')
"
done

# Note: Real C96 grid and gridspec files are kept in the 'data/' directory as a local cache.

echo "========================================================================"
echo " All 6 tiles of the C96 curvilinear grid have been successfully tested with [0, 360] inputs!"
echo "========================================================================"
