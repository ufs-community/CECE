#ifndef ACES_DIAGNOSTICS_HPP
#define ACES_DIAGNOSTICS_HPP

#include <string>
#include <unordered_map>
#include <vector>

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
     * @param hour Current hour of day (0-23).
     * @param day_of_week Current day of week (0-6).
     * @param export_state Internal export state.
     * @param output_path Optional output directory path.
     */
    void WriteDiagnostics(const DiagnosticConfig& config, int hour, int day_of_week,
                          const AcesExportState& export_state, const std::string& output_path = "");

   private:
    struct DiagnosticField {
        DualView3D data;
        std::string units;
        std::string long_name;
    };

    std::unordered_map<std::string, DiagnosticField> diagnostics_;
};

}  // namespace aces

#endif  // ACES_DIAGNOSTICS_HPP
