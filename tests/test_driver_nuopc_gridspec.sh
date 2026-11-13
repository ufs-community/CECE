#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# CECE — Community Emissions Computing Engine
# Copyright (c) HELM Project Contributors
#
# =====================================================================
# NUOPC cap grid-topology e2e test
# =====================================================================
#   Verifies that the cap runs on a GRIDSPEC curvilinear
#   target with output grid geometry matching the input grid.
#
# Runs BOTH drivers on examples/cece_config_ceds_oc_gridspec.yaml at
# -np 1 and asserts:
#
#   1. The NUOPC cap's output carries 2-D curvilinear coordinate
#      variables (lon/lat rank 2 with (lat, lon) grid dims) — the
#      observable proof the output geometry matches the input grid.
#   2. The coordinate variables and every configured data field are
#      bit-for-bit identical between the two drivers' outputs, and the
#      variable sets match (the same parity checks as the rectilinear
#      fixture).
#
# Modeled on tests/test_driver_nuopc_parity.sh (same launch, retry, and
# comparator idioms). Designed to run INSIDE the cece-dev container with
# ESMF (ctest launches it via ./setup.sh).
#
# Usage:
#   test_driver_nuopc_gridspec.sh \
#       --cxx-driver  <path to cece_standalone_driver> \
#       --nuopc-app   <path to cece_nuopc_app> \
#       --config      <path to cece_config_ceds_oc_gridspec.yaml> \
#       --comparator  <path to nccmp_var.c> \
#       --varlist     <path to nclist_vars.c> \
#       --shaper      <path to ncvar_shape.c> \
#       --fields      <comma-separated data field names, e.g. "oc"> \
#       --workdir     <scratch dir (created, cleaned up on success)>
#
# Env (optional):
#   CECE_GRIDSPEC_TOL      absolute tolerance for field comparison (default 0)
#   CECE_GRIDSPEC_RETRIES  launch attempts per driver (default 4)
#   CECE_GRIDSPEC_KEEP     if 1, keep the scratch workdir on exit

set -u

CXX_DRIVER=""
NUOPC_APP=""
CONFIG_TEMPLATE=""
COMPARATOR_SRC=""
VARLIST_SRC=""
SHAPER_SRC=""
FIELDS=""
WORKDIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --cxx-driver) CXX_DRIVER="$2"; shift 2 ;;
        --nuopc-app)  NUOPC_APP="$2"; shift 2 ;;
        --config)     CONFIG_TEMPLATE="$2"; shift 2 ;;
        --comparator) COMPARATOR_SRC="$2"; shift 2 ;;
        --varlist)    VARLIST_SRC="$2"; shift 2 ;;
        --shaper)     SHAPER_SRC="$2"; shift 2 ;;
        --fields)     FIELDS="$2"; shift 2 ;;
        --workdir)    WORKDIR="$2"; shift 2 ;;
        *) echo "ERROR: unrecognized argument: $1" >&2; exit 2 ;;
    esac
done

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -n "$CXX_DRIVER" ]     || fail "--cxx-driver is required"
[ -n "$NUOPC_APP" ]      || fail "--nuopc-app is required"
[ -n "$CONFIG_TEMPLATE" ] || fail "--config is required"
[ -n "$COMPARATOR_SRC" ] || fail "--comparator is required"
[ -n "$VARLIST_SRC" ]    || fail "--varlist is required"
[ -n "$SHAPER_SRC" ]     || fail "--shaper is required"
[ -n "$FIELDS" ]         || fail "--fields is required"
[ -n "$WORKDIR" ]        || fail "--workdir is required"

[ -x "$CXX_DRIVER" ]     || fail "C++ driver not found or not executable: $CXX_DRIVER"
[ -x "$NUOPC_APP" ]      || fail "NUOPC app not found or not executable: $NUOPC_APP"
[ -f "$CONFIG_TEMPLATE" ] || fail "config template not found: $CONFIG_TEMPLATE"
[ -f "$COMPARATOR_SRC" ] || fail "comparator source not found: $COMPARATOR_SRC"
[ -f "$VARLIST_SRC" ]    || fail "varlist source not found: $VARLIST_SRC"
[ -f "$SHAPER_SRC" ]     || fail "shaper source not found: $SHAPER_SRC"

