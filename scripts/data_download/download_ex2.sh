#!/bin/bash
mkdir -p data
./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_4x5.nc -o data/$(basename HEMCO/MACCITY/v2014-07/MACCity_4x5.nc)
curl -Lfo data/EMEP.2000.co.nc "https://webdab01.umweltbundesamt.at/download/gridding2026/NetCDF/CO_2026_GRID_1990_to_2024.nc?cgiproxy_skip=1"
./scripts/download_hemco_data.py HEMCO/MASKS/v2014-07/Canada_mask.gen.1x1.nc -o data/$(basename HEMCO/MASKS/v2014-07/Canada_mask.gen.1x1.nc)
