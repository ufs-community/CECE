#!/usr/bin/env bash
# Run the CECE test suite on a Slurm system. See scripts/run-tests-slurm.README.md.
#
# Usage:
#   sbatch [--wait] <site flags> scripts/run-tests-slurm.sbatch <test-build-dir> [ctest args...]
#
# The directives below carry only suite-owned topology: --ntasks is the
# suite's largest per-test rank count (the _np4 tests), so every
# srun-launched test fits in the allocation. Site policy (--account,
# --partition, --clusters, --qos, --output, time overrides) belongs on
# the sbatch command line, which overrides these directives. The
# compiler/MPI environment must be loaded in the submitting shell;
# sbatch's default --export=ALL carries it into the job.
#
# The build must be configured for srun launch:
#   -DMPIEXEC_EXECUTABLE=$(command -v srun) -DMPIEXEC_NUMPROC_FLAG=-n
# Each test then runs as its own right-sized job step inside this
# allocation.
#
#SBATCH --job-name=cece-tests
#SBATCH --nodes=1
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=2
#SBATCH --time=00:10:00

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: sbatch [--wait] <site flags> $0 <test-build-dir> [ctest args...]" >&2
  exit 2
fi

test_build_dir="$1"
shift

exec ctest --test-dir "${test_build_dir}" --output-on-failure "$@"
