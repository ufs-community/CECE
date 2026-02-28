#!/bin/bash
# setup_hemco_examples.sh
# Sets up ACES configuration files mimicking all 6 basic examples from HEMCO documentation.

set -e

mkdir -p examples

# Example 1: Add global anthropogenic emissions (MACCity CO)
cat <<EOF > examples/aces_config_ex1.yaml
# ACES equivalent of HEMCO Example 1
met_mapping:
  hourly_scalfact: HOURLY_SCALFACT

species:
  co:
    - field: "MACCITY_CO"
      operation: "add"
      scale: 1.0
      scale_fields: ["hourly_scalfact"]

cdeps_config:
  streams:
    - name: "MACCITY_CO"
      file_path: "data/MACCity.nc"
    - name: "HOURLY_SCALFACT"
      file_path: "data/hourly.nc"
EOF

# Example 2: Overlay regional emissions (EMEP CO replaces MACCity CO)
cat <<EOF > examples/aces_config_ex2.yaml
# ACES equivalent of HEMCO Example 2
met_mapping:
  hourly_scalfact: HOURLY_SCALFACT
  mask_europe: MASK_EUROPE

species:
  co:
    - field: "MACCITY_CO"
      category: 1
      hierarchy: 1
      operation: "add"
      scale_fields: ["hourly_scalfact"]
    - field: "EMEP_CO"
      category: 1
      hierarchy: 2
      operation: "replace"
      masks: ["mask_europe"]
      scale_fields: ["hourly_scalfact"]

cdeps_config:
  streams:
    - name: "MACCITY_CO"
      file_path: "data/MACCity.nc"
    - name: "EMEP_CO"
      file_path: "data/EMEP.nc"
    - name: "HOURLY_SCALFACT"
      file_path: "data/hourly.nc"
    - name: "MASK_EUROPE"
      file_path: "data/mask_europe.nc"
EOF

# Example 3: Adding the AEIC aircraft emissions (New Category)
cat <<EOF > examples/aces_config_ex3.yaml
# ACES equivalent of HEMCO Example 3
species:
  co:
    - field: "MACCITY_CO"
      category: 1
      hierarchy: 1
      operation: "add"
    - field: "AEIC_CO"
      category: 2
      hierarchy: 1
      operation: "add"

cdeps_config:
  streams:
    - name: "MACCITY_CO"
      file_path: "data/MACCity.nc"
    - name: "AEIC_CO"
      file_path: "data/AEIC.nc"
EOF

# Example 4: Add biomass burning emissions (HEMCO Extension)
cat <<EOF > examples/aces_config_ex4.yaml
# ACES equivalent of HEMCO Example 4
physics_schemes:
  - name: "GFED"
    options:
      version: "GFED4"

species:
  co:
    - field: "MACCITY_CO"
      category: 1
      hierarchy: 1
      operation: "add"

cdeps_config:
  streams:
    - name: "MACCITY_CO"
      file_path: "data/MACCity.nc"
    - name: "GFED_WDL"
      file_path: "data/GFED4/GFED4_gen.nc"
EOF

# Example 5: Tell HEMCO to use additional species
cat <<EOF > examples/aces_config_ex5.yaml
# ACES equivalent of HEMCO Example 5
met_mapping:
  so2_to_so4: SO2toSO4

species:
  co:
    - field: "MACCITY_CO"
      operation: "add"
  no:
    - field: "MACCITY_NO"
      operation: "add"
  so2:
    - field: "MACCITY_SO2"
      operation: "add"
  so4:
    - field: "MACCITY_SO2" # Mimic deriving SO4 from SO2
      operation: "add"
      scale_fields: ["so2_to_so4"]

cdeps_config:
  streams:
    - name: "MACCITY_CO"
      file_path: "data/MACCity.nc"
    - name: "MACCITY_NO"
      file_path: "data/MACCity.nc"
    - name: "MACCITY_SO2"
      file_path: "data/MACCity.nc"
    - name: "SO2toSO4"
      # In ACES, this can be a constant stream or a scale factor
      file_path: "data/constants.nc"
EOF

# Example 6: Add inventories that do not separate out biofuels and/or trash emissions
cat <<EOF > examples/aces_config_ex6.yaml
# ACES equivalent of HEMCO Example 6
species:
  no:
    - field: "EDGAR_NO_POW"
      category: 1
      operation: "add"
    - field: "CEDS_NO_AGR"
      category: 1
      operation: "add"

cdeps_config:
  streams:
    - name: "EDGAR_NO_POW"
      file_path: "data/EDGAR_v43.NOx.POW.nc"
    - name: "CEDS_NO_AGR"
      file_path: "data/NO-em-anthro_CMIP_CEDS.nc"
EOF

echo "All 6 ACES example configurations created in the 'examples/' directory."
