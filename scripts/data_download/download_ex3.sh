#!/bin/bash
mkdir -p data
./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_4x5.nc -o data/MACCity_4x5.nc
./scripts/download_hemco_data.py HEMCO/AEIC/v2015-01/AEIC.nc -o data/AEIC.nc

