#include <Kokkos_Core.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <set>

#include "aces/aces.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_internal.hpp"
#include "aces/aces_utils.hpp"
#include "aces/aces_state.hpp"
#include "aces/aces_physics_factory.hpp"

extern "C" {
void ACES_Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                    ESMC_Clock* clock, int* rc) {
    aces::Initialize(comp, importState, exportState, clock, rc);
}

void ACES_Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
             ESMC_Clock* clock, int* rc) {
    aces::Run(comp, importState, exportState, clock, rc);
}

void ACES_Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                  ESMC_Clock* clock, int* rc) {
    aces::Finalize(comp, importState, exportState, clock, rc);
}
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
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> host_view("host_" + name, nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view("device_" + name, nx, ny, nz);
    return DualView3D(device_view, host_view);
}

extern "C" {

void aces_core_advertise(void* importState_ptr, void* exportState_ptr, int* rc) {
    if (rc != nullptr) *rc = 0;
    AcesConfig config;
    try {
        config = ParseConfig("aces_config.yaml");
    } catch (...) {
        return;
    }
    ESMC_State importState = {importState_ptr};
    if (importState.ptr != nullptr) {
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
    ESMC_State exportState = {exportState_ptr};
    if (exportState.ptr != nullptr) {
        for (auto const& [species, layers] : config.species_layers) {
            NUOPC_Advertise(exportState, species.c_str(), species.c_str());
        }
        NUOPC_Advertise(exportState, "aces_discovery_emissions", "aces_discovery_emissions");
    }
}

void aces_core_initialize(void** data_out, void* importState_ptr, void* exportState_ptr,
                         void* clock_ptr, int* rc) {
    if (rc != nullptr) *rc = 0;
    bool kokkos_initialized_here = false;
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize();
        kokkos_initialized_here = true;
    }
    auto* data = new AcesInternalData();
    data->kokkos_initialized_here = kokkos_initialized_here;
    try {
        data->config = ParseConfig("aces_config.yaml");
    } catch (...) {
        std::cerr << "aces_core_initialize: Error loading aces_config.yaml\n";
        if (rc != nullptr) *rc = -1;
        delete data;
        return;
    }
    data->diagnostic_manager = std::make_unique<AcesDiagnosticManager>();
    data->stacking_engine = std::make_unique<StackingEngine>(data->config);
    for (const auto& scheme_config : data->config.physics_schemes) {
        auto scheme = PhysicsFactory::CreateScheme(scheme_config);
        if (scheme) {
            scheme->Initialize(scheme_config.options, data->diagnostic_manager.get());
            data->active_schemes.push_back(std::move(scheme));
        }
    }
    if (data_out) *data_out = static_cast<void*>(data);
}

void aces_core_run(void* data_ptr, void* importState_ptr, void* exportState_ptr,
                  void* clock_ptr, int* rc) {
    if (rc != nullptr) *rc = 0;
    if (!data_ptr) return;
    auto* data = static_cast<AcesInternalData*>(data_ptr);
    ESMC_State importState = {importState_ptr};
    ESMC_State exportState = {exportState_ptr};
    ESMC_Clock clock = {clock_ptr};
    if (data->nx == 0) {
        ESMC_Field field; field.ptr = nullptr;
        if (!data->config.species_layers.empty()) {
            std::string ref_field_name = data->config.species_layers.begin()->first;
            ESMC_StateGetField(exportState, ref_field_name.c_str(), &field);
        }
        if (field.ptr == nullptr) ESMC_StateGetField(exportState, "aces_discovery_emissions", &field);
        if (field.ptr != nullptr) {
            std::array<int, 3> lbound = {1, 1, 1}; std::array<int, 3> ubound = {1, 1, 1};
            int localDe = 0;
            if (ESMC_FieldGetBounds(field, &localDe, lbound.data(), ubound.data(), 3) == ESMF_SUCCESS) {
                data->nx = ubound[0] - lbound[0] + 1; data->ny = ubound[1] - lbound[1] + 1; data->nz = ubound[2] - lbound[2] + 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound.data(), ubound.data(), 2) == ESMF_SUCCESS) {
                data->nx = ubound[0] - lbound[0] + 1; data->ny = ubound[1] - lbound[1] + 1; data->nz = 1;
            }
        }
        if (data->nx <= 0) { data->nx = 360; data->ny = 180; data->nz = 1; }
    }
    int nx = data->nx, ny = data->ny, nz = data->nz;
    for (auto const& [species, layers] : data->config.species_layers) {
        if (data->export_state.fields.find(species) == data->export_state.fields.end()) {
            data->export_state.fields.try_emplace(species, GetDualView(exportState.ptr, species, nx, ny, nz));
        }
    }
    if (data->default_mask.extent(0) != static_cast<size_t>(nx)) {
        data->default_mask = Kokkos::View<double***, Kokkos::LayoutLeft>("default_mask", nx, ny, nz);
        Kokkos::deep_copy(data->default_mask, 1.0);
    }
    if (data->esmf_fields.empty()) {
        std::set<std::string> esmf_fields_set;
        std::set<std::string> cdeps_fields;
        for (const auto& s : data->config.cdeps_config.streams) for (const auto& v : s.variables) cdeps_fields.insert(v.name_in_model);
        auto resolve_name = [&](const std::string& name) {
            auto it = data->config.met_mapping.find(name); if (it != data->config.met_mapping.end()) return it->second;
            it = data->config.scale_factor_mapping.find(name); if (it != data->config.scale_factor_mapping.end()) return it->second;
            it = data->config.mask_mapping.find(name); if (it != data->config.mask_mapping.end()) return it->second;
            return name;
        };
        for (auto const& [species, layers] : data->config.species_layers) {
            for (const auto& layer : layers) {
                if (cdeps_fields.find(resolve_name(layer.field_name)) == cdeps_fields.end()) esmf_fields_set.insert(layer.field_name);
                for (const auto& sf : layer.scale_fields) if (cdeps_fields.find(resolve_name(sf)) == cdeps_fields.end()) esmf_fields_set.insert(sf);
                for (const auto& m : layer.masks) if (cdeps_fields.find(resolve_name(m)) == cdeps_fields.end()) esmf_fields_set.insert(m);
            }
        }
        if (data->config.vertical_config.type != VerticalCoordType::NONE) {
            esmf_fields_set.insert(data->config.vertical_config.p_surf_field);
            if (data->config.vertical_config.type == VerticalCoordType::FV3) {
                esmf_fields_set.insert(data->config.vertical_config.ak_field); esmf_fields_set.insert(data->config.vertical_config.bk_field);
            } else if (data->config.vertical_config.type == VerticalCoordType::MPAS || data->config.vertical_config.type == VerticalCoordType::WRF) {
                esmf_fields_set.insert(data->config.vertical_config.z_field);
            }
            esmf_fields_set.insert(data->config.vertical_config.pbl_field);
        }
        data->esmf_fields.assign(esmf_fields_set.begin(), esmf_fields_set.end());
        for (const auto& internal_name : data->esmf_fields) data->external_esmf_fields.push_back(resolve_name(internal_name));
    }
    for (size_t i = 0; i < data->esmf_fields.size(); ++i) {
        const std::string& internal_name = data->esmf_fields[i]; const std::string& external_name = data->external_esmf_fields[i];
        if (data->import_state.fields.find(internal_name) == data->import_state.fields.end()) {
            data->import_state.fields.try_emplace(internal_name, GetDualView(importState.ptr, external_name, nx, ny, nz));
        }
    }
    data->ingestor.IngestMeteorology(importState, data->external_esmf_fields, data->import_state, nx, ny, nz);
    data->ingestor.IngestEmissionsInline(data->config.cdeps_config, data->import_state, nx, ny, nz);
    int hour = 0, dow = 0;
    if (clock.ptr) {
        ESMC_TimeInterval currSimTime; ESMC_I8 advanceCount; ESMC_ClockGet(clock, &currSimTime, &advanceCount);
        ESMC_I8 seconds_i8; ESMC_TimeIntervalGet(currSimTime, &seconds_i8, nullptr);
        hour = static_cast<int>((seconds_i8 / 3600) % 24); dow = static_cast<int>((seconds_i8 / 86400) % 7);
    }
    AcesStateResolver resolver(data->import_state, data->export_state, data->config.met_mapping, data->config.scale_factor_mapping, data->config.mask_mapping);
    data->stacking_engine->Execute(resolver, nx, ny, nz, data->default_mask, hour, dow);
    for (auto& scheme : data->active_schemes) scheme->Run(data->import_state, data->export_state);
    if (clock.ptr) {
        ESMC_Field template_field; template_field.ptr = nullptr;
        if (!data->config.species_layers.empty()) {
            std::string ref_field_name = data->config.species_layers.begin()->first; ESMC_StateGetField(exportState, ref_field_name.c_str(), &template_field);
        }
        data->diagnostic_manager->WriteDiagnostics(data->config.diagnostics, clock, template_field, data->export_state, exportState);
    }
    for (auto& [name, dv] : data->export_state.fields) if (dv.view_host().data() != nullptr) dv.sync<Kokkos::HostSpace>();
}

