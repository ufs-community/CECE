#ifndef CECE_INTERNAL_HPP
#define CECE_INTERNAL_HPP

#include <Kokkos_Core.hpp>
#include <memory>
#include <string>
#include <vector>

#include "cece/cece_clock.hpp"
#include "cece/cece_config.hpp"
#include "cece/cece_data_ingestor.hpp"
#include "cece/cece_diagnostics.hpp"
#include "cece/cece_provenance.hpp"
#include "cece/cece_stacking_engine.hpp"
#include "cece/cece_standalone_writer.hpp"
#include "cece/cece_state.hpp"
#include "cece/physics_scheme.hpp"

namespace cece {

/**
 * @brief Internal data structure persisted across ESMF phases.
 */
struct CeceInternalData {
    CeceConfig config;                                                                        ///< Parsed CECE configuration.
    std::unique_ptr<StackingEngine> stacking_engine;                                          ///< Optimized compute engine.
    std::unique_ptr<CeceDiagnosticManager> diagnostic_manager;                                ///< Diagnostic manager.
    std::vector<std::unique_ptr<PhysicsScheme>> active_schemes;                               ///< List of active physics plugins.
    CeceImportState import_state;                                                             ///< Input data views.
    CeceExportState export_state;                                                             ///< Output emission views.
    CeceDataIngestor ingestor;                                                                ///< Hybrid data ingestor.
    ProvenanceTracker provenance;                                                             ///< Emission provenance tracker.
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> default_mask;  ///< Persistent 1.0 mask.
    bool kokkos_initialized_here = false;                                                     ///< Flag to track if this component initialized Kokkos.
    bool advertised = false;                                                                  ///< Flag to track if the Advertise phase has run.
    bool standalone_mode = false;                                                             ///< True when running via the single-model driver.
    std::unique_ptr<CeceStandaloneWriter> standalone_writer;                                  ///< Output writer for standalone mode.
    int step_count = 0;                ///< Current time step counter for output frequency gating.
    std::string start_time_iso8601;    ///< Start time in ISO 8601 format for output.
    std::unique_ptr<CeceClock> clock;  ///< Clock for per-component refresh interval scheduling.

    // Cached metadata
    int nx = 0, ny = 0, nz = 0;                     ///< Cached grid dimensions.
    std::vector<std::string> esmf_fields;           ///< Internal names of fields to ingest from ESMF.
    std::vector<std::string> external_esmf_fields;  ///< External names of fields to ingest.
    std::vector<std::string> realized_fields;       ///< Fields already added to export state (for multi-cycle support).
    std::vector<std::string> unique_input_fields;   ///< List of all unique input fields required by configuration.

    // Field data pointers and metadata
    std::vector<void*> field_pointers;     ///< Data pointers for ESMF fields (one per species).
    std::vector<std::string> field_names;  ///< Names of fields corresponding to field_pointers.
};

}  // namespace cece

#endif  // CECE_INTERNAL_HPP
