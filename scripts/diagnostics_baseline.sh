#!/bin/bash

##############################################################################
# Script: diagnostics_baseline.sh
#
# Purpose: Capture the SC-005 regression baseline for feature 003 (scheme
#          diagnostics export). Runs the standalone driver on an
#          emissions-only configuration (no `output.include_diagnostics`)
#          and records a SHA-256 of the *content of every NetCDF variable*
#          into specs/003-scheme-diagnostics-export/baseline/checksums.txt.
#
#          After the diagnostics feature lands, re-running this script and
#          diffing against the committed baseline must show NO change to the
#          emission variables — proving that turning diagnostics off leaves
#          output behavior identical (FR-010, SC-005).
#
# Why per-variable hashing instead of whole-file hashing:
#          NetCDF-4/HDF5 files embed a nondeterministic superblock (root
#          object UUID + modification time), so two runs with identical
#          scientific output produce byte-different files. Whole-file SHA-256
#          is therefore useless as a regression signal (verified: three clean
#          runs of the same config gave three different file hashes). The
#          stable, meaningful signal is the content of each variable, which
#          this script extracts via the netCDF C API and hashes.
#
# Usage: ./scripts/diagnostics_baseline.sh [config.yaml]
#
# Arguments:
#   config.yaml  Optional emissions-only config. Default:
#                examples/cece_config_ex3.yaml — a minimal 2x2 grid that uses
#                the locally-available /work/data/MACCity_4x5.nc and declares
#                NO diagnostics, so it exercises the pure species path.
#                (examples/cece_config_ex1.yaml is the spec's nominal choice
#                but its HTAP/CAMS streams live on /gpfs remote storage that
#                is not reachable in the local container; ex3 is the
#                equivalent emissions-only config that actually runs here.)
#
# Requirements:
#   - Run inside the CECE dev container (netCDF C dev headers + libnetcdf).
#   - A built driver binary. Set CECE_DRIVER to override the path; defaults
#     to the first found of: ./build/bin/cece_standalone_driver,
#     ./build-cece-dev/cece_standalone_driver.
#
# Output:
#   - specs/003-scheme-diagnostics-export/baseline/checksums.txt
#     (one "<sha256>  <variable_name>" line per variable, sorted by name)
#
##############################################################################

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

CONFIG="${1:-examples/cece_config_ex3.yaml}"
BASELINE_DIR="specs/003-scheme-diagnostics-export/baseline"
CHECKSUMS="${BASELINE_DIR}/checksums.txt"

# Locate the driver binary.
DRIVER="${CECE_DRIVER:-}"
if [[ -z "${DRIVER}" ]]; then
    for candidate in ./build/bin/cece_standalone_driver ./build-cece-dev/cece_standalone_driver; do
        if [[ -x "${candidate}" ]]; then DRIVER="${candidate}"; break; fi
    done
fi
if [[ -z "${DRIVER}" || ! -x "${DRIVER}" ]]; then
    echo "ERROR: cece_standalone_driver not found. Build it or set CECE_DRIVER." >&2
    exit 1
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "${WORKDIR}"' EXIT
OUTDIR="${WORKDIR}/output"
mkdir -p "${OUTDIR}"

# Rewrite the output directory to a scratch location so the baseline run never
# touches ./cece_output and is reproducible from a clean state.
RENDERED="${WORKDIR}/config.yaml"
sed "s#^\( *directory:\).*#\1 ${OUTDIR}#" "${CONFIG}" > "${RENDERED}"

echo "Driver:   ${DRIVER}"
echo "Config:   ${CONFIG}"
echo "Capturing emissions-only run ..."
# The standalone driver has a pre-existing intermittent AMIO crash during
# AdvanceTime (observed in the local arm64 dev container; it aborts before
# writing any output on some runs). The crash is transient and unrelated to
# this feature, so we retry from a clean scratch dir until output is produced.
# Each attempt starts from an empty OUTDIR, so a partial file never leaks in.
MAX_ATTEMPTS="${CECE_BASELINE_ATTEMPTS:-8}"
captured=0
for attempt in $(seq 1 "${MAX_ATTEMPTS}"); do
    rm -rf "${OUTDIR}"; mkdir -p "${OUTDIR}"
    "${DRIVER}" "${RENDERED}" >"${WORKDIR}/driver.log" 2>&1 || true
    if compgen -G "${OUTDIR}/*.nc" >/dev/null; then
        captured=1
        echo "Captured output on attempt ${attempt}."
        break
    fi
    echo "Attempt ${attempt} produced no output (transient driver crash); retrying ..." >&2
done
if [[ "${captured}" -ne 1 ]]; then
    echo "ERROR: driver produced no NetCDF output after ${MAX_ATTEMPTS} attempts." >&2
    tail -20 "${WORKDIR}/driver.log" >&2
    exit 1
fi

# Per-variable content hasher (netCDF C API + OpenSSL SHA-256).
cat > "${WORKDIR}/varhash.c" <<'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <netcdf.h>
#include <openssl/sha.h>
static void tohex(const unsigned char *d, char *o) {
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) sprintf(o + 2 * i, "%02x", d[i]);
}
int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        int ncid;
        if (nc_open(argv[a], NC_NOWRITE, &ncid)) { fprintf(stderr, "open fail %s\n", argv[a]); continue; }
        int nvars;
        nc_inq_nvars(ncid, &nvars);
        for (int v = 0; v < nvars; v++) {
            char name[NC_MAX_NAME + 1];
            int ndims;
            if (nc_inq_varname(ncid, v, name)) continue;
            if (nc_inq_varndims(ncid, v, &ndims) || ndims == 0) continue;
            size_t n = 1;
            int dims[NC_MAX_VAR_DIMS];
            nc_inq_vardimid(ncid, v, dims);
            for (int d = 0; d < ndims && d < NC_MAX_VAR_DIMS; d++) {
                size_t dl;
                nc_inq_dimlen(ncid, dims[d], &dl);
                n *= dl;
            }
            double *buf = (double *)malloc(n * sizeof(double));
            if (nc_get_var_double(ncid, v, buf) == 0) {
                SHA256_CTX c;
                SHA256_Init(&c);
                SHA256_Update(&c, buf, n * sizeof(double));
                unsigned char m[SHA256_DIGEST_LENGTH];
                SHA256_Final(m, &c);
                char h[65];
                tohex(m, h);
                printf("%s  %s\n", h, name);
            }
            free(buf);
        }
        nc_close(ncid);
    }
    return 0;
}
CEOF

cc -w "${WORKDIR}/varhash.c" -o "${WORKDIR}/varhash" -lnetcdf -lcrypto -lcrypto 2>"${WORKDIR}/cc.log" || {
    echo "ERROR: failed to compile varhash helper." >&2
    cat "${WORKDIR}/cc.log" >&2
    exit 1
}

mkdir -p "${BASELINE_DIR}"
# Sort by variable name (field 2) so ordering is stable across runs.
"${WORKDIR}/varhash" "${OUTDIR}"/*.nc | sort -k2 > "${CHECKSUMS}"

echo "Wrote baseline checksums -> ${CHECKSUMS}"
echo "-----------------------------------------------------"
cat "${CHECKSUMS}"
