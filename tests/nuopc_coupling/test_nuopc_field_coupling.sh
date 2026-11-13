#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) HELM Project Contributors
#
# =====================================================================
# NUOPC field-coupling integration test
# =====================================================================
#   Verifies CECE's export fields reach a coupled consumer correctly.
#
# Runs the two-component coupling app (production CECE cap + minimal sink
# peer connected through a NUOPC Connector) and the C++ standalone driver on
# the SAME config, then asserts:
#
#   1. At -np 1: the sink-received field is BIT-FOR-BIT identical to the
#      standalone driver's output for the same species.
#   2. At -np 2: same equality through the latitude-band decomposition (the
#      sink's realized fields must tile the globe exactly once, band-aligned).
#   3. An export configured but left unconnected (sink skips it) is pruned at
#      realization: the run still succeeds and logs the removal.
#
# The coupling app is the field-dictionary host (the preloaded ESMF dictionary
# lacks the emission standard names), so every app run passes --dictionary.
#
# Modeled on tests/test_driver_nuopc_parity.sh (same launch, retry, and
# comparator idioms). Designed to run INSIDE the cece-dev container with ESMF.
#
# Usage:
#   test_nuopc_field_coupling.sh \
#       --app         <path to cece_nuopc_coupling_test> \
#       --cxx-driver  <path to cece_standalone_driver> \
#       --config      <path to coupled fixture yaml> \
#       --dictionary  <path to field dictionary yaml> \
#       --comparator  <path to nccmp_var.c> \
#       --var         <data field name, e.g. "oc"> \
#       --workdir     <scratch dir (created; kept on failure)>
#
# Env (optional):
#   CECE_CPL_RETRIES  launch attempts per run (default 4)
#   CECE_CPL_KEEP     if 1, keep the scratch workdir on success

set -u

APP=""
NUOPC_APP=""
CXX_DRIVER=""
CONFIG_TEMPLATE=""
DICTIONARY=""
COMPARATOR_SRC=""
VAR=""
WORKDIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --app)        APP="$2"; shift 2 ;;
        --nuopc-app)  NUOPC_APP="$2"; shift 2 ;;
        --cxx-driver) CXX_DRIVER="$2"; shift 2 ;;
        --config)     CONFIG_TEMPLATE="$2"; shift 2 ;;
        --dictionary) DICTIONARY="$2"; shift 2 ;;
        --comparator) COMPARATOR_SRC="$2"; shift 2 ;;
        --var)        VAR="$2"; shift 2 ;;
        --workdir)    WORKDIR="$2"; shift 2 ;;
        *) echo "ERROR: unrecognized argument: $1" >&2; exit 2 ;;
    esac
done

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -n "$APP" ]               || fail "--app is required"
[ -n "$NUOPC_APP" ]         || fail "--nuopc-app is required"
[ -n "$CXX_DRIVER" ]        || fail "--cxx-driver is required"
[ -n "$CONFIG_TEMPLATE" ]   || fail "--config is required"
[ -n "$DICTIONARY" ]        || fail "--dictionary is required"
[ -n "$COMPARATOR_SRC" ]    || fail "--comparator is required"
[ -n "$VAR" ]               || fail "--var is required"
[ -n "$WORKDIR" ]           || fail "--workdir is required"

[ -x "$APP" ]               || fail "coupling app not found or not executable: $APP"
[ -x "$NUOPC_APP" ]         || fail "standalone NUOPC app not found or not executable: $NUOPC_APP"
[ -x "$CXX_DRIVER" ]        || fail "C++ driver not found or not executable: $CXX_DRIVER"
[ -f "$CONFIG_TEMPLATE" ]   || fail "config template not found: $CONFIG_TEMPLATE"
[ -f "$DICTIONARY" ]        || fail "dictionary not found: $DICTIONARY"
[ -f "$COMPARATOR_SRC" ]    || fail "comparator source not found: $COMPARATOR_SRC"

echo "========================================================================"
echo " NUOPC field-coupling checks:"
echo "  - sink-received export == standalone output (np1, bit-for-bit)"
echo "  - sink-received export == standalone output (np2, band-tiled)"
echo "  - configured-but-unconnected export is pruned at realization"
echo "  - host-provided import advertised, realized, copied (np1)"
echo "  - standalone NUOPC app fails without a host dictionary (negative)"
echo "  - standalone NUOPC app with dictionary == uncoupled output"
echo " Fixture: $CONFIG_TEMPLATE"
echo "========================================================================"

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR" || fail "could not create workdir: $WORKDIR"

