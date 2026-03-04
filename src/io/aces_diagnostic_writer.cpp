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
                                             ESMC_Field template_field,
                                             const AcesExportState& export_state,
                                             ESMC_State export_state_esmf) {
    if (config.variables.empty()) {
        return;
    }

    // 1. Clock Check
    if (clock.ptr != nullptr && config.output_interval_seconds > 0) {
        ESMC_TimeInterval currSimTime;
        ESMC_I8 stepCount;
        // Correct signature for ESMF 8.8.0
        ESMC_ClockGet(clock, &currSimTime, &stepCount);

        ESMC_I8 seconds_i8;
        ESMC_TimeIntervalGet(currSimTime, &seconds_i8, nullptr);
        long long seconds = static_cast<long long>(seconds_i8);

        if (seconds % config.output_interval_seconds != 0) {
            return;
        }
    }

    // 2. Grid/Mesh Setup (Cached)
    if (config.grid_type == "gaussian" && cached_grid_.ptr == nullptr) {
        int counts[2] = {config.nx, config.ny};
        ESMC_InterArrayInt iCounts;
        ESMC_InterArrayIntSet(&iCounts, counts, 2);
        int rc;
        cached_grid_ = ESMC_GridCreateNoPeriDim(&iCounts, nullptr, nullptr, nullptr, &rc);
    } else if (config.grid_type == "mesh" &&
               (cached_mesh_.ptr == nullptr || cached_mesh_file_ != config.grid_file)) {
        int rc;
        cached_mesh_ = ESMC_MeshCreateFromFile(config.grid_file.c_str(), ESMC_FILEFORMAT_SCRIP,
                                               nullptr, nullptr, nullptr, nullptr, nullptr, &rc);
        cached_mesh_file_ = config.grid_file;
    }

    ESMC_Grid target_grid = cached_grid_;
    ESMC_Mesh target_mesh = cached_mesh_;

    for (const auto& name : config.variables) {
        // First check internal diagnostics
        auto it = diagnostics_.find(name);
        if (it != diagnostics_.end()) {
            it->second.sync<Kokkos::DefaultHostExecutionSpace::memory_space>();
            // For now we use template_field to match the output grid, but this
            // should ideally be the actual diagnostic field wrapped in an ESMF
            // Field.
            WriteField(template_field, name);
            continue;
        }

        // Then check export state (species emissions)
        auto it_exp = export_state.fields.find(name);
        if (it_exp != export_state.fields.end()) {
            const_cast<DualView3D&>(it_exp->second)
                .sync<Kokkos::DefaultHostExecutionSpace::memory_space>();

            // Look up the actual ESMF field to write
            ESMC_Field species_field;
            int rc = ESMC_StateGetField(export_state_esmf, name.c_str(), &species_field);
            if (rc == ESMF_SUCCESS) {
                WriteField(species_field, name);
            } else {
                WriteField(template_field, name);
            }
            continue;
        }
    }
}

}  // namespace aces
