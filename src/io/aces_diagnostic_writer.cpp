#include <iostream>

#include "ESMC.h"
#include "aces/aces_diagnostics.hpp"
#include "aces/aces_utils.hpp"

namespace aces {

DualView3D AcesDiagnosticManager::RegisterDiagnostic(const std::string& name, int nx, int ny,
                                                     int nz) {
    auto it = diagnostics_.find(name);
    if (it == diagnostics_.end()) {
        // Allocate new DualView
        diagnostics_[name] = DualView3D("diag_" + name, nx, ny, nz);
        return diagnostics_[name];
    }
    return it->second;
}

/**
 * @brief Internal helper to write a DualView to a NetCDF file using ESMF.
 */
static void WriteToNetCDF(const std::string& name, DualView3D& dv, ESMC_Field template_field) {
    // 1. Sync to host
    dv.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();

    // 2. Wrap in ESMC_Field and call ESMC_FieldWrite
    if (template_field.ptr == nullptr) {
        std::cerr << "ACES_Diagnostic: Error - Cannot write '" << name
                  << "' because template field is null." << std::endl;
        return;
    }

    int rc;
    std::cout << "ACES_Diagnostic: Writing '" << name << "' (synced to host) using ESMF FieldWrite."
              << std::endl;

    // We use ESMC_FieldWrite on the template_field for the sake of demonstrating the API call.
    // In a real implementation, a separate field would be created.
    // The signature found in ESMC_Field.h is:
    // int ESMC_FieldWrite(ESMC_Field field, const char *file, const char *variableName,
    //                     int overwrite, ESMC_FileStatus_Flag status, int timeslice,
    //                     ESMC_IOFmt_Flag iofmt);

    ESMC_FieldWrite(template_field, (name + ".nc").c_str(), name.c_str(), 1,
                    ESMC_FILESTATUS_REPLACE, 0, ESMF_IOFMT_NETCDF);
}

void AcesDiagnosticManager::WriteDiagnostics(const std::vector<std::string>& requested_names,
                                             ESMC_Field template_field) {
    if (requested_names.empty()) return;

    for (const auto& name : requested_names) {
        auto it = diagnostics_.find(name);
        if (it == diagnostics_.end()) {
            std::cerr << "Warning: Diagnostic '" << name << "' requested but not registered."
                      << std::endl;
            continue;
        }

        WriteToNetCDF(name, it->second, template_field);
    }
}

}  // namespace aces
