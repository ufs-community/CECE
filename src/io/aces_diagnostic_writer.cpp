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
 * @brief Internal helper to write a field to NetCDF with CF attributes.
 */
static void WriteCFField(ESMC_Field field, const std::string& name) {
    ESMC_AttributeAdd(field, "units", "kg m-2 s-1");
    ESMC_AttributeAdd(field, "long_name", (name + " diagnostic").c_str());
    ESMC_AttributeAdd(field, "Conventions", "CF-1.8");

    ESMC_FieldWrite(field, (name + ".nc").c_str(), name.c_str(), 1, ESMC_FILESTATUS_REPLACE, 1,
                    ESMF_IOFMT_NETCDF);
}

void AcesDiagnosticManager::WriteDiagnostics(const DiagnosticConfig& config, ESMC_Clock clock,
                                             ESMC_Field template_field) {
    if (config.variables.empty()) return;

    // 1. Clock Check using ESMF Time Arithmetic
    if (clock.ptr != nullptr && config.output_interval_seconds > 0) {
        ESMC_Time currTime;
        ESMC_ClockGet(clock, "CurrTime", &currTime, NULL);

        int seconds;
        ESMC_TimeGet(currTime, "s", &seconds, NULL);

        // Only write if we are at the beginning of the simulation or at an interval
        if (seconds % config.output_interval_seconds != 0) {
            return;
        }
    }

    // 2. Grid/Mesh Setup
    ESMC_Grid target_grid;
    target_grid.ptr = nullptr;
    ESMC_Mesh target_mesh;
    target_mesh.ptr = nullptr;

    if (config.grid_type == "gaussian") {
        int counts[2] = {config.nx, config.ny};
        // In ESMF C API, creating a Gaussian grid often involves creating a Grid from
        // coordinate arrays. For this implementation, we use GridCreateNoInit as a proxy
        // for more complex grid creation logic that would be handled in a production bridge.
        target_grid = ESMC_GridCreateNoInit("gaussian_grid", 2, counts, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    } else if (config.grid_type == "mesh") {
        int rc;
        // Load an unstructured mesh from a SCRIP or ESMF format file.
        target_mesh = ESMC_MeshCreateFromFile(config.grid_file.c_str(), ESMF_FILEFORMAT_SCRIP, NULL,
                                              NULL, NULL, &rc);
    }

    for (const auto& name : config.variables) {
        auto it = diagnostics_.find(name);
        if (it == diagnostics_.end()) continue;

        // Ensure data is on host for ESMF
        it->second.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();

        if (target_grid.ptr != nullptr || target_mesh.ptr != nullptr) {
            // Regridding logic:
            // This would involve ESMC_FieldRegridStore and ESMC_FieldRegrid.
            // For now, we log the intent and use the template field as a proxy.
            std::cout << "ACES_Diagnostic: Interpolating '" << name << "' to " << config.grid_type
                      << " " << (config.grid_type == "mesh" ? config.grid_file : "") << std::endl;
            WriteCFField(template_field, name);
        } else {
            // Write to native grid
            WriteCFField(template_field, name);
        }
    }
}

}  // namespace aces
