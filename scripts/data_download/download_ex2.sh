#!/bin/bash
mkdir -p data
./scripts/download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_4x5.nc -o data/$(basename HEMCO/MACCITY/v2014-07/MACCity_4x5.nc)
./scripts/download_hemco_data.py HEMCO/HTAPv3/v2022-12/2010/HTAPv3_CO_0.1x0.1_2010.nc -o data/$(basename HEMCO/HTAPv3/v2022-12/2010/HTAPv3_CO_0.1x0.1_2010.nc)
./scripts/download_hemco_data.py HEMCO/MASKS/v2014-07/Canada_mask.gen.1x1.nc -o data/$(basename HEMCO/MASKS/v2014-07/Canada_mask.gen.1x1.nc)
