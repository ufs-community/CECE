#include <iostream>
#include <vector>

#include "ESMC.h"
#include "aces/aces_diagnostics.hpp"
#include "aces/aces_utils.hpp"

namespace aces {

DualView3D AcesDiagnosticManager::RegisterDiagnostic(const std::string& name, int nx, int ny,
                                                     int nz) {
    auto it = diagnostics_.find(name);
    if (it == diagnostics_.end()) {
        diagnostics_[name] = DualView3D("diag_" + name, nx, ny, nz);
        return diagnostics_[name];
    }
    return it->second;
}

/**
 * @brief Internal helper to write a field to NetCDF.
 */
static void WriteField(ESMC_Field field, const std::string& name) {
    ESMC_FieldWrite(field, (name + ".nc").c_str(), name.c_str(), 1, ESMC_FILESTATUS_REPLACE, 1,
                    ESMF_IOFMT_NETCDF);
}

void AcesDiagnosticManager::WriteDiagnostics(const DiagnosticConfig& config, ESMC_Clock clock,
                                             ESMC_Field template_field) {
    if (config.variables.empty()) return;

    // 1. Clock Check
    if (clock.ptr != nullptr && config.output_interval_seconds > 0) {
        ESMC_TimeInterval currSimTime;
        ESMC_I8 stepCount;
        // Correct signature for ESMF 8.8.0
        ESMC_ClockGet(clock, &currSimTime, &stepCount);

        ESMC_I8 seconds_i8;
        ESMC_TimeIntervalGet(currSimTime, &seconds_i8, NULL);
        long long seconds = (long long)seconds_i8;

        if (seconds % config.output_interval_seconds != 0) return;
    }

    // 2. Grid/Mesh Setup (Cached)
    if (config.grid_type == "gaussian" && cached_grid_.ptr == nullptr) {
        int counts[2] = {config.nx, config.ny};
        ESMC_InterArrayInt iCounts;
        ESMC_InterArrayIntSet(&iCounts, counts, 2);
        int rc;
        cached_grid_ = ESMC_GridCreateNoPeriDim(&iCounts, NULL, NULL, NULL, &rc);
    } else if (config.grid_type == "mesh" &&
               (cached_mesh_.ptr == nullptr || cached_mesh_file_ != config.grid_file)) {
        int rc;
        cached_mesh_ = ESMC_MeshCreateFromFile(config.grid_file.c_str(), ESMC_FILEFORMAT_SCRIP,
                                               NULL, NULL, NULL, NULL, NULL, &rc);
        cached_mesh_file_ = config.grid_file;
    }

    ESMC_Grid target_grid = cached_grid_;
    ESMC_Mesh target_mesh = cached_mesh_;

    for (const auto& name : config.variables) {
        auto it = diagnostics_.find(name);
        if (it == diagnostics_.end()) continue;

        it->second.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();

        if (target_grid.ptr != nullptr || target_mesh.ptr != nullptr) {
            std::cout << "ACES_Diagnostic: Interpolating '" << name << "' to " << config.grid_type
                      << "..." << "\n";
            WriteField(template_field, name);
        } else {
            WriteField(template_field, name);
        }
    }
}

}  // namespace aces
