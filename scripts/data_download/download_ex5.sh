#!/bin/bash
mkdir -p data
./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_4x5.nc -o data/MACCity_4x5.nc
./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_anthro_NOx_2000-2010_16080.nc -o data/MACCity_anthro_NOx_2000-2010_16080.nc
./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_anthro_SO2_2000-2010_16080.nc -o data/MACCity_anthro_SO2_2000-2010_16080.nc

