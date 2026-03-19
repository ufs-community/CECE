#!/bin/bash
mkdir -p data
./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_4x5.nc -o data/MACCity_4x5.nc
./scripts/download_hemco_data.py HEMCO/MASKS/v2014-07/Canada_mask.gen.1x1.nc -o data/Canada_mask.gen.1x1.nc

