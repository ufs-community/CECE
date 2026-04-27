/**
 * @file cece_ccpp_api.cpp
 * @brief Implementation of the CCPP C-linkage API for CECE.
 *
 * Provides per-scheme initialization, execution, finalization, and field
 * marshalling functions that Fortran CCPP driver schemes call via
 * iso_c_binding. These operate on the existing CeceInternalData structure
 * but provide per-scheme granularity needed by the CCPP framework.
 *
 * Requirements: 3.1, 3.2, 3.5, 9.1-9.6, 14.1, 14.2, 14.3
 */

#include "cece/cece_ccpp_api.h"

#include <Kokkos_Core.hpp>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#include "cece/cece_config.hpp"
#include "cece/cece_diagnostics.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_physics_factory.hpp"
#include "cece/cece_stacking_engine.hpp"
#include "cece/physics_scheme.hpp"

extern "C" {

void cece_ccpp_core_init(void** data_ptr, const char* config_path, int config_path_len, int nx, int ny, int nz, int* rc) {
    if (rc != nullptr) {
        *rc = 0;
    }

    std::cout << "INFO: CECE CCPP core_init — starting initialization" << std::endl;

    // 1. Conditionally initialize Kokkos
    bool kokkos_initialized_here = false;
    if (!Kokkos::is_initialized()) {
        std::cout << "INFO: Initializing Kokkos execution space" << std::endl;

        Kokkos::InitializationSettings args;

#ifdef KOKKOS_ENABLE_OPENMP
        const char* num_threads = std::getenv("OMP_NUM_THREADS");
        if (num_threads != nullptr) {
            int threads = std::atoi(num_threads);
            if (threads > 0) {
                args.set_num_threads(threads);
            }
        }
#endif

#ifdef KOKKOS_ENABLE_CUDA
        const char* device_id = std::getenv("CECE_DEVICE_ID");
        if (device_id != nullptr) {
            int dev_id = std::atoi(device_id);
            if (dev_id >= 0) {
                args.set_device_id(dev_id);
            }
        }
#endif

#ifdef KOKKOS_ENABLE_HIP
        const char* device_id = std::getenv("CECE_DEVICE_ID");
        if (device_id != nullptr) {
            int dev_id = std::atoi(device_id);
            if (dev_id >= 0) {
                args.set_device_id(dev_id);
            }
        }
#endif

        Kokkos::initialize(args);
        kokkos_initialized_here = true;
        std::cout << "INFO: Kokkos initialized — execution space: " << Kokkos::DefaultExecutionSpace::name() << std::endl;
    } else {
        std::cout << "INFO: Kokkos already initialized — reusing existing instance" << std::endl;
    }

    // 2. Build config path string from Fortran-style (ptr + length)
    std::string cfg_path;
    if (config_path != nullptr && config_path_len > 0) {
        cfg_path.assign(config_path, static_cast<size_t>(config_path_len));
    } else {
        cfg_path = "cece_config.yaml";  // default per Req 14.2
    }

    std::cout << "INFO: Parsing configuration: " << cfg_path << std::endl;

    // 3. Parse YAML configuration
    cece::CeceConfig config;
    try {
        config = cece::ParseConfig(cfg_path);
        std::cout << "INFO: Configuration parsed — " << config.species_layers.size() << " species, " << config.physics_schemes.size()
                  << " physics schemes" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR in cece_ccpp_core_init: Failed to parse config '" << cfg_path << "': " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    } catch (...) {
        std::cerr << "ERROR in cece_ccpp_core_init: Unknown error parsing config '" << cfg_path << "'" << std::endl;
        if (rc != nullptr) *rc = -1;
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // 4. Allocate CeceInternalData on the heap
    auto* internal_data = new cece::CeceInternalData();
    internal_data->config = config;
    internal_data->kokkos_initialized_here = kokkos_initialized_here;
    internal_data->nx = nx;
    internal_data->ny = ny;
    internal_data->nz = nz;

    // Populate unique_input_fields
    std::set<std::string> unique_fields;
    for (const auto& [species_name, layers] : config.species_layers) {
        for (const auto& layer : layers) {
            if (!layer.field_name.empty()) {
                unique_fields.insert(layer.field_name);
            }
            for (const auto& sf : layer.scale_fields) {
                unique_fields.insert(sf);
            }
            for (const auto& m : layer.masks) {
                unique_fields.insert(m);
            }
        }
    }
    internal_data->unique_input_fields.assign(unique_fields.begin(), unique_fields.end());

    // 5. Initialize PhysicsFactory — instantiate all configured physics schemes
    std::cout << "INFO: Initializing physics schemes" << std::endl;
    for (const auto& scheme_config : config.physics_schemes) {
        try {
            auto scheme = cece::PhysicsFactory::CreateScheme(scheme_config);
            if (scheme == nullptr) {
                std::cerr << "ERROR: Physics scheme '" << scheme_config.name << "' not registered in factory" << std::endl;
                if (rc != nullptr) *rc = -1;
                delete internal_data;
                if (kokkos_initialized_here && Kokkos::is_initialized()) {
                    Kokkos::finalize();
                }
                return;
            }
            scheme->Initialize(scheme_config.options, nullptr);
            internal_data->active_schemes.push_back(std::move(scheme));
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed to initialize physics scheme '" << scheme_config.name << "': " << e.what() << std::endl;
            if (rc != nullptr) *rc = -1;
            delete internal_data;
            if (kokkos_initialized_here && Kokkos::is_initialized()) {
                Kokkos::finalize();
            }
            return;
        }
    }
    std::cout << "INFO: Initialized " << internal_data->active_schemes.size() << " physics schemes" << std::endl;

    // 6. Initialize StackingEngine
    try {
        internal_data->stacking_engine = std::make_unique<cece::StackingEngine>(config);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to initialize StackingEngine: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
        delete internal_data;
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // 7. Initialize DiagnosticManager
    try {
        internal_data->diagnostic_manager = std::make_unique<cece::CeceDiagnosticManager>();
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to initialize DiagnosticManager: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
        delete internal_data;
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // Return the opaque pointer
    if (data_ptr != nullptr) {
        *data_ptr = internal_data;
    }

    std::cout << "INFO: CECE CCPP core_init completed (nx=" << nx << ", ny=" << ny << ", nz=" << nz << ")" << std::endl;
}

void cece_ccpp_scheme_init(void* data_ptr, const char* scheme_name, int scheme_name_len, int nx, int ny, int nz, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr || scheme_name == nullptr) {
        std::cerr << "ERROR in cece_ccpp_scheme_init: null argument" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    std::string name(scheme_name, static_cast<size_t>(scheme_name_len));

    try {
        // Find the scheme by matching against config names (same order as active_schemes)
        const auto& scheme_configs = internal_data->config.physics_schemes;
        bool found = false;
        for (size_t i = 0; i < scheme_configs.size(); ++i) {
            if (scheme_configs[i].name == name) {
                if (i >= internal_data->active_schemes.size() || internal_data->active_schemes[i] == nullptr) {
                    std::cerr << "ERROR in cece_ccpp_scheme_init: scheme '" << name << "' exists in config but not in active_schemes" << std::endl;
                    if (rc != nullptr) *rc = -2;
                    return;
                }
                found = true;
                std::cout << "INFO: cece_ccpp_scheme_init — scheme '" << name << "' verified ready (nx=" << nx << ", ny=" << ny << ", nz=" << nz
                          << ")" << std::endl;
                break;
            }
        }

        if (!found) {
            std::cerr << "ERROR in cece_ccpp_scheme_init: scheme '" << name << "' not found in active_schemes" << std::endl;
            if (rc != nullptr) *rc = -3;
            return;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR in cece_ccpp_scheme_init: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    } catch (...) {
        std::cerr << "ERROR in cece_ccpp_scheme_init: unknown exception" << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

void cece_ccpp_scheme_run(void* data_ptr, const char* scheme_name, int scheme_name_len, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr || scheme_name == nullptr) {
        std::cerr << "ERROR in cece_ccpp_scheme_run: null argument" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    std::string name(scheme_name, static_cast<size_t>(scheme_name_len));

    try {
        // Find the scheme by matching against config names (same order as active_schemes)
        const auto& scheme_configs = internal_data->config.physics_schemes;
        cece::PhysicsScheme* scheme_ptr = nullptr;
        for (size_t i = 0; i < scheme_configs.size(); ++i) {
            if (scheme_configs[i].name == name) {
                if (i < internal_data->active_schemes.size()) {
                    scheme_ptr = internal_data->active_schemes[i].get();
                }
                break;
            }
        }

        if (scheme_ptr == nullptr) {
            std::cerr << "ERROR in cece_ccpp_scheme_run: scheme '" << name << "' not found in active_schemes" << std::endl;
            if (rc != nullptr) *rc = -3;
            return;
        }

        // Execute the scheme
        scheme_ptr->Run(internal_data->import_state, internal_data->export_state);

        // Fence after execution to ensure device kernels complete
        Kokkos::fence();
    } catch (const std::exception& e) {
        std::cerr << "ERROR in cece_ccpp_scheme_run: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    } catch (...) {
        std::cerr << "ERROR in cece_ccpp_scheme_run: unknown exception" << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

void cece_ccpp_scheme_finalize(void* data_ptr, const char* scheme_name, int scheme_name_len, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr || scheme_name == nullptr) {
        std::cerr << "ERROR in cece_ccpp_scheme_finalize: null argument" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    std::string name(scheme_name, static_cast<size_t>(scheme_name_len));

    try {
        // Find the scheme by matching against config names (same order as active_schemes)
        const auto& scheme_configs = internal_data->config.physics_schemes;
        cece::PhysicsScheme* scheme_ptr = nullptr;
        for (size_t i = 0; i < scheme_configs.size(); ++i) {
            if (scheme_configs[i].name == name) {
                if (i < internal_data->active_schemes.size()) {
                    scheme_ptr = internal_data->active_schemes[i].get();
                }
                break;
            }
        }

        if (scheme_ptr == nullptr) {
            std::cerr << "ERROR in cece_ccpp_scheme_finalize: scheme '" << name << "' not found in active_schemes" << std::endl;
            if (rc != nullptr) *rc = -3;
            return;
        }

        // Finalize the scheme
        scheme_ptr->Finalize();
    } catch (const std::exception& e) {
        std::cerr << "ERROR in cece_ccpp_scheme_finalize: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    } catch (...) {
        std::cerr << "ERROR in cece_ccpp_scheme_finalize: unknown exception" << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

void cece_ccpp_set_import_field(void* data_ptr, const char* field_name, int name_len, const double* field_data, int nx, int ny, int nz, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr || field_name == nullptr || field_data == nullptr) {
        std::cerr << "ERROR in cece_ccpp_set_import_field: null argument" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    std::string name(field_name, static_cast<size_t>(name_len));

    const size_t snx = static_cast<size_t>(nx);
    const size_t sny = static_cast<size_t>(ny);
    const size_t snz = static_cast<size_t>(nz);

    // Look up or create the DualView3D in import_state
    auto it = internal_data->import_state.fields.find(name);
    if (it != internal_data->import_state.fields.end()) {
        // Existing field — validate dimensions match
        auto& dv = it->second;
        if (dv.extent(0) != snx || dv.extent(1) != sny || dv.extent(2) != snz) {
            std::cerr << "ERROR in cece_ccpp_set_import_field: dimension mismatch for '" << name << "'. Expected " << dv.extent(0) << "x"
                      << dv.extent(1) << "x" << dv.extent(2) << ", got " << nx << "x" << ny << "x" << nz << std::endl;
            if (rc != nullptr) *rc = -2;
            return;
        }
    } else {
        // Create a new DualView3D with the given dimensions
        cece::DualView3D dv("import_" + name, snx, sny, snz);
        internal_data->import_state.fields.emplace(name, std::move(dv));
    }

    // Copy Fortran array data into the host view
    auto& dv = internal_data->import_state.fields[name];
    double* host_ptr = dv.view_host().data();
    const size_t total_bytes = snx * sny * snz * sizeof(double);
    std::memcpy(host_ptr, field_data, total_bytes);

    // Mark host as modified and sync to device
    dv.modify_host();
    dv.sync_device();
}

void cece_ccpp_get_export_field(void* data_ptr, const char* field_name, int name_len, double* field_data, int nx, int ny, int nz, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr || field_name == nullptr || field_data == nullptr) {
        std::cerr << "ERROR in cece_ccpp_get_export_field: null argument" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);
    std::string name(field_name, static_cast<size_t>(name_len));

    // Look up the field in export_state
    auto it = internal_data->export_state.fields.find(name);
    if (it == internal_data->export_state.fields.end()) {
        std::cerr << "ERROR in cece_ccpp_get_export_field: field '" << name << "' not found in export state" << std::endl;
        if (rc != nullptr) *rc = -3;
        return;
    }

    auto& dv = it->second;
    const size_t snx = static_cast<size_t>(nx);
    const size_t sny = static_cast<size_t>(ny);
    const size_t snz = static_cast<size_t>(nz);

    // Validate dimensions match
    if (dv.extent(0) != snx || dv.extent(1) != sny || dv.extent(2) != snz) {
        std::cerr << "ERROR in cece_ccpp_get_export_field: dimension mismatch for '" << name << "'. DualView is " << dv.extent(0) << "x"
                  << dv.extent(1) << "x" << dv.extent(2) << ", requested " << nx << "x" << ny << "x" << nz << std::endl;
        if (rc != nullptr) *rc = -2;
        return;
    }

    // Sync device data to host
    dv.sync_host();

    // Copy host view data into the caller's Fortran array
    const double* host_ptr = dv.view_host().data();
    const size_t total_bytes = snx * sny * snz * sizeof(double);
    std::memcpy(field_data, host_ptr, total_bytes);
}

void cece_ccpp_run_stacking(void* data_ptr, int hour, int day_of_week, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR in cece_ccpp_run_stacking: null data_ptr" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    try {
        // If config has no species layers, skip stacking (Req 6.4)
        if (internal_data->config.species_layers.empty()) {
            std::cout << "INFO: cece_ccpp_run_stacking — no species layers, skipping" << std::endl;
            return;
        }

        if (!internal_data->stacking_engine) {
            std::cerr << "ERROR in cece_ccpp_run_stacking: stacking_engine is null" << std::endl;
            if (rc != nullptr) *rc = -2;
            return;
        }

        // Create a CeceStateResolver (mirrors cece_core_run.cpp pattern)
        cece::CeceStateResolver resolver(internal_data->import_state, internal_data->export_state, internal_data->config.met_mapping,
                                         internal_data->config.scale_factor_mapping, internal_data->config.mask_mapping);

        // Execute the stacking engine with temporal parameters (Req 6.1, 6.2)
        internal_data->stacking_engine->Execute(resolver, internal_data->nx, internal_data->ny, internal_data->nz, internal_data->default_mask, hour,
                                                day_of_week);

        // Fence to ensure all device operations complete
        Kokkos::fence("CECE::CCPP::RunStacking");

    } catch (const std::exception& e) {
        std::cerr << "ERROR in cece_ccpp_run_stacking: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    } catch (...) {
        std::cerr << "ERROR in cece_ccpp_run_stacking: unknown exception" << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

void cece_ccpp_core_finalize(void* data_ptr, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR in cece_ccpp_core_finalize: null data_ptr" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    try {
        // Fence before cleanup to ensure all device work is complete
        Kokkos::fence("CECE::CCPP::Finalize::PreCleanup");

        std::cout << "INFO: CECE CCPP core_finalize — beginning cleanup" << std::endl;

        // Finalize all physics schemes (mirrors cece_core_finalize.cpp)
        std::cout << "INFO: Finalizing " << internal_data->active_schemes.size() << " physics schemes" << std::endl;
        for (auto& scheme : internal_data->active_schemes) {
            if (scheme) {
                try {
                    scheme->Finalize();
                } catch (const std::exception& e) {
                    std::cerr << "WARNING: Physics scheme Finalize threw: " << e.what() << std::endl;
                }
            }
        }

        // Track whether we need to finalize Kokkos (Req 8.2, 8.3)
        bool should_finalize_kokkos = internal_data->kokkos_initialized_here;

        // Final fence before deletion
        Kokkos::fence("CECE::CCPP::Finalize::PreDelete");

        // Release CeceInternalData (Req 8.1)
        delete internal_data;
        std::cout << "INFO: CeceInternalData deleted" << std::endl;

        // Conditionally finalize Kokkos only if we initialized it
        if (should_finalize_kokkos && Kokkos::is_initialized()) {
            Kokkos::finalize();
            std::cout << "INFO: Kokkos finalized (initialized by CECE CCPP)" << std::endl;
        } else {
            std::cout << "INFO: Skipping Kokkos finalization (not owned by CECE CCPP)" << std::endl;
        }

        std::cout << "INFO: CECE CCPP core_finalize completed" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "ERROR in cece_ccpp_core_finalize: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    } catch (...) {
        std::cerr << "ERROR in cece_ccpp_core_finalize: unknown exception" << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

void cece_ccpp_sync_import(void* data_ptr, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR in cece_ccpp_sync_import: null data_ptr" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    try {
        // Iterate all fields in import_state and sync each to device (Req 12.1)
        for (auto& [name, field] : internal_data->import_state.fields) {
            field.sync_device();
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR in cece_ccpp_sync_import: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    } catch (...) {
        std::cerr << "ERROR in cece_ccpp_sync_import: unknown exception" << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

void cece_ccpp_sync_export_to_host(void* data_ptr, int* rc) {
    if (rc != nullptr) *rc = 0;

    if (data_ptr == nullptr) {
        std::cerr << "ERROR in cece_ccpp_sync_export_to_host: null data_ptr" << std::endl;
        if (rc != nullptr) *rc = -1;
        return;
    }

    auto* internal_data = static_cast<cece::CeceInternalData*>(data_ptr);

    try {
        // Iterate all fields in export_state and sync each to host (Req 12.2)
        for (auto& [name, field] : internal_data->export_state.fields) {
            field.sync_host();
        }

        // Fence to ensure all device-to-host transfers complete
        Kokkos::fence("CECE::CCPP::SyncExportToHost");

    } catch (const std::exception& e) {
        std::cerr << "ERROR in cece_ccpp_sync_export_to_host: " << e.what() << std::endl;
        if (rc != nullptr) *rc = -1;
    } catch (...) {
        std::cerr << "ERROR in cece_ccpp_sync_export_to_host: unknown exception" << std::endl;
        if (rc != nullptr) *rc = -1;
    }
}

}  // extern "C"
