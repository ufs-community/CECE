# Running the CECE test suite on Slurm

`scripts/run-tests-slurm.sbatch` runs the registered ctest suite on an
HPC Slurm system with proper rank placement: ctest runs on a compute
node inside one allocation and every MPI test launches as its own
right-sized `srun` job step. For container and local runs see the
top-level README.

## Launch profiles

The test suite launches MPI tests through the CMake FindMPI
variables, so the launcher is chosen once, at configure time:

| Profile | Configure with | Launcher |
|---|---|---|
| Container / local | `-DCECE_MPIEXEC_CONTAINER_FLAGS=ON` (container only) | `mpiexec`, ranks forked locally |
| HPC Slurm, OpenMPI / cray-mpich | `-DMPIEXEC_EXECUTABLE=$(command -v srun) -DMPIEXEC_NUMPROC_FLAG=-n` | `srun`, ranks placed by Slurm |
| HPC Slurm, Intel MPI | (defaults) | Intel `mpiexec`, ranks forked inside the sbatch allocation |

One CMake source of truth — no platform conditionals; the profiles
differ only in configure flags.

## HPC Slurm

`run-tests-slurm.sbatch` carries only suite-owned topology
(`--nodes=1 --ntasks=4 --cpus-per-task=2 --time=00:10:00` — ntasks is
the suite's largest per-test rank count). Everything site-specific
(`--account`, `--partition`, `--clusters`, `--qos`, `--output`) goes
on the `sbatch` command line, which overrides the script's
directives. Load your compiler/MPI environment **before** submitting;
sbatch's default `--export=ALL` carries it into the job. Inside the
allocation, ctest runs each test as its own right-sized `srun` job
step (`-n 1`, `-n 2`, or `-n 4`).

### Intel MPI: use `mpiexec`, not `srun`

With Intel MPI leave the launcher at CMake's default (Intel's `mpiexec`):
do **not** pass `-DMPIEXEC_EXECUTABLE=srun`. Hydra bootstraps through the
allocation on its own and forks each test's ranks inside it; the script's
`--ntasks` bounds them. Under `srun`, Intel MPI has no Slurm PMI client and
`MPI_Init` aborts with `PMI_Init returned 14` (slurmd:
`pmirank missing in fullinit command`); making that work needs the site's
`libpmi2.so` via `I_MPI_PMI_LIBRARY` plus `-DMPIEXEC_PREFLAGS=--mpi=pmi2`,
which is not worth it for a single-node test allocation.

### Example: Ursa

```bash
# gcc / OpenMPI: srun places every test in its own job step
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMPIEXEC_EXECUTABLE=$(command -v srun) -DMPIEXEC_NUMPROC_FLAG=-n
# oneapi / Intel MPI: keep the default launcher (Intel's mpiexec)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
sbatch --wait --account=epic --output=cece-tests-%j.log \
  scripts/run-tests-slurm.sbatch build
```

### Example: Gaea-C6

Same build; only the submit flags differ:

```bash
sbatch --wait --clusters=c6 --partition=batch --account=bil-fire3 \
  --output=cece-tests-%j.log scripts/run-tests-slurm.sbatch build
```

The `--account`/`--partition` values above are site-policy examples,
not requirements baked into the script — substitute your own
allocation.

### Variants

The first argument is the test build directory (the tree holding
`CTestTestfile.cmake`); everything after it passes straight through to
ctest:

```bash
# Asynchronous submit: drop --wait, then watch the log / job.
sbatch --account=epic --output=cece-tests-%j.log \
  scripts/run-tests-slurm.sbatch build
tail -f cece-tests-<jobid>.log        # or: sacct -j <jobid>

# Subset by name regex (the multi-rank tests):
sbatch --wait --account=epic --output=cece-tests-%j.log \
  scripts/run-tests-slurm.sbatch build -R '_np[24]$'

# Subset by rank-count label (np1, np2, np4):
sbatch --wait --account=epic --output=cece-tests-%j.log \
  scripts/run-tests-slurm.sbatch build -L np2

# Packed execution: up to 4 concurrent job steps. Safe because every
# test declares PROCESSORS, so ctest's -j budget counts ranks.
sbatch --wait --account=epic --output=cece-tests-%j.log \
  scripts/run-tests-slurm.sbatch build -j 4
```
