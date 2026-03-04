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
     * @brief Registers a new diagnostic variable with metadata.
     * @param name Unique name for the diagnostic.
     * @param nx Grid dimension in x.
     * @param ny Grid dimension in y.
     * @param nz Grid dimension in z.
     * @param units CF-compliant units.
     * @param long_name Descriptive name.
     * @return DualView3D The allocated DualView for the diagnostic.
     */
    DualView3D RegisterDiagnostic(const std::string& name, int nx, int ny, int nz,
                                  const std::string& units = "", const std::string& long_name = "");

    /**
     * @brief Writes requested diagnostics to disk.
     * @param config Diagnostic configuration.
     * @param clock ESMF clock for timing.
     * @param template_field A field used to derive grid information for output.
     * @param export_state Internal export state.
     * @param export_state_esmf ESMF export state for species lookup.
     */
    void WriteDiagnostics(const DiagnosticConfig& config, ESMC_Clock clock,
                          ESMC_Field template_field, const AcesExportState& export_state,
                          ESMC_State export_state_esmf);

   private:
    struct DiagnosticField {
        DualView3D data;
        std::string units;
        std::string long_name;
    };

    std::unordered_map<std::string, DiagnosticField> diagnostics_;
    ESMC_Grid cached_grid_{nullptr};
    ESMC_Mesh cached_mesh_{nullptr};
    std::string cached_mesh_file_;
};

}  // namespace aces

#endif  // ACES_DIAGNOSTICS_HPP
