// Minimal Kokkos profiling tool (KokkosP callbacks) — hand-written, no network.
//
// Attaches via KOKKOS_TOOLS_LIBS=/path/to/libkp_kernel_timer.so and prints, at
// Kokkos finalize, a table of the most expensive Kokkos "regions" — parallel_for
// / parallel_reduce / parallel_scan kernels and deep_copy operations — ranked by
// total time, so we can see which named kernels/copies dominate the CECE
// pipeline. This is the right tool for a Kokkos+OpenMP workload where valgrind
// would serialize threads and mismeasure memory-bound kernels.
//
// It implements only the subset of the KokkosP interface needed for wall-time
// attribution: init/finalize, begin/end for the three parallel dispatch kinds,
// and begin/end deep_copy. Each begin pushes a timer; the matching end pops it
// and accumulates (count, total_ns, max_ns) keyed by the kernel/copy name.
//
// Build (inside cece-dev container):
//   g++ -O2 -std=c++17 -fPIC -shared tests/tools/kp_kernel_timer.cpp -o build/libkp_kernel_timer.so

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Stat {
    uint64_t count = 0;
    double total_ns = 0.0;
    double max_ns = 0.0;
};

struct Frame {
    std::string name;
    Clock::time_point t0;
};

std::map<std::string, Stat> g_stats;
std::vector<Frame> g_stack;

void push(const std::string& name) {
    g_stack.push_back({name, Clock::now()});
}

void pop() {
    if (g_stack.empty()) return;
    const auto now = Clock::now();
    const Frame f = g_stack.back();
    g_stack.pop_back();
    const double ns = std::chrono::duration<double, std::nano>(now - f.t0).count();
    Stat& s = g_stats[f.name];
    s.count += 1;
    s.total_ns += ns;
    if (ns > s.max_ns) s.max_ns = ns;
}

}  // namespace

extern "C" {

void kokkosp_init_library(const int /*loadseq*/, const uint64_t /*version*/, const uint32_t /*ndevinfos*/, void* /*devinfos*/) {
    std::fprintf(stderr, "[kp_kernel_timer] attached\n");
}

void kokkosp_finalize_library() {
    // Rank-tagged so multi-rank runs interleave readably.
    std::fprintf(stderr, "\n[kp_kernel_timer] ===== Kokkos region timing (by total time) =====\n");
    std::fprintf(stderr, "%-52s %8s %14s %12s %12s\n", "name", "count", "total_ms", "avg_us", "max_us");
    std::vector<std::pair<std::string, Stat>> rows(g_stats.begin(), g_stats.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.total_ns > b.second.total_ns; });
    for (const auto& [name, s] : rows) {
        const double total_ms = s.total_ns / 1.0e6;
        const double avg_us = (s.count ? s.total_ns / s.count : 0.0) / 1.0e3;
        const double max_us = s.max_ns / 1.0e3;
        std::fprintf(stderr, "%-52s %8llu %14.4f %12.3f %12.3f\n", name.c_str(), static_cast<unsigned long long>(s.count), total_ms, avg_us, max_us);
    }
    std::fprintf(stderr, "[kp_kernel_timer] ================================================\n");
}

void kokkosp_begin_parallel_for(const char* name, const uint32_t /*devid*/, uint64_t* kernid) {
    *kernid = 0;
    push(std::string("[for] ") + (name ? name : "anon"));
}
void kokkosp_end_parallel_for(const uint64_t /*kernid*/) {
    pop();
}

void kokkosp_begin_parallel_reduce(const char* name, const uint32_t /*devid*/, uint64_t* kernid) {
    *kernid = 0;
    push(std::string("[reduce] ") + (name ? name : "anon"));
}
void kokkosp_end_parallel_reduce(const uint64_t /*kernid*/) {
    pop();
}

void kokkosp_begin_parallel_scan(const char* name, const uint32_t /*devid*/, uint64_t* kernid) {
    *kernid = 0;
    push(std::string("[scan] ") + (name ? name : "anon"));
}
void kokkosp_end_parallel_scan(const uint64_t /*kernid*/) {
    pop();
}

void kokkosp_begin_deep_copy(const uint32_t /*dst_space*/, const char* dst_name, const void* /*dst_ptr*/, const uint32_t /*src_space*/,
                             const char* src_name, const void* /*src_ptr*/, const uint64_t /*size*/) {
    push(std::string("[deep_copy] ") + (dst_name ? dst_name : "?") + " <- " + (src_name ? src_name : "?"));
}
void kokkosp_end_deep_copy() {
    pop();
}

// Region markers (Kokkos::Profiling::pushRegion / popRegion).
void kokkosp_push_profile_region(const char* name) {
    push(std::string("[region] ") + (name ? name : "anon"));
}
void kokkosp_pop_profile_region() {
    pop();
}

}  // extern "C"
