// PMPI interposition: time every MPI_Allgatherv (and its datatype cost).
//
// Link this TU into an MPI program to transparently measure the wall time and
// call count spent inside MPI_Allgatherv, WITHOUT touching the code that issues
// the collective. This isolates the collective cost of the develop per-level
// loop (many contiguous Allgatherv) from the optimize single strided-datatype
// Allgatherv, so we can attribute the observed regression to the MPI transfer
// itself vs the surrounding work.
//
// The accumulators are process-local; the harness reads/reset them between
// timed phases via the C hooks below.

#include <mpi.h>

#include <atomic>
#include <chrono>

namespace {
std::atomic<unsigned long long> g_calls{0};
std::atomic<double> g_total_ns{0.0};

void add_ns(double ns) {
    // relaxed accumulate (single-threaded MPI calls in this harness)
    double cur = g_total_ns.load(std::memory_order_relaxed);
    g_total_ns.store(cur + ns, std::memory_order_relaxed);
}
}  // namespace

extern "C" {

int MPI_Allgatherv(const void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, const int* recvcounts, const int* displs,
                   MPI_Datatype recvtype, MPI_Comm comm) {
    const auto t0 = std::chrono::steady_clock::now();
    const int rc = PMPI_Allgatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm);
    const auto t1 = std::chrono::steady_clock::now();
    g_calls.fetch_add(1, std::memory_order_relaxed);
    add_ns(std::chrono::duration<double, std::nano>(t1 - t0).count());
    return rc;
}

// Harness hooks (C linkage).
void pmpi_allgatherv_reset() {
    g_calls.store(0, std::memory_order_relaxed);
    g_total_ns.store(0.0, std::memory_order_relaxed);
}
unsigned long long pmpi_allgatherv_calls() {
    return g_calls.load(std::memory_order_relaxed);
}
double pmpi_allgatherv_total_ms() {
    return g_total_ns.load(std::memory_order_relaxed) / 1.0e6;
}

}  // extern "C"
