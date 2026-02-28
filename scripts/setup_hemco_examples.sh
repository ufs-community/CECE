#!/bin/bash
# setup_hemco_examples.sh
# Sets up ACES configuration files mimicking basic examples from HEMCO documentation.

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

# Note: In ACES, CDEPS-inline handles file reading.
# You would define the streams in cdeps_config.
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

echo "ACES example configurations created in the 'examples/' directory."
