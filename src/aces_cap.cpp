#include <Kokkos_Core.hpp>
#include <iostream>
#include <memory>
#include <vector>

#include "aces/aces.hpp"
#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_data_ingestor.hpp"
#include "aces/aces_physics_factory.hpp"
#include "aces/aces_state.hpp"
#include "aces/aces_utils.hpp"
#include "aces/physics_scheme.hpp"

/**
 * @file aces_cap.cpp
 * @brief ESMF cap for the ACES component.
 *
 * This file implements the ESMF-required entry points (Initialize, Run, Finalize)
 * and bridges the ESMF data structures with the Kokkos-based compute engine.
 */

namespace aces {

/**
 * @brief Internal data structure persisted across ESMF phases.
 *
 * This structure is stored in the ESMF GridComp's internal state to avoid
 * redundant allocations and re-parsing of configuration on every timestep.
 */
struct AcesInternalData {
    AcesConfig config;  ///< Parsed ACES configuration.
    std::vector<std::unique_ptr<PhysicsScheme>>
        active_schemes;                    ///< List of active physics plugins.
    AcesImportState import_state;          ///< Input data views.
    AcesExportState export_state;          ///< Output emission views.
    AcesDataIngestor ingestor;             ///< Hybrid data ingestor.
    bool kokkos_initialized_here = false;  ///< Flag to track if this component initialized Kokkos.
};

/**
 * @brief ESMF implementation of FieldResolver.
 *
 * Bridges the abstract FieldResolver interface with actual ESMF State lookups.
 */
class EsmfFieldResolver : public FieldResolver {
    ESMC_State importState;
    ESMC_State exportState;

   public:
    EsmfFieldResolver(ESMC_State imp, ESMC_State exp) : importState(imp), exportState(exp) {}

    /**
     * @brief Resolves an import field by name and wraps it in a Kokkos View.
     */
    UnmanagedHostView3D ResolveImport(const std::string& name, int nx, int ny, int nz) override {
        ESMC_Field field;
        int rc = ESMC_StateGetField(importState, name.c_str(), &field);
        if (rc != ESMF_SUCCESS) return UnmanagedHostView3D();
        return WrapESMCField(field, nx, ny, nz);
    }

    /**
     * @brief Resolves an export field by name and wraps it in a Kokkos View.
     */
    UnmanagedHostView3D ResolveExport(const std::string& name, int nx, int ny, int nz) override {
        ESMC_Field field;
        int rc = ESMC_StateGetField(exportState, name.c_str(), &field);
        if (rc != ESMF_SUCCESS) return UnmanagedHostView3D();
        return WrapESMCField(field, nx, ny, nz);
    }
};

/**
 * @brief FieldResolver that pulls from the unified AcesImportState and AcesExportState.
 */
class AcesStateResolver : public FieldResolver {
    const AcesImportState& import_state;
    const AcesExportState& export_state;

   public:
    AcesStateResolver(const AcesImportState& imp, const AcesExportState& exp)
        : import_state(imp), export_state(exp) {}

    UnmanagedHostView3D ResolveImport(const std::string& name, int nx, int ny, int nz) override {
        if (name == "temperature") return import_state.temperature.view_host();
        if (name == "wind_speed_10m") return import_state.wind_speed_10m.view_host();
        if (name == "base_anthropogenic_nox")
            return import_state.base_anthropogenic_nox.view_host();
        return UnmanagedHostView3D();
    }

    UnmanagedHostView3D ResolveExport(const std::string& name, int nx, int ny, int nz) override {
        if (name == "total_nox_emissions") return export_state.total_nox_emissions.view_host();
        return UnmanagedHostView3D();
    }
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
void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                ESMC_Clock* clock, int* rc) {
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

        // Instantiate requested physics schemes
        for (const auto& scheme_config : data->config.physics_schemes) {
            data->active_schemes.push_back(PhysicsFactory::CreateScheme(scheme_config));
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
    std::string ref_field_name = "total_nox_emissions";
    if (ESMC_StateGetField(exportState, ref_field_name.c_str(), &field) != ESMF_SUCCESS) {
        // Use any configured species as a reference if nox isn't present
        if (!data->config.species_layers.empty()) {
            ref_field_name = "total_" + data->config.species_layers.begin()->first + "_emissions";
            ESMC_StateGetField(exportState, ref_field_name.c_str(), &field);
        }
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
        std::cerr << "ACES_Run: Warning - Could not discover grid dimensions, using defaults " << nx
                  << "x" << ny << "x" << nz << std::endl;
    }

    // Hybrid data ingestion
    data->ingestor.IngestMeteorology(importState, data->import_state, nx, ny, nz);
    if (!data->config.cdeps_config.streams.empty()) {
        data->ingestor.IngestEmissionsInline(data->config.cdeps_config, data->import_state, nx, ny,
                                             nz);
    }

    // Run core compute engine (layer addition/replacement)
    AcesStateResolver resolver(data->import_state, data->export_state);
    ComputeEmissions(data->config, resolver, nx, ny, nz);

    // Lazily initialize persistent DualViews for physics plugins and export state.
    // This avoids per-timestep allocation overhead.
    if (data->export_state.total_nox_emissions.view_host().data() == nullptr) {
        data->export_state.total_nox_emissions =
            GetDualView(exportState, "total_nox_emissions", nx, ny, nz);
    }

    auto& imp = data->import_state;
    auto& exp = data->export_state;

    // Run active physics plugins (Sea Salt, Dust, etc.)
    for (auto& scheme : data->active_schemes) {
        scheme->Run(imp, exp);
    }

    // Sync results back to host space so the ESMF framework can see updated field data.
    if (exp.total_nox_emissions.view_host().data()) {
        exp.total_nox_emissions.sync<Kokkos::HostSpace>();
    }

    if (rc) *rc = ESMF_SUCCESS;
}

/**
 * @brief Internal implementation of Finalize phase.
 */
void Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc) {
    bool kokkos_initialized_here = false;
    if (comp.ptr != nullptr) {
        int rc_internal;
        void* data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
        if (data_ptr) {
            auto data = static_cast<AcesInternalData*>(data_ptr);
            kokkos_initialized_here = data->kokkos_initialized_here;
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
    if (rc) *rc = ESMF_SUCCESS;
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, ACES_Initialize, 1);
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_RUN, ACES_Run, 1);
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_FINALIZE, ACES_Finalize, 1);
    std::cout << "ACES_SetServices: Services set." << std::endl;
}

}  // extern "C"
