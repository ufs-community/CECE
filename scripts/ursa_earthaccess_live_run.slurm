#!/bin/bash
#SBATCH --job-name=cece-earthaccess-live
#SBATCH --account=naqfc
#SBATCH --partition=u1-service
#SBATCH --qos=windfall
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=64
#SBATCH --mem=240G
#SBATCH --time=06:00:00
#SBATCH --output=cece-earthaccess-live.%j.out
#SBATCH --error=cece-earthaccess-live.%j.err

set -euo pipefail

# Submit from the CECE repository root after preparing the cloud environment
# and building cece_standalone_driver:
#   sbatch scripts/ursa_earthaccess_live_run.slurm
#
# Check peak memory after the job finishes:
#   sacct -j <job-id> --format=JobID,State,Elapsed,ReqMem,MaxRSS,ExitCode

cd "${SLURM_SUBMIT_DIR}"

module purge
module use "${SLURM_SUBMIT_DIR}/modulefiles"
module load cece_ursa.intel

earthaccess_venv="${CECE_EARTHACCESS_VENV:-${SLURM_SUBMIT_DIR}/.venv-ursa-earthaccess}"
if [[ ! -x "${earthaccess_venv}/bin/python" ]]; then
  echo "EarthAccess environment not found: ${earthaccess_venv}" >&2
  echo "Create it with: python -m venv ${earthaccess_venv}" >&2
  echo "Then install dependencies with: python -m pip install -e '.[cloud]'" >&2
  exit 2
fi
source "${earthaccess_venv}/bin/activate"

driver_bin="${CECE_DRIVER_BIN:-${SLURM_SUBMIT_DIR}/build-ursa-earthaccess/cece_standalone_driver}"
if [[ ! -x "${driver_bin}" ]]; then
  driver_bin="${SLURM_SUBMIT_DIR}/build-ursa-earthaccess/bin/cece_standalone_driver"
fi
if [[ ! -x "${driver_bin}" ]]; then
  echo "CECE standalone driver not found under build-ursa-earthaccess" >&2
  exit 2
fi

application_threads=8
export OMP_NUM_THREADS="${application_threads}"
export OMP_PROC_BIND=spread
export OMP_PLACES=cores
export MALLOC_ARENA_MAX=2

export CECE_EARTHACCESS_AUTH_STRATEGY="${CECE_EARTHACCESS_AUTH_STRATEGY:-netrc}"
export CECE_EARTHACCESS_STREAM_ATTEMPTS="${CECE_EARTHACCESS_STREAM_ATTEMPTS:-6}"
export CECE_EARTHACCESS_RETRY_DELAY_SECONDS="${CECE_EARTHACCESS_RETRY_DELAY_SECONDS:-5}"

if [[ "${CECE_EARTHACCESS_AUTH_STRATEGY}" == "netrc" ]]; then
  unset EARTHDATA_TOKEN
fi

config="${CECE_CONFIG:-examples/cece_config_earthaccess_megan3.yaml}"

echo "Starting CECE direct EarthAccess stream"
echo "Host: $(hostname)"
echo "Allocated CPUs: ${SLURM_JOB_CPUS_PER_NODE:-unknown}"
echo "Application threads: ${application_threads}; memory: ${SLURM_MEM_PER_NODE:-unknown} MiB"
echo "Config: ${config}"

# Ursa charges enough CPUs to satisfy the requested memory even though CECE uses
# only eight application threads. The 64-CPU request keeps Slurm's CPU variables
# consistent for launchers used by linked runtime libraries.
"${driver_bin}" "${config}"
