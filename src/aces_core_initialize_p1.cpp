/**
 * @file aces_core_initialize_p1.cpp
 * @brief Implementation of NUOPC Initialize Phase 1 (IPDv00p1) for ACES.
 *
 * Phase 1 of initialization handles core component setup:
 * - Initialize Kokkos if not already initialized
 * - Parse YAML configuration
 * - Allocate AcesInternalData structure to persist state across phases
 * - Initialize PhysicsFactory and instantiate all physics schemes
 * - Initialize StackingEngine
 * - Initialize DiagnosticManager
 *
 * This phase does NOT:
 * - Initialize CDEPS (happens in Phase 2)
 * - Bind to ESMF fields (happens in Phase 2)
 * - Access import/export states (happens in Phase 2)
 *
 * @note This is a C++ bridge function called from the Fortran NUOPC cap.
 * @note Uses ESMF C API for state management.
 * @note The AcesInternalData pointer is returned and must be passed to Phase 2.
 *
 * Requirements: 4.7-4.10, 4.18, 4.19
 */

#include <ESMC.h>
#include <Kokkos_Core.hpp>

#include <iostream>
#include <memory>
#include <string>

#include "aces/aces_config.hpp"
#include "aces/aces_diagnostics.hpp"
#include "aces/aces_internal.hpp"
#include "aces/aces_physics_factory.hpp"
#include "aces/aces_stacking_engine.hpp"
#include "aces/aces_standalone_writer.hpp"
#include "aces/physics_scheme.hpp"

