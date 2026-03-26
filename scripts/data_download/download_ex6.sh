#!/bin/bash
mkdir -p data
./scripts/download_hemco_data.py HEMCO/EDGARv43/v2014-10/EDGAR_v43.NOx.POW.nc -o data/$(basename HEMCO/EDGARv43/v2014-10/EDGAR_v43.NOx.POW.nc)
./scripts/download_hemco_data.py HEMCO/CEDS/v2020-08/1970/ALK4_butanes-em-total-anthro_CEDS_1970.nc -o data/$(basename HEMCO/CEDS/v2020-08/1970/ALK4_butanes-em-total-anthro_CEDS_1970.nc)
