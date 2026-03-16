#include <ESMC.h>
#include <NUOPC.h>

#include <Kokkos_Core.hpp>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "aces/aces.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_internal.hpp"
#include "aces/aces_physics_factory.hpp"
#include "aces/aces_state.hpp"
#include "aces/aces_utils.hpp"

// Forward declarations for functions implemented in their own translation units
extern "C" {
void aces_core_run(void* data_ptr, void* importState_ptr, void* exportState_ptr, void* clock_ptr,
                   int* rc);
void aces_core_finalize(void* data_ptr, int* rc);
}

namespace aces {

static DualView3D GetDualView(void* state_ptr, const std::string& name, int nx, int ny, int nz) {
    ESMC_State state = {state_ptr};
    ESMC_Field field;
    if (state_ptr && ESMC_StateGetField(state, name.c_str(), &field) == ESMF_SUCCESS) {
        UnmanagedHostView3D host_view = WrapESMCField(field, nx, ny, nz);
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
            "device_" + name, nx, ny, nz);
        return DualView3D(device_view, host_view);
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> host_view("host_" + name, nx, ny,
                                                                             nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
        "device_" + name, nx, ny, nz);
    return DualView3D(device_view, host_view);
}

void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                ESMC_Clock* clock, int* rc);
void Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
         int* rc);
void Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc);

}  // namespace aces

extern "C" {

// aces_core_advertise is implemented in src/aces_core_advertise.cpp

void aces_core_initialize(void** data_out, void* importState_ptr, void* exportState_ptr,
                          void* clock_ptr, int* rc) {
    if (rc != nullptr) *rc = 0;
    if (data_out != nullptr) *data_out = nullptr;

    bool kokkos_initialized_here = false;
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize();
        kokkos_initialized_here = true;
    }
    auto* data = new aces::AcesInternalData();
    data->kokkos_initialized_here = kokkos_initialized_here;
    try {
        data->config = aces::ParseConfig("aces_config.yaml");
    } catch (...) {
        std::cerr << "aces_core_initialize: Error loading aces_config.yaml\n";
        if (rc != nullptr) *rc = -1;
        delete data;
        return;
    }
    data->diagnostic_manager = std::make_unique<aces::AcesDiagnosticManager>();
    data->stacking_engine = std::make_unique<aces::StackingEngine>(data->config);
    for (const auto& scheme_config : data->config.physics_schemes) {
        auto scheme = aces::PhysicsFactory::CreateScheme(scheme_config);
        if (scheme) {
            scheme->Initialize(scheme_config.options, data->diagnostic_manager.get());
            data->active_schemes.push_back(std::move(scheme));
        }
    }
    if (data_out) *data_out = static_cast<void*>(data);
}

// aces_core_finalize is implemented in src/aces_core_finalize.cpp

void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                     ESMC_Clock* clock, int* rc) {
    void* data_ptr = nullptr;
    aces_core_initialize(&data_ptr, importState.ptr, exportState.ptr,
                         (clock ? clock->ptr : nullptr), rc);
    if (rc && *rc == 0 && comp.ptr) ESMC_GridCompSetInternalState(comp, data_ptr);
}

void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc) {
    int rc_internal;
    void* data_ptr = nullptr;
    if (comp.ptr) data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
    aces_core_run(data_ptr, importState.ptr, exportState.ptr, (clock ? clock->ptr : nullptr), rc);
}

void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                   ESMC_Clock* clock, int* rc) {
    int rc_internal;
    void* data_ptr = nullptr;
    if (comp.ptr) data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
    aces_core_finalize(data_ptr, rc);
}
}

namespace aces {

void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                ESMC_Clock* clock, int* rc) {
    ::ACES_Initialize(comp, importState, exportState, clock, rc);
}

void Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
         int* rc) {
    ::ACES_Run(comp, importState, exportState, clock, rc);
}

void Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState, ESMC_Clock* clock,
              int* rc) {
    ::ACES_Finalize(comp, importState, exportState, clock, rc);
}

}  // namespace aces
