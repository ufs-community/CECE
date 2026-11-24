/**
 * @file cece_core_initialize_p1.cpp
 * @brief Implementation of NUOPC Initialize Phase 1 (IPDv00p1) for CECE.
 *
 * Phase 1 of initialization handles core component setup:
 * - Initialize Kokkos if not already initialized
 * - Parse YAML configuration
 * - Allocate CeceInternalData structure to persist state across phases
 * - Initialize PhysicsFactory and instantiate all physics schemes
 * - Initialize StackingEngine
 * - Initialize DiagnosticManager
 *
 * This phase does NOT:
 * - Initialize TIDE (happens in Phase 2)
 * - Bind to external fields (happens in Phase 2)
 * - Access import/export states (happens in Phase 2)
 *
 * @note This is a C++ bridge function called from the driver.
 * @note The CeceInternalData pointer is returned and must be passed to Phase 2.
 *
 * Requirements: 4.7-4.10, 4.18, 4.19
 */

#include <Kokkos_Core.hpp>
#include <conf/value.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>

#include "cece/cece_config.hpp"
#include "cece/cece_config_path.hpp"
#include "cece/cece_diagnostics.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_physics_factory.hpp"
#include "cece/cece_stacking_engine.hpp"
#include "cece/physics_scheme.hpp"

namespace fs = std::filesystem;

using namespace std::literals::string_literals;