extern "C" {

/**
 * @brief Initialize Phase 1 (IPDv00p1) implementation for ACES.
 *
 * This function performs core initialization that does not depend on other
 * components or field realization. It sets up the internal data structures
 * and prepares ACES for field binding in Phase 2.
 *
 * Operations Performed:
 * 1. Initialize Kokkos if not already initialized (track ownership)
 * 2. Parse aces_config.yaml configuration file
 * 3. Allocate AcesInternalData structure for persistent state
 * 4. Initialize PhysicsFactory and instantiate all configured physics schemes
 * 5. Initialize StackingEngine with configuration
 * 6. Initialize DiagnosticManager
 *
 * @param data_ptr_ptr Pointer to void* that will receive the AcesInternalData pointer
 * @param rc Return code (0 = success, non-zero = error)
 *
 * @note The data_ptr_ptr is a double pointer because we need to allocate and return
 *       a new pointer to the caller (Fortran cap).
 * @note Kokkos initialization is tracked - if ACES initializes Kokkos, it will
 *       finalize it. If Kokkos was already initialized, ACES will not finalize it.
 * @note All physics schemes are instantiated and their Initialize methods are called.
 * @note Configuration parsing errors result in rc = -1 and early return.
 *
 * Requirements: 4.7-4.10, 4.18, 4.19
 */
void aces_core_initialize_p1(void** data_ptr_ptr, int* rc) {
    // Initialize return code to success
    if (rc != nullptr) {
        *rc = ESMF_SUCCESS;
    }

    std::cout << "INFO: ACES Initialize Phase 1 (IPDv00p1) - Core initialization" << std::endl;

    // 1. Initialize Kokkos if not already initialized
    bool kokkos_initialized_here = false;
    if (!Kokkos::is_initialized()) {
        std::cout << "INFO: Initializing Kokkos execution space" << std::endl;

        Kokkos::InitializationSettings args;

        // Configure OpenMP threads if enabled
#ifdef KOKKOS_ENABLE_OPENMP
        const char* num_threads = std::getenv("OMP_NUM_THREADS");
        if (num_threads != nullptr) {
            int threads = std::atoi(num_threads);
            if (threads > 0) {
                args.set_num_threads(threads);
                std::cout << "INFO: Setting OpenMP threads to " << threads << std::endl;
            }
        } else {
            // Default to all available threads if not specified
            std::cout << "INFO: OMP_NUM_THREADS not set - using all available threads" << std::endl;
        }
#endif

        // Configure GPU device ID if enabled
#ifdef KOKKOS_ENABLE_CUDA
        const char* device_id = std::getenv("ACES_DEVICE_ID");
        if (device_id != nullptr) {
            int dev_id = std::atoi(device_id);
            if (dev_id >= 0) {
                args.set_device_id(dev_id);
                std::cout << "INFO: Setting CUDA device ID to " << dev_id << std::endl;
            }
        } else {
            std::cout << "INFO: ACES_DEVICE_ID not set - using default CUDA device (0)" << std::endl;
        }
#endif

#ifdef KOKKOS_ENABLE_HIP
        const char* device_id = std::getenv("ACES_DEVICE_ID");
        if (device_id != nullptr) {
            int dev_id = std::atoi(device_id);
            if (dev_id >= 0) {
                args.set_device_id(dev_id);
                std::cout << "INFO: Setting HIP device ID to " << dev_id << std::endl;
            }
        } else {
            std::cout << "INFO: ACES_DEVICE_ID not set - using default HIP device (0)" << std::endl;
        }
#endif

        // Log active execution spaces
        std::cout << "INFO: Active Kokkos execution spaces:" << std::endl;
#ifdef KOKKOS_ENABLE_SERIAL
        std::cout << "INFO:   - Serial" << std::endl;
#endif
#ifdef KOKKOS_ENABLE_OPENMP
        std::cout << "INFO:   - OpenMP" << std::endl;
#endif
#ifdef KOKKOS_ENABLE_CUDA
        std::cout << "INFO:   - CUDA" << std::endl;
#endif
#ifdef KOKKOS_ENABLE_HIP
        std::cout << "INFO:   - HIP" << std::endl;
#endif

        Kokkos::initialize(args);
        kokkos_initialized_here = true;
        std::cout << "INFO: Kokkos initialized successfully" << std::endl;
        std::cout << "INFO: Default execution space: " << Kokkos::DefaultExecutionSpace::name() << std::endl;
    } else {
        std::cout << "INFO: Kokkos already initialized - using existing instance" << std::endl;
        std::cout << "INFO: Default execution space: " << Kokkos::DefaultExecutionSpace::name() << std::endl;
    }

    // 2. Parse YAML configuration
    std::cout << "INFO: Parsing aces_config.yaml" << std::endl;
    aces::AcesConfig config;
    try {
        config = aces::ParseConfig("aces_config.yaml");
        std::cout << "INFO: Configuration parsed successfully" << std::endl;
        std::cout << "INFO: Found " << config.species_layers.size() << " emission species" << std::endl;
        std::cout << "INFO: Found " << config.physics_schemes.size() << " physics schemes" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR in aces_core_initialize_p1: Failed to parse aces_config.yaml: "
                  << e.what() << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        // Clean up Kokkos if we initialized it
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    } catch (...) {
        std::cerr << "ERROR in aces_core_initialize_p1: Unknown error parsing aces_config.yaml"
                  << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        // Clean up Kokkos if we initialized it
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // 3. Allocate AcesInternalData structure
    std::cout << "INFO: Allocating internal data structure" << std::endl;
    auto* internal_data = new aces::AcesInternalData();
    internal_data->config = config;
    internal_data->kokkos_initialized_here = kokkos_initialized_here;

    // 4. Initialize PhysicsFactory and instantiate all physics schemes
    std::cout << "INFO: Initializing physics schemes" << std::endl;
    for (const auto& scheme_config : config.physics_schemes) {
        try {
            std::cout << "INFO: Creating physics scheme: " << scheme_config.name << std::endl;
            auto scheme = aces::PhysicsFactory::CreateScheme(scheme_config);

            if (scheme == nullptr) {
                std::cerr << "ERROR: Failed to create physics scheme '" << scheme_config.name
                          << "' - scheme not registered" << std::endl;
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
            std::cout << "INFO: Initializing physics scheme: " << scheme_config.name << std::endl;
            scheme->Initialize(scheme_config.options, nullptr);

            internal_data->active_schemes.push_back(std::move(scheme));
            std::cout << "INFO: Successfully initialized physics scheme: " << scheme_config.name
                      << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed to initialize physics scheme '" << scheme_config.name
                      << "': " << e.what() << std::endl;
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

    std::cout << "INFO: Successfully initialized " << internal_data->active_schemes.size()
              << " physics schemes" << std::endl;

    // 5. Initialize StackingEngine
    std::cout << "INFO: Initializing StackingEngine" << std::endl;
    try {
        internal_data->stacking_engine = std::make_unique<aces::StackingEngine>(config);
        std::cout << "INFO: StackingEngine initialized successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to initialize StackingEngine: " << e.what() << std::endl;
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
    std::cout << "INFO: Initializing DiagnosticManager" << std::endl;
    try {
        internal_data->diagnostic_manager = std::make_unique<aces::AcesDiagnosticManager>();
        std::cout << "INFO: DiagnosticManager initialized successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to initialize DiagnosticManager: " << e.what() << std::endl;
        if (rc != nullptr) {
            *rc = -1;
        }
        delete internal_data;
        if (kokkos_initialized_here && Kokkos::is_initialized()) {
            Kokkos::finalize();
        }
        return;
    }

    // 7. Initialize AcesStandaloneWriter if output config is enabled (Req 11.1, 11.4)
    if (config.output_config.enabled) {
        std::cout << "INFO: Initializing AcesStandaloneWriter for standalone output" << std::endl;
        try {
            internal_data->standalone_writer =
                std::make_unique<aces::AcesStandaloneWriter>(config.output_config);
            internal_data->standalone_mode = true;
            std::cout << "INFO: AcesStandaloneWriter initialized successfully" << std::endl;
            std::cout << "INFO: Output directory: " << config.output_config.directory << std::endl;
            std::cout << "INFO: Output frequency: every " << config.output_config.frequency_steps
                      << " time steps" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed to initialize AcesStandaloneWriter: " << e.what() << std::endl;
            if (rc != nullptr) {
                *rc = -1;
            }
            delete internal_data;
            if (kokkos_initialized_here && Kokkos::is_initialized()) {
                Kokkos::finalize();
            }
            return;
        }
    } else {
        std::cout << "INFO: No output configuration found - standalone writer disabled" << std::endl;
        internal_data->standalone_mode = false;
    }

    // Return the internal data pointer to the caller
    if (data_ptr_ptr != nullptr) {
        *data_ptr_ptr = internal_data;
    }

    std::cout << "INFO: ACES Initialize Phase 1 completed successfully" << std::endl;

    if (rc != nullptr) {
        *rc = ESMF_SUCCESS;
    }
}

}  // extern "C"
