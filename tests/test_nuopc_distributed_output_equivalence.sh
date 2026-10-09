#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) HELM Project Contributors
#
# =====================================================================
# NUOPC cap distributed output equivalence integration test
# =====================================================================
#   Verifies that the cap's output is rank-count invariant and
#   matches the C++ driver, and that every configured field is
#   exported through the cap.
#
# Runs the NUOPC cap app (cece_nuopc_app) at -np 1, -np 2, and -np 3 on the
# small CEDS OC config, then asserts:
#
#   1. The written `oc` field is BIT-FOR-BIT identical across rank counts
#      (np1 == np2 == np3): band decomposition does not change the result
#      through the cap, mirroring the C++ driver's own distributed
#      equivalence (tests/test_distributed_output_equivalence.sh).
#   2. The cap's -np 1 output matches the C++ driver's -np 1 output bit-for-
#      bit (the cap replicates the driver, not just itself).
#   3. FIELD COMPLETENESS: every data field configured under
#      `output.fields` in the config appears in the cap's output at EVERY
#      rank count, with none missing; and the full output variable set is
#      identical across np1/np2/np3 (none extra appears at one rank count and
#      not another).
#
# Modeled on tests/test_distributed_output_equivalence.sh (same launch, retry,
# and comparator idioms) and tests/test_driver_nuopc_parity.sh (same cross-
# driver comparison). Designed to run INSIDE the cece-dev container with ESMF
# (ctest launches it via ./setup.sh).
#
# Usage:
#   test_nuopc_distributed_output_equivalence.sh \
#       --cxx-driver  <path to cece_standalone_driver> \
#       --nuopc-app   <path to cece_nuopc_app> \
#       --config      <path to cece_config_ceds_oc_small.yaml template> \
#       --comparator  <path to nccmp_var.c> \
#       --varlist     <path to nclist_vars.c> \
#       --workdir     <scratch dir (created, cleaned up on success)>
#
# Env (optional):
#   CECE_NUOPC_EQUIV_TOL      absolute tolerance (default 0 = bit-for-bit)
#   CECE_NUOPC_EQUIV_RETRIES  launch attempts per rank count (default 4)
#   CECE_NUOPC_EQUIV_KEEP     if 1, keep the scratch workdir on exit

set -u

CXX_DRIVER=""
NUOPC_APP=""
CONFIG_TEMPLATE=""
COMPARATOR_SRC=""
VARLIST_SRC=""
WORKDIR=""
RANKS=(1 2 3)

while [ $# -gt 0 ]; do
    case "$1" in
        --cxx-driver) CXX_DRIVER="$2"; shift 2 ;;
        --nuopc-app)  NUOPC_APP="$2"; shift 2 ;;
        --config)     CONFIG_TEMPLATE="$2"; shift 2 ;;
        --comparator) COMPARATOR_SRC="$2"; shift 2 ;;
        --varlist)    VARLIST_SRC="$2"; shift 2 ;;
        --workdir)    WORKDIR="$2"; shift 2 ;;
        *) echo "ERROR: unrecognized argument: $1" >&2; exit 2 ;;
    esac
done

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -n "$CXX_DRIVER" ]      || fail "--cxx-driver is required"
[ -n "$NUOPC_APP" ]       || fail "--nuopc-app is required"
[ -n "$CONFIG_TEMPLATE" ] || fail "--config is required"
[ -n "$COMPARATOR_SRC" ]  || fail "--comparator is required"
[ -n "$VARLIST_SRC" ]     || fail "--varlist is required"
[ -n "$WORKDIR" ]         || fail "--workdir is required"

[ -x "$CXX_DRIVER" ]      || fail "C++ driver not found or not executable: $CXX_DRIVER"
[ -x "$NUOPC_APP" ]       || fail "NUOPC app not found or not executable: $NUOPC_APP"
[ -f "$CONFIG_TEMPLATE" ] || fail "config template not found: $CONFIG_TEMPLATE"
[ -f "$COMPARATOR_SRC" ]  || fail "comparator source not found: $COMPARATOR_SRC"
[ -f "$VARLIST_SRC" ]     || fail "varlist source not found: $VARLIST_SRC"

echo "========================================================================"
echo " Checks:"
echo "  - cap output rank-count invariant (np1==np2==np3) and equal to"
echo "    the C++ driver at np1"
echo "  - every configured output field present at every rank count"
echo " Fixture: $CONFIG_TEMPLATE"
echo "========================================================================"

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR" || fail "could not create workdir: $WORKDIR"

cleanup() {
    if [ "${CECE_NUOPC_EQUIV_KEEP:-0}" = "1" ]; then
        echo "CECE_NUOPC_EQUIV_KEEP=1 -> leaving scratch dir $WORKDIR in place"
    else
        rm -rf "$WORKDIR"
    fi
}
trap cleanup EXIT

