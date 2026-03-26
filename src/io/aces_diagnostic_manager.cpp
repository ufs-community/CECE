#include "aces/aces_diagnostics.hpp"
#include <iostream>

namespace aces {

DualView3D AcesDiagnosticManager::RegisterDiagnostic(const std::string& name, int nx, int ny, int nz,
                                                     const std::string& units,
                                                     const std::string& long_name) {
    if (diagnostics_.count(name)) {
        return diagnostics_[name].data;
    }

    DualView3D view(name, nx, ny, nz);
    DiagnosticField info;
    info.data = view;
    info.units = units;
    info.long_name = long_name;

    diagnostics_[name] = info;
    return view;
}

void AcesDiagnosticManager::WriteDiagnostics(const DiagnosticConfig& config, int hour, int day_of_week,
                                            const AcesExportState& export_state,
                                            const std::string& output_path) {
    // Stub implementation
}

} // namespace aces