cleanup() {
    if [ "${CECE_CPL_KEEP:-0}" = "1" ]; then
        echo "CECE_CPL_KEEP=1 -> leaving scratch dir $WORKDIR in place"
    else
        rm -rf "$WORKDIR"
    fi
}
trap cleanup EXIT

# ------------------------------------------------------------------------
# 1. Build the NetCDF comparator against the container NetCDF-C (same idiom
#    as the parity harness).
# ------------------------------------------------------------------------
COMPARATOR_BIN="$WORKDIR/nccmp_var"
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

# ------------------------------------------------------------------------
# Shared launch helpers.
# ------------------------------------------------------------------------
MPIRUN="${MPIEXEC_EXECUTABLE:-mpirun}"
NPFLAG="${MPIEXEC_NUMPROC_FLAG:--np}"
MPI_PREFLAGS=(--allow-run-as-root --oversubscribe)

# run_case <tag> <cwd> <cmd...>
# Runs the full command line inside <cwd> with bounded retries (HDF5/MPI race
# under oversubscription, same as the parity harness). Logs land in
# <cwd>/run.log; ESMF per-PET log files are written into <cwd> too.
run_case() {
    local tag="$1" cwd="$2"
    shift 2
    local log="$cwd/run.log"
    local rc=1 attempt=0
    local max_attempts="${CECE_CPL_RETRIES:-4}"
    mkdir -p "$cwd" || fail "could not create case dir $cwd"
    while [ "$attempt" -lt "$max_attempts" ]; do
        attempt=$((attempt + 1))
        ( cd "$cwd" && OMP_NUM_THREADS=1 OMP_PROC_BIND=false "$@" > "$log" 2>&1 )
        rc=$?
        if [ "$rc" -eq 0 ]; then break; fi
        echo "WARN: ${tag} exited ${rc} on attempt ${attempt}/${max_attempts}" >&2
        if [ "$attempt" -lt "$max_attempts" ]; then sleep 1; fi
    done
    if [ "$rc" -ne 0 ]; then
        echo "----- ${tag} log -----" >&2
        tail -n 60 "$log" >&2
        echo "----- ${tag} dir -----" >&2
        ls -la "$cwd" >&2
        fail "${tag} exited with code ${rc} after ${max_attempts} attempts"
    fi
}

# standalone_out <tag> <np> -> echoes the single NetCDF file the C++ driver wrote
standalone_out() {
    local tag="$1" np="$2"
    local cwd="$WORKDIR/cxx_${tag}"
    local outdir="$cwd/out"
    local cfg="$WORKDIR/config_cxx_${tag}.yaml"
    # Rewrite ONLY output.directory; grid, timing, streams, fields, and the
    # nuopc section stay identical (the section is inert without a peer).
    sed "s|^\(\s*directory:\).*|\1 ${outdir}|" "$CONFIG_TEMPLATE" > "$cfg" \
        || fail "failed to render standalone config for ${tag}"
    mkdir -p "$outdir"

    if [ "$np" -eq 1 ]; then
        run_case "cxx_${tag}" "$cwd" "$CXX_DRIVER" "$cfg"
    else
        run_case "cxx_${tag}" "$cwd" \
            "$MPIRUN" "${MPI_PREFLAGS[@]}" "$NPFLAG" "$np" "$CXX_DRIVER" "$cfg"
    fi

    local nc
    nc="$(ls "$outdir"/*.nc 2>/dev/null | head -n 1)"
    [ -n "$nc" ] || fail "no NetCDF output from standalone driver (${tag})"
    echo "$nc"
}

# coupled_sink_file <tag> <np> <sinkdir> [extra app args...] -> echoes sink file
coupled_sink_file() {
    local tag="$1" np="$2" sinkdir="$3"
    shift 3
    local cwd="$WORKDIR/app_${tag}"
    local cfg="$WORKDIR/config_app_${tag}.yaml"
    # The cap's file writer output is irrelevant here (the sink is the oracle
    # consumer); redirect it into the case dir so nothing escapes the scratch.
    sed "s|^\(\s*directory:\).*|\1 ${cwd}/out_cece|" "$CONFIG_TEMPLATE" > "$cfg" \
        || fail "failed to render coupled config for ${tag}"
    mkdir -p "$sinkdir"

    if [ "$np" -eq 1 ]; then
        run_case "app_${tag}" "$cwd" "$APP" \
            --config "$cfg" --dictionary "$DICTIONARY" --sinkdir "$sinkdir" "$@"
    else
        run_case "app_${tag}" "$cwd" \
            "$MPIRUN" "${MPI_PREFLAGS[@]}" "$NPFLAG" "$np" "$APP" \
            --config "$cfg" --dictionary "$DICTIONARY" --sinkdir "$sinkdir" "$@"
    fi
}

