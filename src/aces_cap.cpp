#include "aces/aces.hpp"
#include "aces/aces_state.hpp"
#include "aces/aces_utils.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_compute.hpp"
#include "aces/aces_physics_factory.hpp"
#include "aces/physics_scheme.hpp"
#include <iostream>
#include <vector>
#include <memory>
#include <Kokkos_Core.hpp>

// Forward declarations for mock functions used in standalone driver
extern "C" {
int Mock_ESMC_GridCompSetInternalState(ESMC_GridComp comp, void* data) __attribute__((weak));
void* Mock_ESMC_GridCompGetInternalState(ESMC_GridComp comp, int* rc) __attribute__((weak));
}

namespace aces {

/**
 * @brief Internal data stored in the ESMF GridComp.
 */
struct AcesInternalData {
    AcesConfig config;
    std::vector<std::unique_ptr<PhysicsScheme>> active_schemes;
    AcesImportState import_state;
    AcesExportState export_state;
    bool kokkos_initialized_here = false;
};

/**
 * @brief Helper to wrap internal state access to support both real ESMF and mock drivers.
 */
static int InternalSetState(ESMC_GridComp comp, void* data) {
    if (Mock_ESMC_GridCompSetInternalState) {
        return Mock_ESMC_GridCompSetInternalState(comp, data);
    }
    return ESMC_GridCompSetInternalState(comp, data);
}

static void* InternalGetState(ESMC_GridComp comp, int* rc) {
    if (Mock_ESMC_GridCompGetInternalState) {
        return Mock_ESMC_GridCompGetInternalState(comp, rc);
    }
    return ESMC_GridCompGetInternalState(comp, rc);
}

/**
 * @brief ESMF implementation of FieldResolver.
 */
class EsmfFieldResolver : public FieldResolver {
    ESMC_State importState;
    ESMC_State exportState;

public:
    EsmfFieldResolver(ESMC_State imp, ESMC_State exp)
        : importState(imp), exportState(exp) {}

    UnmanagedHostView3D ResolveImport(const std::string& name, int nx, int ny, int nz) override {
        ESMC_Field field;
        int rc = ESMC_StateGetField(importState, name.c_str(), &field);
        if (rc != ESMF_SUCCESS) return UnmanagedHostView3D();
        return WrapESMCField(field, nx, ny, nz);
    }

    UnmanagedHostView3D ResolveExport(const std::string& name, int nx, int ny, int nz) override {
        ESMC_Field field;
        int rc = ESMC_StateGetField(exportState, name.c_str(), &field);
        if (rc != ESMF_SUCCESS) return UnmanagedHostView3D();
        return WrapESMCField(field, nx, ny, nz);
    }
};

/**
 * @brief Helper to create a DualView from an ESMF field.
 */
static DualView3D GetDualView(ESMC_State state, const std::string& name, int nx, int ny, int nz) {
    ESMC_Field field;
    int rc = ESMC_StateGetField(state, name.c_str(), &field);
    if (rc != ESMF_SUCCESS) {
        return DualView3D();
    }
    UnmanagedHostView3D host_view = WrapESMCField(field, nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view("device_" + name, nx, ny, nz);
    return DualView3D(device_view, host_view);
}

void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
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

        for (const auto& scheme_config : data->config.physics_schemes) {
            data->active_schemes.push_back(PhysicsFactory::CreateScheme(scheme_config));
        }

        InternalSetState(comp, data);
    }

    if (rc) *rc = ESMF_SUCCESS;
}

void Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    std::cout << "ACES_Run: Executing." << std::endl;

    if (comp.ptr == nullptr) {
        if (rc) *rc = ESMF_SUCCESS;
        return;
    }

    int rc_internal;
    void* data_ptr = InternalGetState(comp, &rc_internal);
    if (!data_ptr) {
        std::cerr << "ACES_Run Error: Internal state not found." << std::endl;
        if (rc) *rc = -1;
        return;
    }
    auto data = static_cast<AcesInternalData*>(data_ptr);

    // Dynamic dimension discovery from a representative field
    int nx = 0, ny = 0, nz = 0;
    ESMC_Field field;
    std::string ref_field_name = "total_nox_emissions";
    if (ESMC_StateGetField(exportState, ref_field_name.c_str(), &field) != ESMF_SUCCESS) {
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
        nx = 360; ny = 180; nz = 72;
        std::cerr << "ACES_Run: Warning - Could not discover grid dimensions, using defaults " << nx << "x" << ny << "x" << nz << std::endl;
    }

    // Run core compute engine
    EsmfFieldResolver resolver(importState, exportState);
    ComputeEmissions(data->config, resolver, nx, ny, nz);

    // Lazily initialize persistent DualViews to avoid per-timestep allocation overhead
    if (data->export_state.total_nox_emissions.view_host().data() == nullptr) {
        data->import_state.temperature = GetDualView(importState, "temperature", nx, ny, nz);
        data->import_state.wind_speed_10m = GetDualView(importState, "wind_speed_10m", nx, ny, nz);
        data->import_state.base_anthropogenic_nox = GetDualView(importState, "base_anthropogenic_nox", nx, ny, nz);
        data->export_state.total_nox_emissions = GetDualView(exportState, "total_nox_emissions", nx, ny, nz);
    }

    auto& imp = data->import_state;
    auto& exp = data->export_state;

    // Sync results back to host to propagate back to the ESMF framework
    if (exp.total_nox_emissions.view_host().data()) {
        exp.total_nox_emissions.sync<Kokkos::HostSpace>();
    }

    if (rc) *rc = ESMF_SUCCESS;
}

void Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    bool kokkos_initialized_here = false;
    if (comp.ptr != nullptr) {
        int rc_internal;
        void* data_ptr = InternalGetState(comp, &rc_internal);
        if (data_ptr) {
            auto data = static_cast<AcesInternalData*>(data_ptr);
            kokkos_initialized_here = data->kokkos_initialized_here;
            delete data;
        }
    }

    if (kokkos_initialized_here && Kokkos::is_initialized()) {
        Kokkos::finalize();
        std::cout << "ACES_Finalize: Kokkos finalized." << std::endl;
    }
    if (rc) *rc = ESMF_SUCCESS;
}

} // namespace aces

extern "C" {

void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    aces::Initialize(comp, importState, exportState, clock, rc);
}

void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    aces::Run(comp, importState, exportState, clock, rc);
}

void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock, int* rc) {
    aces::Finalize(comp, importState, exportState, clock, rc);
}

void ACES_SetServices(ESMC_GridComp comp, int* rc) {
    if (rc) *rc = ESMF_SUCCESS;
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_INITIALIZE, ACES_Initialize, 1);
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_RUN, ACES_Run, 1);
    ESMC_GridCompSetEntryPoint(comp, ESMF_METHOD_FINALIZE, ACES_Finalize, 1);
    std::cout << "ACES_SetServices: Services set." << std::endl;
}

} // extern "C"
