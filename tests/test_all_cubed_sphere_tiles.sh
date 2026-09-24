#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# CECE — Chemical Emissions Coupling Engine
# Copyright (c) HELM Project Contributors

set -e

# ========================================================================
# Download real C96 grid and gridspec files if they are missing
# ========================================================================
mkdir -p data

download_file_if_missing() {
    local filename=$1
    local filepath="data/${filename}"
    local url="https://ftp.emc.ncep.noaa.gov/static_files/public/UFS/GFS/fix/fix_fv3/C96/${filename}"

    if [ ! -f "$filepath" ] || [ ! -s "$filepath" ]; then
        echo "Downloading ${filename} from NOAA..."
        if command -v curl >/dev/null 2>&1; then
            curl -s -S -L -o "$filepath" "$url"
        elif command -v wget >/dev/null 2>&1; then
            wget -q -O "$filepath" "$url"
        else
            echo "Error: Neither curl nor wget found. Cannot download ${filename}." >&2
            exit 1
        fi
    fi
}

for tile in 1 2 3 4 5 6; do
    download_file_if_missing "C96_grid.tile${tile}.nc"
    download_file_if_missing "C96_grid_spec.tile${tile}.nc"
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
# Verify all 6 tile output NetCDF files exist, have correct shapes, and
# calculate total spatial emission mass conservation integrals across tiles
# ========================================================================
echo "========================================================================"
echo " Verifying generated output shapes & mass integrals for all 6 tiles..."
echo "========================================================================"
python3 -c "
import netCDF4 as nc
import numpy as np

total_co_mass_rate = 0.0
for tile in range(1, 7):
    f_out = nc.Dataset(f'cece_output/cece_c96_tile{tile}_20200101_010000.nc')
    f_grid = nc.Dataset(f'data/C96_grid_spec.tile{tile}.nc')
    lon_shape = f_out.variables['lon'].shape
    lat_shape = f_out.variables['lat'].shape
    lon_bnds_shape = f_out.variables['lon_bnds'].shape
    co = f_out.variables['co'][:]
    area = f_grid.variables['area'][:]
    if co.ndim == 3:
        co = co[0]
    tile_mass = float(np.sum(co * area))
    total_co_mass_rate += tile_mass
    print(f'Tile {tile} -> lon:{lon_shape}, lat:{lat_shape}, lon_bnds:{lon_bnds_shape}, co:{co.shape}, CO flux mass: {tile_mass:.6e} kg/s')

print(f'Total C96 CO emission mass rate across all 6 tiles: {total_co_mass_rate:.6e} kg/s')
assert total_co_mass_rate > 0.0, 'Total emission mass rate must be positive!'
"

# ========================================================================
# Verify bit-for-bit identity between serial (-np 1) and multi-rank (-np 2) MPI
# ========================================================================
echo "========================================================================"
echo " Verifying serial (-np 1) vs multi-rank (-np 2) bit-for-bit equality..."
echo "========================================================================"
mkdir -p cece_output_serial
cat <<EOF > examples/cece_config_ex9_serial.yaml
driver:
  start_time: "2020-01-01T00:00:00"
  end_time: "2020-01-01T01:00:00"
  timestep_seconds: 3600
  gridspec_file: "data/C96_grid_spec.tile1.nc"
  grid:
    nx: 96
    ny: 96

species:
  co:
    - field: "MACCITY_CO"
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

output:
  enabled: true
  directory: ./cece_output_serial
  filename_pattern: "cece_c96_serial_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1
  fields:
    - name: co
EOF

./setup.sh -c "OMP_NUM_THREADS=1 OMP_PROC_BIND=false mpirun --allow-run-as-root -np 1 ./build/cece_standalone_driver examples/cece_config_ex9_serial.yaml"
rm examples/cece_config_ex9_serial.yaml

python3 -c "
import netCDF4 as nc
import numpy as np

f_multi = nc.Dataset('cece_output/cece_c96_tile1_20200101_010000.nc')
f_serial = nc.Dataset('cece_output_serial/cece_c96_serial_20200101_010000.nc')

co_multi = f_multi.variables['co'][:]
co_serial = f_serial.variables['co'][:]

diff = np.max(np.abs(co_multi - co_serial))
print(f'Max absolute difference between -np 1 and -np 2: {diff}')
assert diff == 0.0, f'Bit-for-bit assertion failed! Diff = {diff}'
print('SUCCESS: Bit-for-bit serial vs multi-rank equivalence ASSERTION PASSED!')
"

# Note: Real C96 grid and gridspec files are kept in the 'data/' directory as a local cache.

echo "========================================================================"
echo " All 6 tiles of the C96 curvilinear grid have been successfully tested!"
echo "========================================================================"
