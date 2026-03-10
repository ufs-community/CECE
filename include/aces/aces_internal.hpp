#ifndef ACES_INTERNAL_HPP
#define ACES_INTERNAL_HPP

#include <Kokkos_Core.hpp>
#include <memory>
#include <vector>
#include <string>

#include "aces/aces_config.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_diagnostics.hpp"
#include "aces/physics_scheme.hpp"
#include "aces/aces_state.hpp"
#include "aces/aces_data_ingestor.hpp"

namespace aces {

/**
 * @brief Internal data structure persisted across ESMF phases.
 */
struct AcesInternalData {
    AcesConfig config;                                          ///< Parsed ACES configuration.
    std::unique_ptr<StackingEngine> stacking_engine;            ///< Optimized compute engine.
    std::unique_ptr<AcesDiagnosticManager> diagnostic_manager;  ///< Diagnostic manager.
    std::vector<std::unique_ptr<PhysicsScheme>>
        active_schemes;            ///< List of active physics plugins.
    AcesImportState import_state;  ///< Input data views.
    AcesExportState export_state;  ///< Output emission views.
    AcesDataIngestor ingestor;     ///< Hybrid data ingestor.
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
        default_mask;                      ///< Persistent 1.0 mask.
    bool kokkos_initialized_here = false;  ///< Flag to track if this component initialized Kokkos.
    bool advertised = false;               ///< Flag to track if the Advertise phase has run.

    // Cached metadata
    int nx = 0, ny = 0, nz = 0;            ///< Cached grid dimensions.
    std::vector<std::string> esmf_fields;  ///< Internal names of fields to ingest from ESMF.
    std::vector<std::string> external_esmf_fields;  ///< External names of fields to ingest.
};

} // namespace aces

#endif // ACES_INTERNAL_HPP