# nuopc_app_run <tag> [extra app args...] -> echoes the single NetCDF file the
# standalone NUOPC cap app wrote. The config is passed positionally (the last
# argument), matching the app's parsing; extra args such as --field-dictionary
# precede it.
nuopc_app_run() {
    local tag="$1"
    shift 1
    local cwd="$WORKDIR/nuopcapp_${tag}"
    local outdir="$cwd/out"
    local cfg="$WORKDIR/config_nuopcapp_${tag}.yaml"
    sed "s|^\(\s*directory:\).*|\1 ${outdir}|" "$CONFIG_TEMPLATE" > "$cfg" \
        || fail "failed to render standalone NUOPC-app config for ${tag}"
    mkdir -p "$outdir"
    run_case "nuopcapp_${tag}" "$cwd" "$NUOPC_APP" "$@" "$cfg"
    local nc
    nc="$(ls "$outdir"/*.nc 2>/dev/null | head -n 1)"
    [ -n "$nc" ] || fail "no NetCDF output from standalone NUOPC app (${tag})"
    echo "$nc"
}

# ------------------------------------------------------------------------
# 2. Scenario 1: connected export at -np 1, bit-for-bit vs standalone.
# ------------------------------------------------------------------------
echo "--- Scenario 1a: connected export at -np 1"
CXX_NC_1="$(standalone_out np1 1)" || exit 1
echo "    standalone output: $CXX_NC_1"
coupled_sink_file np1 1 "$WORKDIR/sink_np1" || exit 1
SINK_NC_1="$WORKDIR/sink_np1/sink_${VAR}_1.nc"
[ -f "$SINK_NC_1" ] || { ls -la "$WORKDIR/sink_np1" >&2; fail "sink produced no ${VAR} file at np1"; }
"$COMPARATOR_BIN" "$CXX_NC_1" "$SINK_NC_1" "$VAR" 0 \
    || fail "sink-received '${VAR}' NOT bit-for-bit equal to standalone (np1)"
echo "PASS: sink '${VAR}' == standalone '${VAR}' at np1"

# ------------------------------------------------------------------------
# 3. Scenario 2: same at -np 2 (band decomposition must tile exactly).
# ------------------------------------------------------------------------
echo "--- Scenario 1b: connected export at -np 2"
CXX_NC_2="$(standalone_out np2 2)" || exit 1
echo "    standalone output: $CXX_NC_2"
coupled_sink_file np2 2 "$WORKDIR/sink_np2" || exit 1
SINK_NC_2="$WORKDIR/sink_np2/sink_${VAR}_1.nc"
[ -f "$SINK_NC_2" ] || { ls -la "$WORKDIR/sink_np2" >&2; fail "sink produced no ${VAR} file at np2"; }
"$COMPARATOR_BIN" "$CXX_NC_2" "$SINK_NC_2" "$VAR" 0 \
    || fail "sink-received '${VAR}' NOT bit-for-bit equal to standalone (np2)"
"$COMPARATOR_BIN" "$CXX_NC_1" "$CXX_NC_2" "$VAR" 0 \
    || fail "standalone '${VAR}' differs between np1 and np2 (fixture unstable)"
echo "PASS: sink '${VAR}' == standalone '${VAR}' at np2"

