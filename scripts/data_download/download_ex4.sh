#!/bin/bash
mkdir -p data
./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_4x5.nc -o data/MACCity_4x5.nc
./scripts/download_hemco_data.py HEMCO/GFED4/v2015-10/1997/GFED4_gen.025x025.199701.nc -o data/GFED4_gen.025x025.199701.nc

