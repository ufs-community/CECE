#!/bin/bash
mkdir -p data
./scripts/download_hemco_data.py HEMCO/HTAPv3/v2022-12/2018/HTAPv3_NO_0.1x0.1_2018.nc -o data/$(basename HEMCO/HTAPv3/v2022-12/2018/HTAPv3_NO_0.1x0.1_2018.nc)
# ./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_4x5.nc -o data/$(basename HEMCO/MACCITY/v2014-07/MACCity_4x5.nc)
# ./scripts/download_hemco_data.py HEMCO/GFED4/v2015-10/1997/GFED4_gen.025x025.199701.nc -o data/$(basename HEMCO/GFED4/v2015-10/1997/GFED4_gen.025x025.199701.nc)
