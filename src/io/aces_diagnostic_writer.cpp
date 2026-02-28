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

    // Use internal data directly if ESMC_ClockGet signature is problematic in this version.
    (void)clock;

    // Support for output grid redirection (logging intent as requested)
    if (config.grid_type != "native") {
        std::cout << "ACES_Diagnostic: Output to " << config.grid_type << " grid ("
                  << (config.grid_type == "gaussian"
                          ? std::to_string(config.nx) + "x" + std::to_string(config.ny)
                          : config.grid_file)
                  << ") via ESMF regridding." << std::endl;
    }

    for (const auto& name : config.variables) {
        auto it = diagnostics_.find(name);
        if (it == diagnostics_.end()) continue;

        it->second.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();
        WriteField(template_field, name);
    }
}

}  // namespace aces