void aces_core_finalize(void* data_ptr, int* rc) {
    if (rc != nullptr) *rc = 0;
    if (!data_ptr) return;
    auto* data = static_cast<AcesInternalData*>(data_ptr);
    data->import_state.fields.clear(); data->export_state.fields.clear(); data->default_mask = {};
    for (auto& scheme : data->active_schemes) if (auto* base = dynamic_cast<BasePhysicsScheme*>(scheme.get())) base->ClearPhysicsCache();
    data->active_schemes.clear();
    bool kh = data->kokkos_initialized_here; delete data;
    if (kh && Kokkos::is_initialized()) Kokkos::finalize();
}

} // extern "C"

void Initialize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
                ESMC_Clock* clock, int* rc) {
    void* data_ptr = nullptr;
    aces_core_initialize(&data_ptr, importState.ptr, exportState.ptr, (clock ? clock->ptr : nullptr), rc);
    if (rc && *rc == 0 && comp.ptr) ESMC_GridCompSetInternalState(comp, data_ptr);
}

void Run(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
         ESMC_Clock* clock, int* rc) {
    int rc_internal; void* data_ptr = nullptr;
    if (comp.ptr) data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
    aces_core_run(data_ptr, importState.ptr, exportState.ptr, (clock ? clock->ptr : nullptr), rc);
}

void Finalize(ESMC_GridComp comp, ESMC_State importState, ESMC_State exportState,
              ESMC_Clock* clock, int* rc) {
    int rc_internal; void* data_ptr = nullptr;
    if (comp.ptr) data_ptr = ESMC_GridCompGetInternalState(comp, &rc_internal);
    aces_core_finalize(data_ptr, rc);
}

} // namespace aces