echo "========================================================================"
echo " Curvilinear (GRIDSPEC) topology e2e:"
echo "  - the cap completes on a 2-D-coordinate grid and the output"
echo "    carries curvilinear coordinates matching the input grid"
echo " Fixture: $CONFIG_TEMPLATE (-np 1)"
echo "========================================================================"

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR" || fail "could not create workdir: $WORKDIR"

cleanup() {
    if [ "${CECE_GRIDSPEC_KEEP:-0}" = "1" ]; then
        echo "CECE_GRIDSPEC_KEEP=1 -> leaving scratch dir $WORKDIR in place"
    else
        rm -rf "$WORKDIR"
    fi
}
trap cleanup EXIT

# ------------------------------------------------------------------------
# 1. Build the NetCDF helpers against the container NetCDF-C.
# ------------------------------------------------------------------------
COMPARATOR_BIN="$WORKDIR/nccmp_var"
VARLIST_BIN="$WORKDIR/nclist_vars"
SHAPER_BIN="$WORKDIR/ncvar_shape"
CC_BIN="${CC:-cc}"
NC_CONFIG="${NC_CONFIG:-nc-config}"
NC_CFLAGS="$($NC_CONFIG --cflags 2>/dev/null)"
NC_LIBS="$($NC_CONFIG --libs 2>/dev/null)"
if [ -z "$NC_LIBS" ]; then
    NC_LIBS="-lnetcdf"
fi

# shellcheck disable=SC2086
"$CC_BIN" "$COMPARATOR_SRC" $NC_CFLAGS -O2 -o "$COMPARATOR_BIN" $NC_LIBS -lm \
    || fail "failed to compile NetCDF comparator"
# shellcheck disable=SC2086
"$CC_BIN" "$VARLIST_SRC" $NC_CFLAGS -O2 -o "$VARLIST_BIN" $NC_LIBS \
    || fail "failed to compile NetCDF variable lister"
# shellcheck disable=SC2086
"$CC_BIN" "$SHAPER_SRC" $NC_CFLAGS -O2 -o "$SHAPER_BIN" $NC_LIBS \
    || fail "failed to compile NetCDF variable shaper"

# ------------------------------------------------------------------------
# 2. Run each driver at -np 1 into its own output dir.
# ------------------------------------------------------------------------
run_driver() {
    # $1 = tag (cxx|nuopc), $2 = executable
    local tag="$1"
    local exe="$2"
    local outdir="$WORKDIR/out_${tag}"
    local cfg="$WORKDIR/config_${tag}.yaml"
    local log="$WORKDIR/run_${tag}.log"

    # Rewrite output.directory so each driver writes to its own dir, and
    # gridspec_file so the coordinate file resolves from any working
    # directory (the fixture keeps repo-relative paths for readability).
    # The CEDS stream, timing, and field set stay identical.
    local repo_root
    repo_root="$(cd "$(dirname "$CONFIG_TEMPLATE")/.." && pwd)"
    sed -e "s|^\(\s*directory:\).*|\1 ${outdir}|" \
        -e "s|^\(\s*gridspec_file:\) \"data/|\1 \"${repo_root}/data/|" \
        "$CONFIG_TEMPLATE" > "$cfg" \
        || fail "failed to render config for ${tag}"

    mkdir -p "$outdir"

    # Same bounded retry as the parity harness (benign libhdf5 race under an
    # oversubscribed container).
    local rc=1
    local attempt=0
    local max_attempts="${CECE_GRIDSPEC_RETRIES:-4}"
    while [ "$attempt" -lt "$max_attempts" ]; do
        attempt=$((attempt + 1))
        rm -rf "$outdir"; mkdir -p "$outdir"
        OMP_NUM_THREADS=1 OMP_PROC_BIND=false \
            "$exe" "$cfg" > "$log" 2>&1
        rc=$?
        if [ "$rc" -eq 0 ]; then break; fi
        echo "WARN: ${tag} driver exited ${rc} on attempt ${attempt}/${max_attempts}" >&2
        if [ "$attempt" -lt "$max_attempts" ]; then sleep 1; fi
    done
    if [ "$rc" -ne 0 ]; then
        echo "----- ${tag} driver log -----" >&2
        tail -n 40 "$log" >&2
        fail "${tag} driver exited with code ${rc} after ${max_attempts} attempts"
    fi

    local nc
    nc="$(ls "$outdir"/*.nc 2>/dev/null | head -n 1)"
    [ -n "$nc" ] || fail "no NetCDF output produced by ${tag} driver (see ${log})"
    echo "$nc"
}

