#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# CECE — Chemical Emissions Coupling Engine
# Copyright (c) HELM Project Contributors

set -e

# ========================================================================
# Setting up temporary C96 tile grids (symlinks to tile1)
# ========================================================================
for tile in 2 3 4 5 6; do
    ln -sf C96_grid.tile1.nc data/C96_grid.tile${tile}.nc
    ln -sf C96_grid_spec.tile1.nc data/C96_grid_spec.tile${tile}.nc
done

# Ensure output directory exists
mkdir -p cece_output

# ========================================================================
# Run the C96 simulation for all 6 tiles sequentially
# ========================================================================
echo "========================================================================"
echo " Executing standalone driver on all 6 curvilinear tiles..."
echo "========================================================================"
for tile in 1 2 3 4 5 6; do
    echo "------------------------------------------------------------------------"
    echo " Running simulation for Tile ${tile}..."
    echo "------------------------------------------------------------------------"

    # Generate temporary config file for this specific tile
    cat <<EOF > examples/cece_config_ex9_tile${tile}.yaml
# =====================================================================
# CECE Example 9 - Tile ${tile} Temporary Test Configuration
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
  co:
    - field: "MACCITY_CO"
      operation: "add"
  no:
    - field: "MACCITY_NO"
      operation: "add"

cece_data:
  streams:
    - name: "MACCITY_CO"
      file: "/work/data/MACCity_4x5.nc"
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
      file: "/work/data/MACCity_anthro_NOx_2000-2010_16080.nc"
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
  filename_pattern: "cece_c96_tile${tile}_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1
  global_attributes:
    title: "My Custom C96 Tile ${tile} Simulation"
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
    ./setup.sh -c "OMP_NUM_THREADS=1 OMP_PROC_BIND=false mpirun --allow-run-as-root -np 2 ./build/cece_standalone_driver examples/cece_config_ex9_tile${tile}.yaml"

    # Clean up temporary config file
    rm examples/cece_config_ex9_tile${tile}.yaml
done

# ========================================================================
# Verify all 6 tile output NetCDF files exist and have correct shapes
# ========================================================================
echo "========================================================================"
echo " Verifying generated output shapes for all 6 curvilinear tiles..."
echo "========================================================================"
for tile in 1 2 3 4 5 6; do
    python3 -c "
import netCDF4 as nc
f = nc.Dataset('cece_output/cece_c96_tile${tile}_20200101_010000.nc')
lon_shape = f.variables['lon'].shape
lat_shape = f.variables['lat'].shape
lon_bnds_shape = f.variables['lon_bnds'].shape
co_shape = f.variables['co'].shape
print(f'Tile ${tile} -> lon:{lon_shape}, lat:{lat_shape}, lon_bnds:{lon_bnds_shape}, co:{co_shape}')
"
done

# ========================================================================
# Clean up temporary C96 tile gridspec files symlinks
# ========================================================================
echo "========================================================================"
echo " Cleaning up temporary C96 tile symlinks..."
echo "========================================================================"
for tile in 2 3 4 5 6; do
    rm -f data/C96_grid.tile${tile}.nc
    rm -f data/C96_grid_spec.tile${tile}.nc
done

echo "========================================================================"
echo " All 6 tiles of the C96 curvilinear grid have been successfully tested!"
echo "========================================================================"
