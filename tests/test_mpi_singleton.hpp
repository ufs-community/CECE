#ifndef CECE_TEST_MPI_SINGLETON_HPP
#define CECE_TEST_MPI_SINGLETON_HPP

#include <cstdlib>

namespace cece::test {

// Force MPI into standalone (singleton) initialization by removing every
// Slurm/PMI process-management variable a launcher may have exported into the
// environment. Test binaries are launched by ctest — not by srun/mpiexec — so
// they must never speak PMI: a PARTIAL environment is the worst case (PMI_FD
// present but PMI_RANK scrubbed makes the MPI runtime open the inherited PMI
// socket and send a malformed handshake — "pmirank missing in fullinit
// command" under `srun ... ctest` on Ursa). Call at the top of main(), before
// MPI_Init/InitGoogleTest.
inline void force_mpi_singleton() {
    // PMI1/PMI2 client plumbing. PMI_FD is the critical one: its presence
    // alone routes MPI_Init through the PMI client.
    unsetenv("PMI_FD");
    unsetenv("PMI_JOBID");
    unsetenv("PMI_APPNUM");
    unsetenv("PMI_RANK");
    unsetenv("PMI_SIZE");
    unsetenv("PMI_SPAWNED");
    // PMIx equivalents (slurm --mpi=pmix)
    unsetenv("PMIX_RANK");
    unsetenv("PMIX_NAMESPACE");
    unsetenv("PMIX_SERVER_URI");
    // Slurm step identity, used by MPI runtimes to detect a managed launch
    unsetenv("SLURM_JOB_ID");
    unsetenv("SLURM_JOBID");
    unsetenv("SLURM_STEP_ID");
    unsetenv("SLURM_STEPID");
    unsetenv("SLURM_PROCID");
    unsetenv("SLURM_LOCALID");
    unsetenv("SLURM_NODEID");
    unsetenv("SLURM_NTASKS");
    unsetenv("SLURM_NPROCS");
    unsetenv("I_MPI_PMI_LIBRARY");
    // Keep Intel MPI local-only on login nodes (prevent PMI2/Hydra aborts)
    setenv("I_MPI_HYDRA_BOOTSTRAP", "none", 0);
    setenv("I_MPI_SHM", "disable", 0);
}

}  // namespace cece::test

#endif  // CECE_TEST_MPI_SINGLETON_HPP
