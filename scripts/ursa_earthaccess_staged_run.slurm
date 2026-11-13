#!/bin/bash
#SBATCH --job-name=cece-earthaccess-staged
#SBATCH --account=<your-account>
#SBATCH --partition=<your-partition>
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=02:00:00
#SBATCH --output=cece-earthaccess-staged.%j.out
#SBATCH --error=cece-earthaccess-staged.%j.err

set -euo pipefail

# Run from the CECE repository root on an Ursa login node:
#   bash scripts/ursa_earthaccess_staged_run.slurm
# Optional overrides:
#   CECE_CONFIG=examples/cece_config_earthaccess_megan3.yaml \
#   CECE_EARTHACCESS_STAGE_DIR=/scratch/$USER/cece_earthaccess_stage \
#   OVERWRITE_STAGE=1 \
#   bash scripts/ursa_earthaccess_staged_run.slurm

if [[ -z "${SLURM_JOB_ID:-}" ]]; then
  cd "$(dirname "${BASH_SOURCE[0]}")/.."

  module purge
  module use "${PWD}/modulefiles"
  module load cece_ursa.intel

  export CECE_CONFIG="${CECE_CONFIG:-examples/cece_config_earthaccess_megan3.yaml}"
  config_base="$(basename "${CECE_CONFIG}" .yaml)"
  export CECE_EARTHACCESS_STAGE_DIR="${CECE_EARTHACCESS_STAGE_DIR:-${PWD}/earthaccess_stage/${config_base}}"
  export CECE_EARTHACCESS_VENV="${CECE_EARTHACCESS_VENV:-${PWD}/.venv-ursa-earthaccess}"
  export CECE_EARTHACCESS_AUTH_STRATEGY="${CECE_EARTHACCESS_AUTH_STRATEGY:-netrc}"

  if [[ "${CECE_EARTHACCESS_AUTH_STRATEGY}" == "netrc" ]]; then
    unset EARTHDATA_TOKEN
  fi

  python -m venv "${CECE_EARTHACCESS_VENV}"
  source "${CECE_EARTHACCESS_VENV}/bin/activate"
  python -m pip install --upgrade pip setuptools wheel
  python -m pip install -e '.[cloud,test]'

  stage_args=(
    --config "${CECE_CONFIG}"
    --stage-dir "${CECE_EARTHACCESS_STAGE_DIR}"
    --auth-strategy "${CECE_EARTHACCESS_AUTH_STRATEGY}"
  )
  if [[ "${OVERWRITE_STAGE:-0}" == "1" ]]; then
    stage_args+=(--overwrite)
  fi
  python scripts/stage_earthaccess_streams.py "${stage_args[@]}"

  export CECE_EARTHACCESS_STAGE_DIR
  sbatch "$0"
  exit 0
fi

module purge
module use "${SLURM_SUBMIT_DIR}/modulefiles"
module load cece_ursa.intel

cd "${SLURM_SUBMIT_DIR}"

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-1}"
export CECE_CONFIG="${CECE_CONFIG:-examples/cece_config_earthaccess_megan3.yaml}"
export CECE_EARTHACCESS_STAGE_DIR="${CECE_EARTHACCESS_STAGE_DIR:?CECE_EARTHACCESS_STAGE_DIR must point to a staged cache directory}"

unset EARTHDATA_USERNAME
unset EARTHDATA_TOKEN

cmake -S . -B build-ursa-earthaccess \
  -DCMAKE_BUILD_TYPE=Release \
  -DKokkos_ENABLE_SERIAL=ON
cmake --build build-ursa-earthaccess --target cece_standalone_driver -j "${SLURM_CPUS_PER_TASK:-1}"

driver_bin="build-ursa-earthaccess/cece_standalone_driver"
if [[ ! -x "${driver_bin}" ]]; then
  driver_bin="build-ursa-earthaccess/bin/cece_standalone_driver"
fi

srun -n "${SLURM_NTASKS:-1}" "${driver_bin}" "${CECE_CONFIG}"
