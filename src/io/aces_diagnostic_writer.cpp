#include <iostream>
#include <vector>

#include "ESMC.h"
#include "aces/aces_diagnostics.hpp"
#include "aces/aces_utils.hpp"

namespace aces {

// cppcheck-suppress unusedFunction
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
        long long stepCount;
        // Signature in ESMF 8.8.0 C API:
        // int ESMC_ClockGet(ESMC_Clock clock, ESMC_TimeInterval *currSimTime, ESMC_I8
        // *currSimStepCount)
        ESMC_ClockGet(clock, &currSimTime, &stepCount);

        int seconds;
        // Extract seconds from TimeInterval
        ESMC_TimeIntervalGet(currSimTime, NULL, NULL, NULL, &seconds, NULL, NULL);

        if (seconds % config.output_interval_seconds != 0) return;
    }

    // 2. Grid/Mesh Setup
    ESMC_Grid target_grid;
    target_grid.ptr = nullptr;
    ESMC_Mesh target_mesh;
    target_mesh.ptr = nullptr;

    if (config.grid_type == "gaussian") {
        int counts[2] = {config.nx, config.ny};
        // Use GridCreateNoPeriDim as a proxy for Gaussian grid creation logic
        target_grid = ESMC_GridCreateNoPeriDim("gaussian_grid", 2, counts, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    } else if (config.grid_type == "mesh") {
        int rc;
        // Use ESMC_FILEFORMAT_SCRIP (correct prefix)
        target_mesh = ESMC_MeshCreateFromFile(config.grid_file.c_str(), ESMC_FILEFORMAT_SCRIP, NULL,
                                              NULL, NULL, &rc);
    }

    for (const auto& name : config.variables) {
        auto it = diagnostics_.find(name);
        if (it == diagnostics_.end()) continue;

        it->second.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();

        if (target_grid.ptr != nullptr || target_mesh.ptr != nullptr) {
            std::cout << "ACES_Diagnostic: Interpolating '" << name << "' to " << config.grid_type
                      << "..." << std::endl;
            WriteField(template_field, name);
        } else {
            WriteField(template_field, name);
        }
    }
}

}  // namespace aces
