#ifndef ACES_DIAGNOSTICS_HPP
#define ACES_DIAGNOSTICS_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "ESMC.h"
#include "aces/aces_config.hpp"
#include "aces/aces_state.hpp"

namespace aces {

/**
 * @class AcesDiagnosticManager
 * @brief Manages registration and output of diagnostic variables.
 */
class AcesDiagnosticManager {
   public:
    AcesDiagnosticManager() = default;

    /**
     * @brief Registers a new diagnostic variable.
     * @param name Unique name for the diagnostic.
     * @param nx Grid dimension in x.
     * @param ny Grid dimension in y.
     * @param nz Grid dimension in z.
     * @return DualView3D The allocated DualView for the diagnostic.
     */
    DualView3D RegisterDiagnostic(const std::string& name, int nx, int ny, int nz);

    /**
     * @brief Writes requested diagnostics to disk.
     * @param config Diagnostic configuration.
     * @param clock ESMF clock for timing.
     * @param template_field A field used to derive grid information for output.
     */
    void WriteDiagnostics(const DiagnosticConfig& config, ESMC_Clock clock,
                          ESMC_Field template_field, const AcesExportState& export_state,
                          ESMC_State export_state_esmf);

   private:
    std::unordered_map<std::string, DualView3D> diagnostics_;
    ESMC_Grid cached_grid_{nullptr};
    ESMC_Mesh cached_mesh_{nullptr};
    std::string cached_mesh_file_;
};

}  // namespace aces

#endif  // ACES_DIAGNOSTICS_HPP