extern "C" {

/**
 * @brief Initialize Phase 1 (IPDv00p1) implementation for CECE.
 *
 * This function performs core initialization that does not depend on other
 * components or field realization. It sets up the internal data structures
 * and prepares CECE for field binding in Phase 2.
 *
 * Operations Performed:
 * 1. Initialize Kokkos if not already initialized (track ownership)
 * 2. Parse cece_config.yaml configuration file
 * 3. Allocate CeceInternalData structure for persistent state
 * 4. Initialize PhysicsFactory and instantiate all configured physics schemes
 * 5. Initialize StackingEngine with configuration
 * 6. Initialize DiagnosticManager
 *
 * @param data_ptr_ptr Pointer to void* that will receive the CeceInternalData pointer
 * @param rc Return code (0 = success, non-zero = error)
 *
 * @note The data_ptr_ptr is a double pointer because we need to allocate and return
 *       a new pointer to the caller (Fortran cap).
 * @note Kokkos initialization is tracked - if CECE initializes Kokkos, it will
 *       finalize it. If Kokkos was already initialized, CECE will not finalize it.
 * @note All physics schemes are instantiated and their Initialize methods are called.
 * @note Configuration parsing errors result in rc = -1 and early return.
 *
 * Requirements: 4.7-4.10, 4.18, 4.19
 */
void cece_core_initialize_p1(void** data_ptr_ptr, int* rc) {
    // Initialize return code to success
    if (rc != nullptr) {
        *rc = 0;
    }

    CECE_LOG_INFO("INFO: CECE Initialize Phase 1 (IPDv00p1) - Core initialization\n");

    // 1. Initialize Kokkos if not already initialized
    bool kokkos_initialized_here = false;
    if (!Kokkos::is_initialized()) {
        CECE_LOG_INFO("INFO: Initializing Kokkos execution space\n");

        Kokkos::InitializationSettings args;

        // Configure OpenMP threads if enabled
#ifdef KOKKOS_ENABLE_OPENMP
        const char* num_threads = std::getenv("OMP_NUM_THREADS");
        if (num_threads != nullptr) {
            char* end;
            long threads = std::strtol(num_threads, &end, 10);
            if (end != num_threads && *end == '\0' && threads > 0) {
                if (threads > std::numeric_limits<int>::max()) {
                    CECE_LOG_WARNING("INFO: OMP_NUM_THREADS value "s + std::to_string(threads) + " exceeds INT_MAX; "s + "clamping to "s +
                                     std::to_string(std::numeric_limits<int>::max()) + "\n"s);
                    threads = std::numeric_limits<int>::max();
                }
                args.set_num_threads(static_cast<int>(threads));
                CECE_LOG_INFO("INFO: Setting OpenMP threads to "s + std::to_string(threads) + "\n"s);
            }
        } else {
            // Default to all available threads if not specified
            CECE_LOG_INFO("INFO: OMP_NUM_THREADS not set - using all available threads\n");
        }
#endif

        // Configure GPU device ID if enabled
#ifdef KOKKOS_ENABLE_CUDA
        const char* device_id = std::getenv("CECE_DEVICE_ID");
        if (device_id != nullptr) {
            char* end;
            long dev_id = std::strtol(device_id, &end, 10);
            if (end != device_id && *end == '\0' && dev_id >= 0) {
                if (dev_id > std::numeric_limits<int>::max()) {
                    CECE_LOG_WARNING("WARNING: CECE_DEVICE_ID value "s + std::to_string(dev_id) + " exceeds INT_MAX - "s +
                                     "using default CUDA device (0)\n"s);
                } else {
                    args.set_device_id(static_cast<int>(dev_id));
                    CECE_LOG_INFO("INFO: Setting CUDA device ID to "s + std::to_string(dev_id) + "\n"s);
                }
            }
        } else {
            CECE_LOG_INFO("INFO: CECE_DEVICE_ID not set - using default CUDA device (0)\n");
        }
#endif

#ifdef KOKKOS_ENABLE_HIP
        const char* device_id = std::getenv("CECE_DEVICE_ID");
        if (device_id != nullptr) {
            char* end;
            long dev_id = std::strtol(device_id, &end, 10);
            if (end != device_id && *end == '\0' && dev_id >= 0) {
                if (dev_id > std::numeric_limits<int>::max()) {
                    CECE_LOG_WARNING("WARNING: CECE_DEVICE_ID value "s + std::to_string(dev_id) + " exceeds INT_MAX - "s +
                                     "using default HIP device (0)\n"s);
                } else {
                    args.set_device_id(static_cast<int>(dev_id));
                    CECE_LOG_INFO("INFO: Setting HIP device ID to "s + std::to_string(dev_id) + "\n"s);
                }
            }
        } else {
            CECE_LOG_INFO("INFO: CECE_DEVICE_ID not set - using default HIP device (0)\n");
        }
#endif

        // Log active execution spaces
        CECE_LOG_INFO("INFO: Active Kokkos execution spaces:\n");
#ifdef KOKKOS_ENABLE_SERIAL
        CECE_LOG_INFO("INFO:   - Serial\n");
#endif
#ifdef KOKKOS_ENABLE_OPENMP
        CECE_LOG_INFO("INFO:   - OpenMP\n");
#endif
#ifdef KOKKOS_ENABLE_CUDA
        CECE_LOG_INFO("INFO:   - CUDA\n");
#endif
#ifdef KOKKOS_ENABLE_HIP
        CECE_LOG_INFO("INFO:   - HIP\n");
#endif

        Kokkos::initialize(args);
        kokkos_initialized_here = true;
        CECE_LOG_INFO("INFO: Kokkos initialized successfully\n");
        CECE_LOG_INFO("INFO: Default execution space: "s + Kokkos::DefaultExecutionSpace::name() + "\n"s);
    } else {
        CECE_LOG_INFO("INFO: Kokkos already initialized - using existing instance\n");
        CECE_LOG_INFO("INFO: Default execution space: "s + Kokkos::DefaultExecutionSpace::name() + "\n"s);
    }

    // 2. Parse YAML configuration
    const fs::path config_path = cece::GetConfigFilePath();
    CECE_LOG_INFO("INFO: Parsing "s + config_path.string() + "\n"s);
    CECE_LOG_DEBUG("DEBUG: Full config path: '"s + config_path.string() + "'\n"s);
    cece::CeceConfig config;
    try {
        config = cece::ParseConfig(config_path);
        CECE_LOG_INFO("INFO: Configuration parsed successfully\n");
        CECE_LOG_INFO("INFO: Found "s + std::to_string(config.species_layers.size()) + " emission species\n"s);
        CECE_LOG_INFO("INFO: Found "s + std::to_string(config.physics_schemes.size()) + " physics schemes\n"s);
    } catch (const std::exception& e) {
        CECE_LOG_ERROR("ERROR in cece_core_initialize_p1: Failed to parse cece_config.yaml: "s + e.what() + "\n"s);
        if (rc != nullptr) {
            *rc = -1;
        }
        // Clean up Kokkos if we initialized it
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    } catch (...) {
        CECE_LOG_ERROR("ERROR in cece_core_initialize_p1: Unknown error parsing cece_config.yaml\n");
        if (rc != nullptr) {
            *rc = -1;
        }
        // Clean up Kokkos if we initialized it
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // 3. Allocate CeceInternalData structure
    CECE_LOG_INFO("INFO: Allocating internal data structure\n");
    auto* internal_data = new cece::CeceInternalData();
    internal_data->config = config;
    internal_data->kokkos_initialized_here = kokkos_initialized_here;

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
    CECE_LOG_INFO("INFO: Found "s + std::to_string(internal_data->unique_input_fields.size()) + " unique input fields required\n"s);

    // 4. Initialize PhysicsFactory and instantiate all physics schemes
    CECE_LOG_INFO("INFO: Initializing physics schemes\n");
    for (const auto& scheme_config : config.physics_schemes) {
        try {
            CECE_LOG_INFO("INFO: Creating physics scheme: "s + scheme_config.name + "\n"s);
            auto scheme = cece::PhysicsFactory::CreateScheme(scheme_config);

            if (scheme == nullptr) {
                CECE_LOG_ERROR("ERROR: Failed to create physics scheme '"s + scheme_config.name + "' - scheme not registered\n"s);
                if (rc != nullptr) {
                    *rc = -1;
                }
                delete internal_data;
                if (kokkos_initialized_here && Kokkos::is_initialized()) {
                    Kokkos::finalize();
                }
                return;
            }

            // Initialize the scheme with its configuration
            // Note: DiagnosticManager will be initialized next, so we pass nullptr for now
            // Schemes will be re-initialized with the diagnostic manager in Phase 2 if needed
            CECE_LOG_INFO("INFO: Initializing physics scheme: "s + scheme_config.name + "\n"s);
            scheme->Initialize(scheme_config.options, nullptr);

            internal_data->active_schemes.push_back(std::move(scheme));
            CECE_LOG_INFO("INFO: Successfully initialized physics scheme: "s + scheme_config.name + "\n"s);
        } catch (const std::exception& e) {
            CECE_LOG_ERROR("ERROR: Failed to initialize physics scheme '"s + scheme_config.name + "': " + e.what() + "\n"s);
            if (rc != nullptr) {
                *rc = -1;
            }
            delete internal_data;
            if (kokkos_initialized_here && Kokkos::is_initialized()) {
                Kokkos::finalize();
            }
            return;
        }
    }

    CECE_LOG_INFO("INFO: Successfully initialized "s + std::to_string(internal_data->active_schemes.size()) + " physics schemes\n"s);

    // 5. Initialize StackingEngine
    CECE_LOG_INFO("INFO: Initializing StackingEngine\n");
    try {
        internal_data->stacking_engine = std::make_unique<cece::StackingEngine>(config);
        CECE_LOG_INFO("INFO: StackingEngine initialized successfully\n");
    } catch (const std::exception& e) {
        CECE_LOG_ERROR("ERROR: Failed to initialize StackingEngine: "s + e.what() + "\n"s);
        if (rc != nullptr) {
            *rc = -1;
        }
        delete internal_data;
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // 6. Initialize DiagnosticManager
    CECE_LOG_INFO("INFO: Initializing DiagnosticManager\n");
    try {
        internal_data->diagnostic_manager = std::make_unique<cece::CeceDiagnosticManager>();
        CECE_LOG_INFO("INFO: DiagnosticManager initialized successfully\n");
    } catch (const std::exception& e) {
        CECE_LOG_ERROR("ERROR: Failed to initialize DiagnosticManager: "s + e.what() + "\n"s);
        if (rc != nullptr) {
            *rc = -1;
        }
        delete internal_data;
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // 7. Setup standalone mode flag if output config is enabled (Req 11.1, 11.4)
    if (config.output_config.enabled) {
        internal_data->standalone_mode = true;
    } else {
        internal_data->standalone_mode = false;
    }

    // 8. Construct CeceClock from parsed configuration
    CECE_LOG_INFO("INFO: Constructing CeceClock for per-component scheduling\n");
    try {
        // Build the vector of ClockComponent from config
        std::vector<cece::ClockComponent> clock_components;

        // Add physics schemes
        for (const auto& scheme : config.physics_schemes) {
            int interval = scheme.refresh_interval_seconds;
            if (interval == 0) {
                interval = config.driver_config.timestep_seconds;
            }
            clock_components.push_back({cece::ComponentType::kPhysicsScheme, scheme.name, interval});
        }

        // Add data streams
        for (const auto& stream : config.cece_data.streams) {
            int interval = stream.refresh_interval_seconds;
            if (interval == 0) {
                interval = config.driver_config.timestep_seconds;
            }
            clock_components.push_back({cece::ComponentType::kDataStream, stream.name, interval});
        }

        // Add stacking engine
        {
            int interval = config.driver_config.stacking_refresh_interval_seconds;
            if (interval == 0) {
                interval = config.driver_config.timestep_seconds;
            }
            clock_components.push_back({cece::ComponentType::kStackingEngine, "stacking", interval});
        }

        internal_data->clock = std::make_unique<cece::CeceClock>(config.driver_config.start_time, config.driver_config.end_time,
                                                                 config.driver_config.timestep_seconds, clock_components);

        CECE_LOG_INFO("INFO: CeceClock constructed with "s + std::to_string(clock_components.size()) + " components\n"s);
    } catch (const std::invalid_argument& e) {
        CECE_LOG_ERROR("ERROR: Failed to construct CeceClock: "s + e.what() + "\n"s);
        if (rc != nullptr) {
            *rc = -1;
        }
        delete internal_data;
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // Return the internal data pointer to the caller
    if (data_ptr_ptr != nullptr) {
        *data_ptr_ptr = internal_data;
    }

    CECE_LOG_INFO("INFO: CECE Initialize Phase 1 completed successfully\n");

    if (rc != nullptr) {
        *rc = 0;
    }
}

}  // extern "C"
