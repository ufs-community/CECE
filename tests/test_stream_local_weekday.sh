#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) HELM Project Contributors
#
# End-to-end test of the stream-level use_local_time option (weekly cadence).
#
# Runs the real cece_standalone_driver on a 7-record weekly file whose record k
# holds k + 1, from Monday 20:00Z to 22:00Z, and checks that each cell reads the
# record of its LOCAL weekday: zones already past local midnight read Tuesday's
# record, the others Monday's. Covers 1 and 2 ranks, plus the UTC baseline with
# the option off.
#
# Usage:
#   test_stream_local_weekday.sh --driver <cece_standalone_driver> \
#       --grid-file <utc_grid_720r.rle> --workdir <scratch dir>
#
# Exits 77 (ctest SKIP) when python3 with netCDF4/numpy is unavailable.

set -u

DRIVER=""
GRID_FILE=""
WORKDIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --driver)    DRIVER="$2"; shift 2 ;;
        --grid-file) GRID_FILE="$2"; shift 2 ;;
        --workdir)   WORKDIR="$2"; shift 2 ;;
        *) echo "ERROR: unrecognized argument: $1" >&2; exit 2 ;;
    esac
done

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -x "$DRIVER" ]    || fail "driver not found or not executable: $DRIVER"
[ -f "$GRID_FILE" ] || fail "UTC offset grid not found: $GRID_FILE"
[ -n "$WORKDIR" ]   || fail "--workdir is required"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELPER="$HERE/stream_local_weekday_helper.py"

if ! python3 -c "import netCDF4, numpy" > /dev/null 2>&1; then
    echo "SKIP: python3 with netCDF4 and numpy is required"
    exit 77
fi

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR" || fail "could not create workdir: $WORKDIR"
trap 'rm -rf "$WORKDIR"' EXIT

python3 "$HELPER" make-input "$WORKDIR" || fail "could not create the weekly input file"

MPIRUN="${MPIEXEC_EXECUTABLE:-mpirun}"
NPFLAG="${MPIEXEC_NUMPROC_FLAG:--np}"
MPI_PREFLAGS=(--allow-run-as-root --oversubscribe)

run_case() {
    local name="$1" local_time_line="$2" np="$3" mode="$4"
    local outdir="$WORKDIR/out_$name"
    mkdir -p "$outdir"
    cat > "$WORKDIR/cfg_$name.yaml" <<EOF
driver:
  start_time: "2026-01-12T20:00:00"
  end_time: "2026-01-12T22:00:00"
  timestep_seconds: 3600
  grid:
    nx: 72
    ny: 36
    lon_min: -180.0
    lon_max: 180.0
    lat_min: -90.0
    lat_max: 90.0

local_time:
  enabled: true
  grid_file: "$GRID_FILE"

species:
  wk:
    - field: "FW"
      operation: "add"

cece_data:
  streams:
    - name: "WEEKLY"
      file: "$WORKDIR/weekly.nc"
      cadence: "weekly"
      mapalgo: "consd"
$local_time_line
      variables:
        - file: "FW"
          model: "FW"

output:
  enabled: true
  directory: "$outdir"
  filename_pattern: "wk_{YYYY}{MM}{DD}_{HH}{mm}{ss}.nc"
  frequency_steps: 1
  fields:
    - name: wk
EOF
    "$MPIRUN" "${MPI_PREFLAGS[@]}" "$NPFLAG" "$np" "$DRIVER" "$WORKDIR/cfg_$name.yaml" > "$WORKDIR/run_$name.log" 2>&1 \
        || { cat "$WORKDIR/run_$name.log"; fail "driver failed for case $name"; }
    python3 "$HELPER" check "$outdir" "$mode" || fail "unexpected values for case $name"
    echo "PASS: $name"
}

run_case local_np1 "      use_local_time: true" 1 local
run_case local_np2 "      use_local_time: true" 2 local
run_case utc_np1 "" 1 utc

echo "PASS: stream local weekday"
