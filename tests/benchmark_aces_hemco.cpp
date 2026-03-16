/**
 * @file benchmark_aces_hemco.cpp
 * @brief Performance benchmark comparing ACES StackingEngine to a HEMCO-equivalent baseline.
 *
 * Measures wall-clock time for emission stacking over a configurable grid with
 * multiple species and layers. Outputs timing in a machine-parseable format:
 *   ACES_TIME_S: <seconds>
 *
 * Usage:
 *   ./benchmark_aces_hemco --mode cpu  <nx> <ny> <nz> <nspecies> <nlayers> <iters>
 *   ./benchmark_aces_hemco --mode gpu  <nx> <ny> <nz> <nspecies> <nlayers> <iters>
 *
 * Requirements: 3.12, 3.13
 */

#include <Kokkos_Core.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_state.hpp"

namespace {

using Clock = std::chrono::high_resolution_clock;

/** Build a synthetic AcesConfig with nspecies species, each with nlayers. */
aces::AcesConfig BuildSyntheticConfig(int nspecies, int nlayers) {
    aces::AcesConfig cfg;
    for (int s = 0; s < nspecies; ++s) {
        std::string sp = "species_" + std::to_string(s);
        std::vector<aces::EmissionLayer> layers;
        for (int l = 0; l < nlayers; ++l) {
            aces::EmissionLayer lay;
            lay.field_name = "field_" + std::to_string(s) + "_" + std::to_string(l);
            lay.operation = (l == 0) ? "add" : "add";
            lay.hierarchy = l + 1;
            lay.scale = 1.0;
            layers.push_back(lay);
        }
        cfg.species_layers[sp] = layers;
    }
    return cfg;
}

/** Populate import/export states with synthetic fields. */
void PopulateStates(const aces::AcesConfig& cfg, aces::AcesImportState& imp,
                    aces::AcesExportState& exp, int nx, int ny, int nz) {
    for (const auto& [sp, layers] : cfg.species_layers) {
        exp.fields[sp] = aces::DualView3D("exp_" + sp, nx, ny, nz);
        Kokkos::deep_copy(exp.fields[sp].view_host(), 0.0);
        exp.fields[sp].modify<Kokkos::HostSpace>();
        exp.fields[sp].sync<Kokkos::DefaultExecutionSpace>();

        for (const auto& lay : layers) {
            imp.fields[lay.field_name] =
                aces::DualView3D("imp_" + lay.field_name, nx, ny, 1);
            Kokkos::deep_copy(imp.fields[lay.field_name].view_host(), 1.0e-9);
            imp.fields[lay.field_name].modify<Kokkos::HostSpace>();
            imp.fields[lay.field_name].sync<Kokkos::DefaultExecutionSpace>();
        }
    }
}

double RunAcesBenchmark(int nx, int ny, int nz, int nspecies, int nlayers, int iters) {
    auto cfg = BuildSyntheticConfig(nspecies, nlayers);
    aces::AcesImportState imp;
    aces::AcesExportState exp;
    PopulateStates(cfg, imp, exp, nx, ny, nz);

    std::unordered_map<std::string, std::string> empty;
    aces::AcesStateResolver resolver(imp, exp, empty, empty, empty);
    aces::StackingEngine engine(cfg);

    // Warm-up
    engine.Execute(resolver, nx, ny, nz, {}, 0, 0, 0);
    Kokkos::fence();

    auto t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
        engine.ResetBindings();
        engine.Execute(resolver, nx, ny, nz, {}, i % 24, i % 7, i % 12);
        Kokkos::fence();
    }
    auto t1 = Clock::now();

    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    return elapsed / iters;
}

}  // namespace

int main(int argc, char** argv) {
    // Parse arguments
    std::string mode = "cpu";
    int nx = 360, ny = 180, nz = 72, nspecies = 10, nlayers = 5, iters = 10;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (std::isdigit(arg[0]) || (arg[0] == '-' && arg.size() > 1)) {
            // Positional: nx ny nz nspecies nlayers iters
            static int pos = 0;
            int val = std::atoi(arg.c_str());
            switch (pos++) {
                case 0: nx = val; break;
                case 1: ny = val; break;
                case 2: nz = val; break;
                case 3: nspecies = val; break;
                case 4: nlayers = val; break;
                case 5: iters = val; break;
            }
        }
    }

    Kokkos::initialize(argc, argv);
    {
        std::cout << "[Benchmark] Mode=" << mode
                  << " Grid=" << nx << "x" << ny << "x" << nz
                  << " Species=" << nspecies << " Layers=" << nlayers
                  << " Iters=" << iters << "\n";

        double avg_s = RunAcesBenchmark(nx, ny, nz, nspecies, nlayers, iters);

        std::cout << "[Benchmark] Average time per iteration: " << avg_s << "s\n";
        // Machine-parseable output for benchmark_hemco_vs_aces.py
        std::cout << "ACES_TIME_S: " << avg_s << "\n";

        // Throughput
        double cells_per_s = static_cast<double>(nx) * ny * nz * nspecies / avg_s;
        std::cout << "[Benchmark] Throughput: " << cells_per_s / 1e6 << " M cell-species/s\n";
    }
    Kokkos::finalize();
    return 0;
}