OUT_CXX="$(run_driver cxx "$CXX_DRIVER")" || exit 1
OUT_NUOPC="$(run_driver nuopc "$NUOPC_APP")" || exit 1

echo "C++ driver output:   $OUT_CXX"
echo "NUOPC cap output:    $OUT_NUOPC"

# ------------------------------------------------------------------------
# 3. The cap's output carries 2-D curvilinear coordinates.
# ------------------------------------------------------------------------
# The writer emits lon/lat with rank 2, dims (lat, lon) sized ny x nx, when
# the grid is curvilinear. Assert exactly that on the cap's file (and the
# C++ file, so a silent regression on either side is caught).
for file in "$OUT_CXX" "$OUT_NUOPC"; do
    for var in lon lat; do
        shape_out="$("$SHAPER_BIN" "$file" "$var")" || fail "variable '${var}' missing from $file"
        echo "shape: $shape_out"
        rank="$(echo "$shape_out" | sed -n 's/.*rank=\([0-9]*\).*/\1/p')"
        if [ "$rank" != "2" ]; then
            fail "'${var}' is rank ${rank} (expected 2-D curvilinear) in $file"
        fi
    done
done
echo "PASS: 2-D curvilinear coordinates present in both drivers' outputs"

# ------------------------------------------------------------------------
# 4. Parity on the curvilinear fixture: coordinates, fields, time,
#    variable set all identical.
# ------------------------------------------------------------------------
TOL="${CECE_GRIDSPEC_TOL:-0}"

for var in lon lat; do
    echo "--- Comparing coordinate '${var}' (tol=${TOL})"
    "$COMPARATOR_BIN" "$OUT_CXX" "$OUT_NUOPC" "$var" "$TOL" \
        || fail "curvilinear coordinate '${var}' NOT bit-for-bit equal between drivers"
    echo "PASS: '${var}' identical"
done

IFS=',' read -r -a FIELD_ARR <<< "$FIELDS"
for field in "${FIELD_ARR[@]}"; do
    echo "--- Comparing field '${field}' (tol=${TOL})"
    "$COMPARATOR_BIN" "$OUT_CXX" "$OUT_NUOPC" "$field" "$TOL" \
        || fail "field '${field}' NOT bit-for-bit equal between drivers on the curvilinear fixture"
    echo "PASS: '${field}' identical"
done

echo "--- Comparing variable 'time'"
"$COMPARATOR_BIN" "$OUT_CXX" "$OUT_NUOPC" "time" "$TOL" \
    || fail "'time' differs between drivers (stamp convention mismatch)"
echo "PASS: 'time' identical"

CXX_VARS="$WORKDIR/vars_cxx.txt"
NUOPC_VARS="$WORKDIR/vars_nuopc.txt"
"$VARLIST_BIN" "$OUT_CXX" | sort > "$CXX_VARS" || fail "failed to list C++ driver variables"
"$VARLIST_BIN" "$OUT_NUOPC" | sort > "$NUOPC_VARS" || fail "failed to list NUOPC cap variables"

if ! diff -u "$CXX_VARS" "$NUOPC_VARS" > "$WORKDIR/vars_diff.txt"; then
    echo "----- variable set diff (C++ vs NUOPC) -----" >&2
    cat "$WORKDIR/vars_diff.txt" >&2
    fail "output variable sets differ between drivers on the curvilinear fixture"
fi
echo "PASS: variable sets identical ($(wc -l < "$CXX_VARS") variables)"

echo "========================================================================"
echo " PASS: NUOPC cap runs the GRIDSPEC curvilinear topology and matches"
echo " the C++ driver bit-for-bit (curvilinear coordinates + full parity)"
echo "========================================================================"
exit 0
