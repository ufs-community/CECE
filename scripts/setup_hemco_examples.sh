#!/bin/bash
# setup_hemco_examples.sh
# Sets up ACES configuration files mimicking all 6 basic examples from HEMCO documentation.

set -e

mkdir -p examples
mkdir -p scripts/data_download

# Helper to generate download script for an example
generate_download_script() {
    local ex_num=$1
    local script_file="scripts/data_download/download_ex${ex_num}.sh"
    shift
    cat <<EOF > "$script_file"
#!/bin/bash
mkdir -p data
EOF
    for path in "$@"; do
        echo "./scripts/download_hemco_data.py $path -o data/\$(basename $path)" >> "$script_file"
    done
    chmod +x "$script_file"
}

# Example 1: Add global anthropogenic emissions (MACCity CO)
# DO NOT GENERATE YAMLS
generate_download_script 1 "HEMCO/MACCITY/v2014-07/MACCity_4x5.nc"

# Example 2: Overlay regional emissions
# DO NOT GENERATE YAMLS
generate_download_script 2 "HEMCO/MACCITY/v2014-07/MACCity_4x5.nc" "HEMCO/HTAPv3/v2022-12/2000/HTAPv3_CO_0.1x0.1_2000.nc" "HEMCO/MASKS/v2014-07/Canada_mask.gen.1x1.nc"

# Example 3: Adding the AEIC aircraft emissions
# DO NOT GENERATE YAMLS
generate_download_script 3 "HEMCO/MACCITY/v2014-07/MACCity_4x5.nc" "HEMCO/AEIC/v2015-01/AEIC.nc"

# Example 4: Add biomass burning emissions
# DO NOT GENERATE YAMLS
generate_download_script 4 "HEMCO/MACCITY/v2014-07/MACCity_4x5.nc" "HEMCO/GFED4/v2015-10/1997/GFED4_gen.025x025.199701.nc"

# Example 5: Tell HEMCO to use additional species
# DO NOT GENERATE YAMLS
generate_download_script 5 "HEMCO/MACCITY/v2014-07/MACCity_4x5.nc" "HEMCO/MACCITY/v2014-07/MACCity_anthro_NOx_2000-2010_16080.nc" "HEMCO/MACCITY/v2014-07/MACCity_anthro_SO2_2000-2010_16080.nc"

# Example 6: Non-separated inventories
# DO NOT GENERATE YAMLS
generate_download_script 6 "HEMCO/EDGARv43/v2014-10/EDGAR_v43.NOx.POW.nc" "HEMCO/CEDS/v2020-08/1970/ALK4_butanes-em-total-anthro_CEDS_1970.nc"

echo "Download scripts generated."