# ------------------------------------------------------------------------
# 0. Derive the configured data-field names from the config's output block.
#    Only `output.fields` uses `- name:` AFTER the `output:` key (the cece_data
#    streams also use `- name:` but live before `output:`), so scoping the awk
#    to the output block isolates the emission fields to assert on.
# ------------------------------------------------------------------------
CONFIGURED_FIELDS="$(
    awk '
        /^output:/ { in_out = 1 }
        in_out && /^[^[:space:]#]/ && !/^output:/ { in_out = 0 }
        in_out && /^[[:space:]]*fields:/ { in_fields = 1; next }
        in_out && in_fields && /^[[:space:]]*-[[:space:]]*name:/ {
            gsub(/"/, "", $3); print $3
        }
    ' "$CONFIG_TEMPLATE"
)"
[ -n "$CONFIGURED_FIELDS" ] || fail "no output.fields found in $CONFIG_TEMPLATE (cannot assert completeness)"
echo "Configured output fields: $(echo "$CONFIGURED_FIELDS" | tr '\n' ' ')"

# ------------------------------------------------------------------------
# 1. Build the NetCDF comparator and variable lister (container NetCDF-C).
# ------------------------------------------------------------------------
COMPARATOR_BIN="$WORKDIR/nccmp_var"
VARLIST_BIN="$WORKDIR/nclist_vars"
CC_BIN="${CC:-cc}"
NC_CONFIG="${NC_CONFIG:-nc-config}"
NC_CFLAGS="$($NC_CONFIG --cflags 2>/dev/null)"
NC_LIBS="$($NC_CONFIG --libs 2>/dev/null)"
[ -n "$NC_LIBS" ] || NC_LIBS="-lnetcdf"

# shellcheck disable=SC2086
"$CC_BIN" "$COMPARATOR_SRC" $NC_CFLAGS -O2 -o "$COMPARATOR_BIN" $NC_LIBS -lm \
    || fail "failed to compile NetCDF comparator"
# shellcheck disable=SC2086
"$CC_BIN" "$VARLIST_SRC" $NC_CFLAGS -O2 -o "$VARLIST_BIN" $NC_LIBS \
    || fail "failed to compile NetCDF variable lister"

# ------------------------------------------------------------------------
# 2. Run a driver at a given rank count into its own output dir.
# ------------------------------------------------------------------------
MPIRUN="${MPIEXEC_EXECUTABLE:-mpirun}"
NPFLAG="${MPIEXEC_NUMPROC_FLAG:--np}"
MPI_PREFLAGS=(--allow-run-as-root --oversubscribe)

# run_one <tag> <exe> <np> -> echoes the produced .nc path
run_one() {
    local tag="$1"
    local exe="$2"
    local np="$3"
    local outdir="$WORKDIR/out_${tag}"
    local cfg="$WORKDIR/config_${tag}.yaml"
    local log="$WORKDIR/run_${tag}.log"

    # Rewrite ONLY output.directory so each run writes to its own dir; the
    # grid, timing, CEDS stream, and field set stay identical.
    sed "s|^\(\s*directory:\).*|\1 ${outdir}|" "$CONFIG_TEMPLATE" > "$cfg" \
        || fail "failed to render config for ${tag}"

    # The CEDS source is read via an HDF5/NetCDF-MPI collective open; under an
    # oversubscribed container this occasionally trips a benign libhdf5 race
    # (see test_distributed_output_equivalence.sh). Bounded retry.
    local rc=1
    local attempt=0
    local max_attempts="${CECE_NUOPC_EQUIV_RETRIES:-4}"
    while [ "$attempt" -lt "$max_attempts" ]; do
        attempt=$((attempt + 1))
        rm -rf "$outdir"; mkdir -p "$outdir"
        if [ "$np" -eq 1 ]; then
            # Single rank: run the binary directly (np1 path == serial run).
            OMP_NUM_THREADS=1 OMP_PROC_BIND=false "$exe" "$cfg" > "$log" 2>&1
        else
            OMP_NUM_THREADS=1 OMP_PROC_BIND=false \
                "$MPIRUN" "${MPI_PREFLAGS[@]}" "$NPFLAG" "$np" "$exe" "$cfg" > "$log" 2>&1
        fi
        rc=$?
        if [ "$rc" -eq 0 ]; then break; fi
        echo "WARN: ${tag} (-np ${np}) exited ${rc} on attempt ${attempt}/${max_attempts}" >&2
        if [ "$attempt" -lt "$max_attempts" ]; then sleep 1; fi
    done
    if [ "$rc" -ne 0 ]; then
        echo "----- ${tag} log (-np ${np}) -----" >&2
        tail -n 40 "$log" >&2
        fail "${tag} exited with code ${rc} at -np ${np} after ${max_attempts} attempts"
    fi

    local nc
    nc="$(ls "$outdir"/*.nc 2>/dev/null | head -n 1)"
    [ -n "$nc" ] || { echo "----- ${tag} log (-np ${np}) -----" >&2; tail -n 40 "$log" >&2; fail "no NetCDF output from ${tag} at -np ${np}"; }
    echo "$nc"
}

OUT_NUOPC_NP1="$(run_one nuopc_np1 "$NUOPC_APP" 1)" || exit 1
OUT_NUOPC_NP2="$(run_one nuopc_np2 "$NUOPC_APP" 2)" || exit 1
OUT_NUOPC_NP3="$(run_one nuopc_np3 "$NUOPC_APP" 3)" || exit 1
OUT_CXX_NP1="$(run_one cxx_np1 "$CXX_DRIVER" 1)" || exit 1

echo "NUOPC np1 output: $OUT_NUOPC_NP1"
echo "NUOPC np2 output: $OUT_NUOPC_NP2"
echo "NUOPC np3 output: $OUT_NUOPC_NP3"
echo "C++   np1 output: $OUT_CXX_NP1"

# ------------------------------------------------------------------------
# 3. Cap output is rank-count invariant, and equals the C++ driver.
#    Every configured field is compared bit-for-bit (tolerance 0).
# ------------------------------------------------------------------------
TOL="${CECE_NUOPC_EQUIV_TOL:-0}"

compare_var() {
    local a="$1" b="$2" var="$3" label="$4"
    "$COMPARATOR_BIN" "$a" "$b" "$var" "$TOL" \
        || fail "${label}: variable '${var}' NOT equivalent (comparator rc=$?)"
    echo "PASS: ${label} '${var}' identical"
}

for field in $CONFIGURED_FIELDS; do
    echo "--- Rank-count invariance for field '${field}' (tol=${TOL})"
    compare_var "$OUT_NUOPC_NP1" "$OUT_NUOPC_NP2" "$field" "np1==np2"
    compare_var "$OUT_NUOPC_NP1" "$OUT_NUOPC_NP3" "$field" "np1==np3"
    compare_var "$OUT_NUOPC_NP2" "$OUT_NUOPC_NP3" "$field" "np2==np3"
done

echo "--- Cross-driver parity at np1 (cap == C++ driver)"
for field in $CONFIGURED_FIELDS; do
    compare_var "$OUT_CXX_NP1" "$OUT_NUOPC_NP1" "$field" "cxx==nuopc(np1)"
done
# The time axis must match too (same stamp convention across ranks/drivers).
compare_var "$OUT_NUOPC_NP1" "$OUT_NUOPC_NP2" "time" "np1==np2"
compare_var "$OUT_NUOPC_NP1" "$OUT_NUOPC_NP3" "time" "np1==np3"
compare_var "$OUT_CXX_NP1" "$OUT_NUOPC_NP1" "time" "cxx==nuopc(np1)"

# ------------------------------------------------------------------------
# 4. Field completeness at every rank count, and an identical
#    variable set across np1/np2/np3 (none missing, none extra).
# ------------------------------------------------------------------------
for np in 1 2 3; do
    eval "nc=\$OUT_NUOPC_NP${np}"
    varfile="$WORKDIR/vars_np${np}.txt"
    "$VARLIST_BIN" "$nc" | sort > "$varfile" || fail "failed to list variables (np${np})"
    for field in $CONFIGURED_FIELDS; do
        grep -qx "$field" "$varfile" \
            || fail "configured field '${field}' MISSING from cap output at -np ${np}"
    done
    echo "PASS: all configured fields present at -np ${np}"
done

if ! diff -u "$WORKDIR/vars_np1.txt" "$WORKDIR/vars_np2.txt" > "$WORKDIR/vars_diff_12.txt"; then
    echo "----- variable set diff (np1 vs np2) -----" >&2
    cat "$WORKDIR/vars_diff_12.txt" >&2
    fail "cap output variable set differs between np1 and np2"
fi
if ! diff -u "$WORKDIR/vars_np1.txt" "$WORKDIR/vars_np3.txt" > "$WORKDIR/vars_diff_13.txt"; then
    echo "----- variable set diff (np1 vs np3) -----" >&2
    cat "$WORKDIR/vars_diff_13.txt" >&2
    fail "cap output variable set differs between np1 and np3"
fi
echo "PASS: cap variable set identical across np1/np2/np3 ($(wc -l < "$WORKDIR/vars_np1.txt") variables)"

echo "========================================================================"
echo " PASS: NUOPC cap output is bit-for-bit rank-count invariant (np1==np2==np3)"
echo "       and matches the C++ driver at np1; every configured"
echo "       output field is present at each rank count."
echo "========================================================================"
exit 0
