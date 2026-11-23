#!/bin/bash
set -euo pipefail

# ========================================================================
# Download real C96 grid and gridspec files if they are missing
# ========================================================================
mkdir -p data

download_file_if_missing() {
    local filename=$1
    local filepath="data/${filename}"
    local url="https://ftp.emc.ncep.noaa.gov/static_files/public/UFS/GFS/fix/fix_fv3/C96/${filename}"

    if [ ! -f "$filepath" ] || [ ! -s "$filepath" ]; then
        echo "Downloading ${filename} from NOAA..."
        if command -v curl >/dev/null 2>&1; then
            curl -s -S -L -o "$filepath" "$url"
        elif command -v wget >/dev/null 2>&1; then
            wget -q -O "$filepath" "$url"
        else
            echo "Error: Neither curl nor wget found. Cannot download ${filename}." >&2
            exit 1
        fi
    fi
}

for tile in 1 2 3 4 5 6; do
    download_file_if_missing "C96_grid.tile${tile}.nc"
    download_file_if_missing "C96_grid_spec.tile${tile}.nc"
done
