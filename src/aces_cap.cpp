#include <Kokkos_Core.hpp>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <vector>

#include "aces/aces.hpp"
#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_data_ingestor.hpp"
#include "aces/aces_diagnostics.hpp"
#include "aces/aces_physics_factory.hpp"
#include "aces/aces_state.hpp"
#include "aces/aces_utils.hpp"
#include "aces/physics_scheme.hpp"

/**
 * @file aces_cap.cpp
 * @brief ESMF cap for the ACES component.
 *
 * This file implements the ESMF-required entry points (Initialize, Run,
 * Finalize) and bridges the ESMF data structures with the Kokkos-based compute
 * engine.
 */

namespace aces {

/**
 * @brief Internal data structure persisted across ESMF phases.
 *
 * This structure is stored in the ESMF GridComp's internal state to avoid
 * redundant allocations and re-parsing of configuration on every timestep.
 */
struct AcesInternalData {
    AcesConfig config;                                          ///< Parsed ACES configuration.
    std::unique_ptr<AcesDiagnosticManager> diagnostic_manager;  ///< Diagnostic manager.
    std::vector<std::unique_ptr<PhysicsScheme>>
        active_schemes;            ///< List of active physics plugins.
    AcesImportState import_state;  ///< Input data views.
    AcesExportState export_state;  ///< Output emission views.
    AcesDataIngestor ingestor;     ///< Hybrid data ingestor.
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
        default_mask;                      ///< Persistent 1.0 mask.
    bool kokkos_initialized_here = false;  ///< Flag to track if this component initialized Kokkos.
};

/**
 * @brief Helper to create a DualView from an ESMF field.
 *
 * Allocates device memory and mirrors the ESMF host data.
 */
static DualView3D GetDualView(ESMC_State state, const std::string& name, int nx, int ny, int nz) {
    ESMC_Field field;
    int rc = ESMC_StateGetField(state, name.c_str(), &field);
    if (rc != ESMF_SUCCESS) {
        return DualView3D();
    }
    UnmanagedHostView3D host_view = WrapESMCField(field, nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
        "device_" + name, nx, ny, nz);
    return DualView3D(device_view, host_view);
}

/**
 * @brief Internal implementation of Initialize phase.
 */
void Initialize(ESMC_GridComp comp, ESMC_State /*importState*/, ESMC_State /*exportState*/,
                ESMC_Clock* /*clock*/, int* rc) {
    std::cout << "ACES_Initialize: Entering." << std::endl;
    bool kokkos_initialized_here = false;
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize();
        std::cout << "ACES_Initialize: Kokkos initialized." << std::endl;
        kokkos_initialized_here = true;
    }

    if (comp.ptr != nullptr) {
        auto data = new AcesInternalData();
        data->kokkos_initialized_here = kokkos_initialized_here;
        data->config = ParseConfig("aces_config.yaml");
        data->diagnostic_manager = std::make_unique<AcesDiagnosticManager>();

        // Instantiate requested physics schemes
        for (const auto& scheme_config : data->config.physics_schemes) {
            auto scheme = PhysicsFactory::CreateScheme(scheme_config);
            if (scheme) {
                scheme->Initialize(scheme_config.options, data->diagnostic_manager.get());
                data->active_schemes.push_back(std::move(scheme));
            } else {
                std::cerr << "ACES_Initialize: Warning - Failed to create physics scheme: "
                          << scheme_config.name << std::endl;
            }
        }

        // Initialize CDEPS if configured
        data->ingestor.InitializeCDEPS(data->config.cdeps_config);

        // Advertise meteorology and emission fields in the ESMF framework using
        // NUOPC API.
        ESMC_State importState_real = NUOPC_ModelGetImportState(comp, NULL);
        if (importState_real.ptr) {
            for (auto const& [internal_name, external_name] : data->config.met_mapping) {
                NUOPC_Advertise(importState_real, external_name.c_str(), external_name.c_str());
            }
        }

        ESMC_State exportState_real = NUOPC_ModelGetExportState(comp, NULL);
        if (exportState_real.ptr) {
            for (auto const& [species, layers] : data->config.species_layers) {
                std::string export_name = "total_" + species + "_emissions";
                NUOPC_Advertise(exportState_real, export_name.c_str(), export_name.c_str());
            }
        }

        ESMC_GridCompSetInternalState(comp, data);
    }

    if (rc) *rc = ESMF_SUCCESS;
}

/**
 * @brief Internal implementation of Run phase.
 */
void Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
         int* rc) {
    std::cout << "ACES_Run: Executing." << std::endl;

    if (comp.ptr == nullptr) {
        if (rc) *rc = ESMF_SUCCESS;
        return;
    }

    int rc_internal;
    void* data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
    if (!data_ptr) {
        std::cerr << "ACES_Run Error: Internal state not found." << std::endl;
        if (rc) *rc = -1;
        return;
    }
    auto data = static_cast<AcesInternalData*>(data_ptr);

    // Dynamic dimension discovery from actual ESMF fields in the export state.
    int nx = 0, ny = 0, nz = 0;
    ESMC_Field field;
    field.ptr = nullptr;

    // Use the first available configured species as a reference for dimensions
    if (!data->config.species_layers.empty()) {
        std::string ref_field_name =
            "total_" + data->config.species_layers.begin()->first + "_emissions";
        ESMC_StateGetField(exportState, ref_field_name.c_str(), &field);
    }

    if (field.ptr) {
        int lbound[3], ubound[3];
        ESMC_FieldGetBounds(field, NULL, lbound, ubound, 3);
        nx = ubound[0] - lbound[0] + 1;
        ny = ubound[1] - lbound[1] + 1;
        nz = ubound[2] - lbound[2] + 1;
    } else {
        // Fallback for cases with empty configuration or missing expected fields
        nx = 360;
        ny = 180;
        nz = 72;
        std::cerr << "ACES_Run: Warning - Could not discover grid dimensions, "
                     "using defaults "
                  << nx << "x" << ny << "x" << nz << std::endl;
    }

    // Lazily initialize persistent DualViews for export state.
    for (auto const& [species, layers] : data->config.species_layers) {
        std::string export_name = "total_" + species + "_emissions";
        if (data->export_state.fields.find(export_name) == data->export_state.fields.end()) {
            data->export_state.fields.try_emplace(
                export_name, GetDualView(exportState, export_name, nx, ny, nz));
        }
    }

    // Lazily initialize persistent scratch views.
    if (data->default_mask.extent(0) != (size_t)nx || data->default_mask.extent(1) != (size_t)ny ||
        data->default_mask.extent(2) != (size_t)nz) {
        data->default_mask =
            Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
                "default_mask", nx, ny, nz);
        Kokkos::deep_copy(data->default_mask, 1.0);
    }

    // Hybrid data ingestion:
    // 1. Meteorology/State from ESMF
    // We dynamically identify which fields are needed from ESMF.
    // These are fields used in 'species' definitions that are NOT provided by
    // CDEPS.
    std::set<std::string> esmf_fields_set;
    std::set<std::string> cdeps_fields;
    for (const auto& s : data->config.cdeps_config.streams) cdeps_fields.insert(s.name);

    for (auto const& [species, layers] : data->config.species_layers) {
        for (const auto& layer : layers) {
            if (cdeps_fields.find(layer.field_name) == cdeps_fields.end()) {
                esmf_fields_set.insert(layer.field_name);
            }

            std::copy_if(
                layer.scale_fields.begin(), layer.scale_fields.end(),
                std::inserter(esmf_fields_set, esmf_fields_set.end()),
                [&](const std::string& sf) { return cdeps_fields.find(sf) == cdeps_fields.end(); });

            for (const auto& m : layer.masks) {
                if (cdeps_fields.find(m) == cdeps_fields.end()) {
                    esmf_fields_set.insert(m);
                }
            }
        }
    }
    std::vector<std::string> esmf_fields(esmf_fields_set.begin(), esmf_fields_set.end());

    Kokkos::Profiling::pushRegion("ACES_DataIngestion");

    // Apply meteorology mapping to get external names for ESMF ingestion
    std::vector<std::string> external_esmf_fields;
    for (const auto& internal_name : esmf_fields) {
        auto it = data->config.met_mapping.find(internal_name);
        if (it != data->config.met_mapping.end()) {
            external_esmf_fields.push_back(it->second);
        } else {
            external_esmf_fields.push_back(internal_name);
        }
    }

    data->ingestor.IngestMeteorology(importState, external_esmf_fields, data->import_state, nx, ny,
                                     nz);

    // 2. Emissions from CDEPS
    if (!data->config.cdeps_config.streams.empty()) {
        data->ingestor.IngestEmissionsInline(data->config.cdeps_config, data->import_state, nx, ny,
                                             nz);
    }
    Kokkos::Profiling::popRegion();

    // Extract time information from ESMF clock
    int hour = 0;
    int day_of_week = 0;
    if (clock != nullptr) {
        // In standard ESMF C API for version 8.8.0:
        // int ESMC_ClockGet(ESMC_Clock clock, ESMC_TimeInterval *currSimTime,
        // ESMC_I8 *advanceCount);
        ESMC_TimeInterval currSimTime;
        ESMC_I8 advanceCount;
        ESMC_ClockGet(*clock, &currSimTime, &advanceCount);

        ESMC_I8 seconds_i8;
        ESMC_TimeIntervalGet(currSimTime, &seconds_i8, NULL);

        // Scientific Logic Improvement: Diurnal/Weekly cycles tied to simulation
        // time offset. For production, this should ideally be tied to absolute UTC
        // time from ESMC_ClockGetCurrTime (if available in bridge).
        hour = (int)((seconds_i8 / 3600) % 24);
        day_of_week = (int)((seconds_i8 / 86400) % 7);
    }

    // Advertise meteorology fields in the internal state metadata if requested by
    // the driver. In NUOPC, this is usually handled via Advertise and Realize
    // phases. Here we ensure the field names we expect (including mappings) are
    // known.

    // Run core compute engine (layer addition/replacement)
    Kokkos::Profiling::pushRegion("ACES_StackingEngine");
    AcesStateResolver resolver(data->import_state, data->export_state, data->config.met_mapping);
    ComputeEmissions(data->config, resolver, nx, ny, nz, data->default_mask, hour, day_of_week);
    Kokkos::Profiling::popRegion();

    auto& imp = data->import_state;
    auto& exp = data->export_state;

    // Run active physics plugins (Sea Salt, Dust, etc.)
    Kokkos::Profiling::pushRegion("ACES_PhysicsExtensions");
    for (auto& scheme : data->active_schemes) {
        scheme->Run(imp, exp);
    }
    Kokkos::Profiling::popRegion();

    // Write diagnostics
    // We use the last discovered field as a template for grid information
    Kokkos::Profiling::pushRegion("ACES_Writeback");
    if (clock != nullptr) {
        data->diagnostic_manager->WriteDiagnostics(data->config.diagnostics, *clock, field);
    }

    // Sync results back to host space so the ESMF framework can see updated field
    // data.
    for (auto& [name, dv] : exp.fields) {
        if (dv.view_host().data()) {
            dv.sync<Kokkos::HostSpace>();
        }
    }
    Kokkos::Profiling::popRegion();

    if (rc) *rc = ESMF_SUCCESS;
}