# ------------------------------------------------------------------------
# 4. Scenario 3: configured-but-unconnected export is pruned at realize.
#    The sink skips the field, so CECE must remove it from exportState and
#    log the removal; the run still completes and nothing is written.
# ------------------------------------------------------------------------
echo "--- Scenario 1c: unconnected export is pruned"
coupled_sink_file unconn 1 "$WORKDIR/sink_unconn" --sink-skip "$VAR" || exit 1
if ls "$WORKDIR/sink_unconn"/*.nc >/dev/null 2>&1; then
    fail "sink wrote files for an export it never advertised"
fi
if ! grep -Rqs "Removed unconnected export field '${VAR}'" "$WORKDIR/app_unconn"; then
    echo "----- app_unconn files -----" >&2
    ls -la "$WORKDIR/app_unconn" >&2
    grep -Rsn "unconnected" "$WORKDIR/app_unconn" >&2 || true
    fail "unconnected-removal INFO line missing from the cap's logs"
fi
echo "PASS: unconnected export pruned at realization"

# ------------------------------------------------------------------------
# 5. Scenario 2 (US2): host-provided import. A constant met-source peer is
#    connected into CECE through a Connector (copy path). The imported field
#    is inert for these OC emissions, so the assertion is that the import
#    plumbing runs end to end WITHOUT error and does not disturb the export:
#      - the coupled-with-met-source run completes,
#      - the cap advertises, realizes, and copies the connected import,
#      - the sink-received export stays bit-for-bit equal to standalone.
# ------------------------------------------------------------------------
echo "--- Scenario 2: host-provided import (met source -> CECE, np1)"
coupled_sink_file met 1 "$WORKDIR/sink_met" --metsource || exit 1
SINK_NC_MET="$WORKDIR/sink_met/sink_${VAR}_1.nc"
[ -f "$SINK_NC_MET" ] || { ls -la "$WORKDIR/sink_met" >&2; fail "met-source run produced no ${VAR} file"; }
# The inert import must not change the emission output.
"$COMPARATOR_BIN" "$CXX_NC_1" "$SINK_NC_MET" "$VAR" 0 \
    || fail "sink '${VAR}' differs with met source active (import disturbed export)"
# The cap must have advertised, realized, and copied the connected import.
if ! grep -Rqs "Advertised .* NUOPC import field" "$WORKDIR/app_met"; then
    echo "----- app_met files -----" >&2
    grep -Rsn "import" "$WORKDIR/app_met" >&2 || true
    fail "cap did not advertise the configured NUOPC import field"
fi
if ! grep -Rqs "Realized import field 'temperature'" "$WORKDIR/app_met"; then
    fail "cap did not realize the connected import field"
fi
if ! grep -Rqs "Copied import field 'temperature'" "$WORKDIR/app_met"; then
    fail "cap did not copy the connected import field into the simulation"
fi
echo "PASS: host-provided import advertised, realized, copied; export undisturbed"

# ------------------------------------------------------------------------
# 6. Scenario 3 (US3, negative): the standalone NUOPC cap app run WITHOUT a
#    host field dictionary must fail, because the preloaded ESMF dictionary
#    lacks the emission/met standard names the coupled config advertises. The
#    failure names the offending StandardName. This proves Advertise validates
#    against the dictionary (a host responsibility), not a cap defect.
# ------------------------------------------------------------------------
echo "--- Scenario 3: standalone NUOPC app without a dictionary fails"
NEG_CFG="$WORKDIR/config_negdict.yaml"
NEG_DIR="$WORKDIR/app_negdict"
sed "s|^\(\s*directory:\).*|\1 ${NEG_DIR}/out|" "$CONFIG_TEMPLATE" > "$NEG_CFG" \
    || fail "failed to render negative config"
mkdir -p "$NEG_DIR"
if ( cd "$NEG_DIR" && OMP_NUM_THREADS=1 OMP_PROC_BIND=false \
        "$NUOPC_APP" "$NEG_CFG" > "$NEG_DIR/run.log" 2>&1 ); then
    echo "----- negative run log -----" >&2
    tail -n 40 "$NEG_DIR/run.log" >&2
    fail "standalone NUOPC app succeeded without a dictionary (expected failure)"
fi
if ! grep -Rqsi "agent_count_emission_flux_of_particulate_organic_matter" "$NEG_DIR"; then
    echo "----- negative run log -----" >&2
    tail -n 40 "$NEG_DIR/run.log" >&2
    grep -Rsn "dictionar\|StandardName\|advertise" "$NEG_DIR" >&2 || true
    fail "negative failure did not name the advertised StandardName"
fi
echo "PASS: standalone NUOPC app rejects unknown standard names without a dictionary"

# ------------------------------------------------------------------------
# 7. Scenario 4 (US3, positive): the same app run WITH the host dictionary
#    succeeds, and its emission output is bit-for-bit identical to the
#    uncoupled C++ driver: adding the nuopc section and loading a dictionary
#    does not change the science (the standalone-unchanged guarantee).
# ------------------------------------------------------------------------
echo "--- Scenario 4: standalone NUOPC app with dictionary == uncoupled"
NUOPC_NC="$(nuopc_app_run dict --field-dictionary "$DICTIONARY")" || exit 1
"$COMPARATOR_BIN" "$CXX_NC_1" "$NUOPC_NC" "$VAR" 0 \
    || fail "standalone NUOPC app '${VAR}' differs from C++ driver (dictionary changed output)"
echo "PASS: standalone NUOPC app with dictionary matches uncoupled output"

echo "========================================================================"
echo " ALL FIELD-COUPLING CHECKS PASSED"
echo "========================================================================"
