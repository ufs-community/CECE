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
#include "aces/aces_stacking_engine.hpp"
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

/**
 * @brief Helper to create a DualView from an ESMF field.
 *
 * Allocates device memory and mirrors the ESMF host data.
 */
static DualView3D GetDualView(ESMC_State state, const std::string& name, int nx, int ny, int nz) {
    ESMC_Field field;
    int rc = ESMC_StateGetField(state, name.c_str(), &field);
    if (rc != ESMF_SUCCESS) {
        return {};
    }
    UnmanagedHostView3D host_view = WrapESMCField(field, nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
        "device_" + name, nx, ny, nz);
    return DualView3D(device_view, host_view);
}

/**
 * @brief Internal implementation of the Advertise specialization.
 */
void Advertise(ESMC_GridComp comp, int* rc) {
    std::cout << "ACES_Advertise: Entering." << "\n";
    int rc_internal;
    void* data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
    if (!data_ptr) {
        // Internal state might not be set yet if Advertise is called before Initialize
        AcesConfig config;
        try {
            config = aces::ParseConfig("aces_config.yaml");
        } catch (...) {
            std::cerr << "ACES_Advertise: Warning - Could not load aces_config.yaml. "
                         "Bypassing Advertise phase."
                      << "\n";
            if (rc) *rc = ESMF_SUCCESS;
            return;
        }

        ESMC_State importState = NUOPC_ModelGetImportState(comp, &rc_internal);
        if (importState.ptr) {
            for (auto const& [internal_name, external_name] : config.met_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
            for (auto const& [internal_name, external_name] : config.scale_factor_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
            for (auto const& [internal_name, external_name] : config.mask_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
        }

        ESMC_State exportState = NUOPC_ModelGetExportState(comp, &rc_internal);
        if (exportState.ptr) {
            for (auto const& [species, layers] : config.species_layers) {
                std::string export_name = "total_" + species + "_emissions";
                NUOPC_Advertise(exportState, export_name.c_str(), export_name.c_str());
            }
        }
    } else {
        auto* data = static_cast<AcesInternalData*>(data_ptr);
        if (data->advertised) {
            if (rc) *rc = ESMF_SUCCESS;
            return;
        }
        ESMC_State importState = NUOPC_ModelGetImportState(comp, &rc_internal);
        if (importState.ptr) {
            for (auto const& [internal_name, external_name] : data->config.met_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
            for (auto const& [internal_name, external_name] : data->config.scale_factor_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
            for (auto const& [internal_name, external_name] : data->config.mask_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
        }
        ESMC_State exportState = NUOPC_ModelGetExportState(comp, &rc_internal);
        if (exportState.ptr) {
            for (auto const& [species, layers] : data->config.species_layers) {
                std::string export_name = "total_" + species + "_emissions";
                NUOPC_Advertise(exportState, export_name.c_str(), export_name.c_str());
            }
        }
        data->advertised = true;
    }
    if (rc) *rc = ESMF_SUCCESS;
}

/**
 * @brief Internal implementation of Initialize phase.
 */
void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                ESMC_Clock* /*clock*/, int* rc) {
    std::cout << "ACES_Initialize: Entering." << "\n";
    bool kokkos_initialized_here = false;
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize();
        std::cout << "ACES_Initialize: Kokkos initialized." << "\n";
        kokkos_initialized_here = true;
    }

    if (comp.ptr != nullptr) {
        auto data = new AcesInternalData();
        data->kokkos_initialized_here = kokkos_initialized_here;
        try {
            data->config = ParseConfig("aces_config.yaml");
        } catch (...) {
            std::cerr << "ACES_Initialize: Error - Could not load aces_config.yaml. "
                         "Component will be improperly configured."
                      << "\n";
            if (rc) *rc = -1;
            return;
        }
        data->diagnostic_manager = std::make_unique<AcesDiagnosticManager>();
        data->stacking_engine = std::make_unique<StackingEngine>(data->config);

        // Instantiate requested physics schemes
        for (const auto& scheme_config : data->config.physics_schemes) {
            auto scheme = PhysicsFactory::CreateScheme(scheme_config);
            if (scheme) {
                scheme->Initialize(scheme_config.options, data->diagnostic_manager.get());
                data->active_schemes.push_back(std::move(scheme));
            } else {
                std::cerr << "ACES_Initialize: Warning - Failed to create physics scheme: "
                          << scheme_config.name << "\n";
            }
        }

        // Initialize CDEPS if configured
        data->ingestor.InitializeCDEPS(data->config.cdeps_config);

        // Advertise if not already done (fallback for standalone drivers)
        if (!data->advertised) {
            for (auto const& [internal_name, external_name] : data->config.met_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
            for (auto const& [internal_name, external_name] : data->config.scale_factor_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
            for (auto const& [internal_name, external_name] : data->config.mask_mapping) {
                NUOPC_Advertise(importState, external_name.c_str(), external_name.c_str());
            }
            for (auto const& [species, layers] : data->config.species_layers) {
                std::string export_name = "total_" + species + "_emissions";
                NUOPC_Advertise(exportState, export_name.c_str(), export_name.c_str());
            }
            data->advertised = true;
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
    std::cout << "ACES_Run: Executing." << "\n";

    if (comp.ptr == nullptr) {
        if (rc) *rc = ESMF_SUCCESS;
        return;
    }

    int rc_internal;
    void* data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
    if (!data_ptr) {
        std::cerr << "ACES_Run Error: Internal state not found." << "\n";
        if (rc) *rc = -1;
        return;
    }
    auto* data = static_cast<AcesInternalData*>(data_ptr);

    // Dynamic dimension discovery (Cached after first discovery)
    if (data->nx == 0) {
        ESMC_Field field;
        field.ptr = nullptr;

        if (!data->config.species_layers.empty()) {
            std::string ref_field_name =
                "total_" + data->config.species_layers.begin()->first + "_emissions";
            int local_rc = ESMC_StateGetField(exportState, ref_field_name.c_str(), &field);
            if (local_rc != ESMF_SUCCESS) field.ptr = nullptr;
        }

        if (field.ptr == nullptr) {
            ESMC_StateGetField(exportState, "total_aces_discovery_emissions", &field);
        }

        if (field.ptr) {
            int lbound[3] = {1, 1, 1}, ubound[3] = {1, 1, 1}, localDe = 0;
            if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 3) == ESMF_SUCCESS) {
                data->nx = ubound[0] - lbound[0] + 1;
                data->ny = ubound[1] - lbound[1] + 1;
                data->nz = ubound[2] - lbound[2] + 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 2) == ESMF_SUCCESS) {
                data->nx = ubound[0] - lbound[0] + 1;
                data->ny = ubound[1] - lbound[1] + 1;
                data->nz = 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound, ubound, 1) == ESMF_SUCCESS) {
                data->nx = ubound[0] - lbound[0] + 1;
                data->ny = 1;
                data->nz = 1;
            }
        }

        // Final safety fallback
        if (data->nx <= 0) {
            data->nx = 360;
            data->ny = 180;
            data->nz = 72;
        }
    }

    int nx = data->nx;
    int ny = data->ny;
    int nz = data->nz;

    // Lazily initialize persistent DualViews for export state.
    for (auto const& [species, layers] : data->config.species_layers) {
        std::string export_name = "total_" + species + "_emissions";
        if (data->export_state.fields.find(export_name) == data->export_state.fields.end()) {
            data->export_state.fields.try_emplace(
                export_name, GetDualView(exportState, export_name, nx, ny, nz));
        }
    }

    // Lazily initialize persistent scratch views.
    if (data->default_mask.extent(0) != static_cast<size_t>(nx) ||
        data->default_mask.extent(1) != static_cast<size_t>(ny) ||
        data->default_mask.extent(2) != static_cast<size_t>(nz)) {
        std::cout << "ACES_Run: Re-initializing default mask for dimensions " << nx << "x" << ny
                  << "x" << nz << "\n";
        data->default_mask =
            Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>(
                "default_mask", nx, ny, nz);
        Kokkos::deep_copy(data->default_mask, 1.0);
    }

    // Hybrid data ingestion (Metadata cached after first run)
    if (data->esmf_fields.empty()) {
        std::set<std::string> esmf_fields_set;
        std::set<std::string> cdeps_fields;
        for (const auto& s : data->config.cdeps_config.streams) cdeps_fields.insert(s.name);

        auto resolve_name = [&](const std::string& name) {
            auto it = data->config.met_mapping.find(name);
            if (it != data->config.met_mapping.end()) return it->second;
            it = data->config.scale_factor_mapping.find(name);
            if (it != data->config.scale_factor_mapping.end()) return it->second;
            it = data->config.mask_mapping.find(name);
            if (it != data->config.mask_mapping.end()) return it->second;
            return name;
        };

        // Include vertical coordinate fields if configured
        if (data->config.vertical_config.type != VerticalCoordType::NONE) {
            esmf_fields_set.insert(data->config.vertical_config.p_surf_field);
            if (data->config.vertical_config.type == VerticalCoordType::FV3) {
                esmf_fields_set.insert(data->config.vertical_config.ak_field);
                esmf_fields_set.insert(data->config.vertical_config.bk_field);
            } else if (data->config.vertical_config.type == VerticalCoordType::MPAS ||
                       data->config.vertical_config.type == VerticalCoordType::WRF) {
                esmf_fields_set.insert(data->config.vertical_config.z_field);
            }
            esmf_fields_set.insert(data->config.vertical_config.pbl_field);
        }

        for (auto const& [species, layers] : data->config.species_layers) {
            for (const auto& layer : layers) {
                if (cdeps_fields.find(resolve_name(layer.field_name)) == cdeps_fields.end()) {
                    esmf_fields_set.insert(layer.field_name);
                }
                for (const auto& sf : layer.scale_fields) {
                    if (cdeps_fields.find(resolve_name(sf)) == cdeps_fields.end()) {
                        esmf_fields_set.insert(sf);
                    }
                }
                for (const auto& m : layer.masks) {
                    if (cdeps_fields.find(resolve_name(m)) == cdeps_fields.end()) {
                        esmf_fields_set.insert(m);
                    }
                }
            }
        }

        data->esmf_fields.assign(esmf_fields_set.begin(), esmf_fields_set.end());
        for (const auto& internal_name : data->esmf_fields) {
            data->external_esmf_fields.push_back(resolve_name(internal_name));
        }
    }

    Kokkos::Profiling::pushRegion("ACES_DataIngestion");

    data->ingestor.IngestMeteorology(importState, data->external_esmf_fields, data->import_state,
                                     nx, ny, nz);

    // Some fields might be 2D or have different Z dimensions (like ak/bk)
    // The ingestor currently assumes nx, ny, nz for all. We might need to adjust.

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
        ESMC_TimeInterval currSimTime;
        ESMC_I8 advanceCount;
        ESMC_ClockGet(*clock, &currSimTime, &advanceCount);

        ESMC_I8 seconds_i8;
        ESMC_TimeIntervalGet(currSimTime, &seconds_i8, NULL);

        hour = (int)((seconds_i8 / 3600) % 24);
        day_of_week = (int)((seconds_i8 / 86400) % 7);
    }

    // Run core compute engine (layer addition/replacement)
    Kokkos::Profiling::pushRegion("ACES_StackingEngine");
    AcesStateResolver resolver(data->import_state, data->export_state, data->config.met_mapping,
                               data->config.scale_factor_mapping, data->config.mask_mapping);
    data->stacking_engine->Execute(resolver, nx, ny, nz, data->default_mask, hour, day_of_week);
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
    Kokkos::Profiling::pushRegion("ACES_Writeback");
    if (clock != nullptr) {
        ESMC_Field template_field;
        template_field.ptr = nullptr;
        if (!data->config.species_layers.empty()) {
            std::string ref_field_name =
                "total_" + data->config.species_layers.begin()->first + "_emissions";
            ESMC_StateGetField(exportState, ref_field_name.c_str(), &template_field);
        }
        data->diagnostic_manager->WriteDiagnostics(data->config.diagnostics, *clock,
                                                   template_field);
    }

    // Sync results back to host space
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
            auto* data = static_cast<AcesInternalData*>(data_ptr);
            kokkos_initialized_here = data->kokkos_initialized_here;

            // Finalize CDEPS
            data->ingestor.FinalizeCDEPS();

            // Clear active schemes and states to ensure Views are destroyed
            for (auto& scheme : data->active_schemes) {
                if (auto* base = dynamic_cast<BasePhysicsScheme*>(scheme.get())) {
                    base->ClearPhysicsCache();
                }
            }
            data->active_schemes.clear();
            data->import_state.fields.clear();
            data->export_state.fields.clear();
            data->diagnostic_manager.reset();

            delete data;
        }
    }

    if (kokkos_initialized_here && Kokkos::is_initialized()) {
        Kokkos::finalize();
        std::cout << "ACES_Finalize: Kokkos finalized." << "\n";
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
 * @brief Standard NUOPC Advertise routine.
 */
void ACES_Advertise(ESMC_GridComp comp, int* rc) {
    aces::Advertise(comp, rc);
}

/**
 * @brief Standard NUOPC SetServices routine.
 * Registers standard ESMF entry points.
 */
void ACES_SetServices(ESMC_GridComp comp, int* rc) {
    std::cout << "ACES_SetServices: Entering." << "\n";

    // Register the component as a NUOPC Model
    int local_rc = NUOPC_CompDerive(comp, NUOPC_ModelSetServices);
    if (local_rc != ESMF_SUCCESS) {
        if (rc) *rc = local_rc;
        return;
    }

    // Specialize Advertise phase
    NUOPC_CompSpecialize(comp, label_Advertise, ACES_Advertise);

    // Register standard ESMF entry points.
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, ACES_Initialize, 1);
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_RUN, ACES_Run, 1);
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_FINALIZE, ACES_Finalize, 1);

    if (rc) *rc = ESMF_SUCCESS;
    std::cout << "ACES_SetServices: Services set." << "\n";
}

}  // extern "C"