/**
 * @brief Internal implementation of Finalize phase.
 */
void Finalize(ESMC_GridComp comp, ESMC_State /*importState*/, ESMC_State /*exportState*/,
              ESMC_Clock* /*clock*/, int* rc) {
    bool kokkos_initialized_here = false;
    if (comp.ptr != nullptr) {
        int rc_internal;
        void* data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
        if (data_ptr) {
            auto data = static_cast<AcesInternalData*>(data_ptr);
            kokkos_initialized_here = data->kokkos_initialized_here;

            // Finalize CDEPS
            data->ingestor.FinalizeCDEPS();

            // Clear active schemes and states to ensure Views are destroyed
            // before Kokkos::finalize is called.
            data->active_schemes.clear();
            data->import_state.fields.clear();
            data->export_state.fields.clear();
            data->diagnostic_manager.reset();

            delete data;
        }
    }

    // Only finalize Kokkos if we were the ones who initialized it.
    // This prevents crashing top-level drivers that manage Kokkos themselves.
    if (kokkos_initialized_here && Kokkos::is_initialized()) {
        Kokkos::finalize();
        std::cout << "ACES_Finalize: Kokkos finalized." << std::endl;
    }
    if (rc) *rc = ESMF_SUCCESS;
}

}  // namespace aces

extern "C" {

/**
 * @brief ESMF Initialize entry point.
 */
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                     ESMC_Clock* clock, int* rc) {
    aces::Initialize(comp, importState, exportState, clock, rc);
}

/**
 * @brief ESMF Run entry point.
 */
void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc) {
    aces::Run(comp, importState, exportState, clock, rc);
}

/**
 * @brief ESMF Finalize entry point.
 */
void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                   ESMC_Clock* clock, int* rc) {
    aces::Finalize(comp, importState, exportState, clock, rc);
}

/**
 * @brief Standard NUOPC SetServices routine.
 * Registers standard ESMF entry points.
 */
void ACES_SetServices(ESMC_GridComp comp, int* rc) {
    std::cout << "ACES_SetServices: Entering." << std::endl;

    // Register standard ESMF entry points.
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, ACES_Initialize, 1);
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_RUN, ACES_Run, 1);
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_FINALIZE, ACES_Finalize, 1);

    if (rc) *rc = ESMF_SUCCESS;
    std::cout << "ACES_SetServices: Services set." << std::endl;
}

}  // extern "C"
