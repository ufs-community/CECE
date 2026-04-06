/**
 * @file aces_c_bindings.cpp
 * @brief C-linkage binding layer for ACES Python interface
 *
 * This file provides C-linkage functions that bridge Python and C++ ACES code.
 * All functions use opaque pointers (void*) to hide C++ implementation details
 * and maintain ABI stability.
 */

#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <iostream>

#include "aces/aces.hpp"
#include "aces/aces_config.hpp"
#include "aces/aces_state.hpp"
#include "aces/aces_compute.hpp"
#include "aces/aces_config_validator.hpp"
#include "aces/aces_kokkos_config.hpp"
#include "aces/aces_logger.hpp"
#include "aces/aces_diagnostics.hpp"

// Error codes
#define ACES_SUCCESS 0
#define ACES_ERROR_INVALID_CONFIG 1
#define ACES_ERROR_INVALID_STATE 2
#define ACES_ERROR_COMPUTATION_FAILED 3
#define ACES_ERROR_MEMORY_ALLOCATION 4
#define ACES_ERROR_INVALID_EXECUTION_SPACE 5
#define ACES_ERROR_NOT_INITIALIZED 6
#define ACES_ERROR_ALREADY_INITIALIZED 7
#define ACES_ERROR_FIELD_NOT_FOUND 8
#define ACES_ERROR_DIMENSION_MISMATCH 9
#define ACES_ERROR_UNKNOWN 255

// Thread-local error storage
thread_local char* g_error_message = nullptr;

// Global state for ACES instance
struct AcesInstance {
    std::shared_ptr<aces::AcesConfig> config;
    std::shared_ptr<aces::AcesImportState> import_state;
    std::shared_ptr<aces::AcesExportState> export_state;
    int nx = 0;
    int ny = 0;
    int nz = 0;
    bool initialized = false;
};

// Global state for ACES state container
struct AcesStateContainer {
    std::shared_ptr<aces::AcesImportState> import_state;
    std::shared_ptr<aces::AcesExportState> export_state;
    int nx = 0;
    int ny = 0;
    int nz = 0;
};

// Thread-safe access to global ACES instance
static std::mutex g_aces_mutex;
static std::shared_ptr<AcesInstance> g_aces_instance = nullptr;

/**
 * @brief Store error message in thread-local storage
 * @param message Error message to store
 */
static void set_error_message(const char* message) {
    if (g_error_message != nullptr) {
        free(g_error_message);
    }
    if (message != nullptr) {
        g_error_message = (char*)malloc(strlen(message) + 1);
        strcpy(g_error_message, message);
    } else {
        g_error_message = nullptr;
    }
}

/**
 * @brief Allocate and copy a C string
 * @param str String to copy
 * @return Allocated C string (must be freed with aces_c_free_string)
 */
static char* allocate_string(const std::string& str) {
    char* result = (char*)malloc(str.length() + 1);
    if (result != nullptr) {
        strcpy(result, str.c_str());
    }
    return result;
}

// ============================================================================
// Initialization and Finalization (2.1)
// ============================================================================

/**
 * @brief Initialize ACES with configuration
 * @param config_yaml YAML configuration string
 * @param aces_handle Output parameter for ACES handle
 * @return Error code (0 for success)
 */
extern "C" int aces_c_initialize(const char* config_yaml, void** aces_handle) {
    if (config_yaml == nullptr || aces_handle == nullptr) {
        set_error_message("Invalid arguments: config_yaml and aces_handle must not be null");
        return ACES_ERROR_INVALID_CONFIG;
    }

    try {
        std::lock_guard<std::mutex> lock(g_aces_mutex);

        if (g_aces_instance != nullptr && g_aces_instance->initialized) {
            set_error_message("ACES is already initialized");
            return ACES_ERROR_ALREADY_INITIALIZED;
        }

        // Parse YAML configuration
        auto config = std::make_shared<aces::AcesConfig>();
        // TODO: Parse YAML string into config
        // For now, create empty config

        // Create state containers
        auto import_state = std::make_shared<aces::AcesImportState>();
        auto export_state = std::make_shared<aces::AcesExportState>();

        // Create ACES instance
        auto instance = std::make_shared<AcesInstance>();
        instance->config = config;
        instance->import_state = import_state;
        instance->export_state = export_state;
        instance->initialized = true;

        g_aces_instance = instance;
        *aces_handle = instance.get();

        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

/**
 * @brief Finalize ACES and clean up resources
 * @param aces_handle ACES handle from initialization
 * @return Error code (0 for success)
 */
extern "C" int aces_c_finalize(void* aces_handle) {
    if (aces_handle == nullptr) {
        set_error_message("Invalid argument: aces_handle must not be null");
        return ACES_ERROR_NOT_INITIALIZED;
    }

    try {
        std::lock_guard<std::mutex> lock(g_aces_mutex);

        if (g_aces_instance == nullptr || !g_aces_instance->initialized) {
            set_error_message("ACES is not initialized");
            return ACES_ERROR_NOT_INITIALIZED;
        }

        // Verify handle matches
        if (g_aces_instance.get() != static_cast<AcesInstance*>(aces_handle)) {
            set_error_message("Invalid ACES handle");
            return ACES_ERROR_INVALID_CONFIG;
        }

        // Clean up
        g_aces_instance->initialized = false;
        g_aces_instance = nullptr;

        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

/**
 * @brief Check if ACES is initialized
 * @param aces_handle ACES handle
 * @return 1 if initialized, 0 if not
 */
extern "C" int aces_c_is_initialized(void* aces_handle) {
    try {
        std::lock_guard<std::mutex> lock(g_aces_mutex);

        if (aces_handle == nullptr) {
            return 0;
        }

        if (g_aces_instance == nullptr) {
            return 0;
        }

        if (g_aces_instance.get() != static_cast<AcesInstance*>(aces_handle)) {
            return 0;
        }

        return g_aces_instance->initialized ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

// ============================================================================
// State Management (2.2)
// ============================================================================

/**
 * @brief Create ACES state with dimensions
 * @param nx X dimension
 * @param ny Y dimension
 * @param nz Z dimension
 * @param state_handle Output parameter for state handle
 * @return Error code (0 for success)
 */
extern "C" int aces_c_state_create(int nx, int ny, int nz, void** state_handle) {
    if (nx <= 0 || ny <= 0 || nz <= 0) {
        set_error_message("Invalid dimensions: nx, ny, nz must be positive");
        return ACES_ERROR_INVALID_STATE;
    }

    if (state_handle == nullptr) {
        set_error_message("Invalid argument: state_handle must not be null");
        return ACES_ERROR_INVALID_STATE;
    }

    try {
        auto import_state = std::make_shared<aces::AcesImportState>();
        auto export_state = std::make_shared<aces::AcesExportState>();

        auto container = std::make_shared<AcesStateContainer>();
        container->import_state = import_state;
        container->export_state = export_state;
        container->nx = nx;
        container->ny = ny;
        container->nz = nz;

        // Store the shared_ptr in a new heap-allocated shared_ptr to maintain reference count
        auto* container_ptr = new std::shared_ptr<AcesStateContainer>(container);
        *state_handle = container_ptr;
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_MEMORY_ALLOCATION;
    }
}

/**
 * @brief Destroy ACES state
 * @param state_handle State handle
 * @return Error code (0 for success)
 */
extern "C" int aces_c_state_destroy(void* state_handle) {
    if (state_handle == nullptr) {
        set_error_message("Invalid argument: state_handle must not be null");
        return ACES_ERROR_INVALID_STATE;
    }

    try {
        auto container_ptr = static_cast<std::shared_ptr<AcesStateContainer>*>(state_handle);
        delete container_ptr;
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

/**
 * @brief Add import field to state
 * @param state_handle State handle
 * @param name Field name
 * @param data Pointer to field data (double array)
 * @param nx X dimension
 * @param ny Y dimension
 * @param nz Z dimension
 * @return Error code (0 for success)
 */
extern "C" int aces_c_state_add_import_field(void* state_handle, const char* name,
                                              double* data, int nx, int ny, int nz) {
    if (state_handle == nullptr || name == nullptr || data == nullptr) {
        set_error_message("Invalid arguments: state_handle, name, and data must not be null");
        return ACES_ERROR_INVALID_STATE;
    }

    if (nx <= 0 || ny <= 0 || nz <= 0) {
        set_error_message("Invalid dimensions: nx, ny, nz must be positive");
        return ACES_ERROR_DIMENSION_MISMATCH;
    }

    try {
        auto container = static_cast<AcesStateContainer*>(state_handle);

        // Verify dimensions match state
        if (nx != container->nx || ny != container->ny || nz != container->nz) {
            std::ostringstream oss;
            oss << "Dimension mismatch: expected (" << container->nx << ", " << container->ny
                << ", " << container->nz << "), got (" << nx << ", " << ny << ", " << nz << ")";
            set_error_message(oss.str().c_str());
            return ACES_ERROR_DIMENSION_MISMATCH;
        }

        // Create unmanaged view wrapping the data
        aces::UnmanagedHostView3D view(data, nx, ny, nz);

        // Create DualView and copy data
        aces::DualView3D dual_view("import_field_" + std::string(name), nx, ny, nz);
        Kokkos::deep_copy(dual_view.view_host(), view);
        dual_view.sync_device();

        // Store in import state
        container->import_state->fields[name] = dual_view;

        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

/**
 * @brief Get export field from state
 * @param state_handle State handle
 * @param name Field name
 * @param data_ptr Output parameter for field data pointer
 * @param nx Output parameter for X dimension
 * @param ny Output parameter for Y dimension
 * @param nz Output parameter for Z dimension
 * @return Error code (0 for success)
 */
extern "C" int aces_c_state_get_export_field(void* state_handle, const char* name,
                                              double** data_ptr, int* nx, int* ny, int* nz) {
    if (state_handle == nullptr || name == nullptr || data_ptr == nullptr) {
        set_error_message("Invalid arguments: state_handle, name, and data_ptr must not be null");
        return ACES_ERROR_INVALID_STATE;
    }

    try {
        auto container = static_cast<AcesStateContainer*>(state_handle);

        auto it = container->export_state->fields.find(name);
        if (it == container->export_state->fields.end()) {
            std::ostringstream oss;
            oss << "Export field not found: " << name;
            set_error_message(oss.str().c_str());
            return ACES_ERROR_FIELD_NOT_FOUND;
        }

        auto& dual_view = it->second;
        dual_view.sync_host();
        auto view = dual_view.view_host();

        *data_ptr = view.data();
        *nx = view.extent(0);
        *ny = view.extent(1);
        *nz = view.extent(2);

        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

// ============================================================================
// Computation (2.3)
// ============================================================================

/**
 * @brief Execute ACES computation
 * @param aces_handle ACES handle
 * @param state_handle State handle
 * @param hour Hour of day (0-23)
 * @param day_of_week Day of week (0-6)
 * @param month Month (1-12)
 * @return Error code (0 for success)
 */
extern "C" int aces_c_compute(void* aces_handle, void* state_handle,
                               int hour, int day_of_week, int month) {
    if (aces_handle == nullptr || state_handle == nullptr) {
        set_error_message("Invalid arguments: aces_handle and state_handle must not be null");
        return ACES_ERROR_INVALID_STATE;
    }

    if (hour < 0 || hour > 23 || day_of_week < 0 || day_of_week > 6 || month < 1 || month > 12) {
        set_error_message("Invalid temporal parameters: hour (0-23), day_of_week (0-6), month (1-12)");
        return ACES_ERROR_INVALID_CONFIG;
    }

    try {
        std::lock_guard<std::mutex> lock(g_aces_mutex);

        if (g_aces_instance == nullptr || !g_aces_instance->initialized) {
            set_error_message("ACES is not initialized");
            return ACES_ERROR_NOT_INITIALIZED;
        }

        if (g_aces_instance.get() != static_cast<AcesInstance*>(aces_handle)) {
            set_error_message("Invalid ACES handle");
            return ACES_ERROR_INVALID_CONFIG;
        }

        auto state_container = static_cast<AcesStateContainer*>(state_handle);

        // Create resolver for field access
        aces::AcesStateResolver resolver(*state_container->import_state,
                                         *state_container->export_state,
                                         g_aces_instance->config->met_mapping,
                                         g_aces_instance->config->scale_factor_mapping,
                                         g_aces_instance->config->mask_mapping);

        // Execute computation
        aces::ComputeEmissions(*g_aces_instance->config, resolver,
                              state_container->nx, state_container->ny, state_container->nz,
                              {}, hour, day_of_week, month);

        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_COMPUTATION_FAILED;
    }
}

// ============================================================================
// Configuration (2.4)
// ============================================================================

/**
 * @brief Validate YAML configuration
 * @param config_yaml YAML configuration string
 * @param error_msg Output parameter for error message
 * @return Error code (0 for success)
 */
extern "C" int aces_c_config_validate(const char* config_yaml, char** error_msg) {
    if (config_yaml == nullptr || error_msg == nullptr) {
        set_error_message("Invalid arguments: config_yaml and error_msg must not be null");
        return ACES_ERROR_INVALID_CONFIG;
    }

    try {
        // TODO: Parse YAML and validate
        // For now, return success
        *error_msg = nullptr;
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        *error_msg = allocate_string(e.what());
        return ACES_ERROR_INVALID_CONFIG;
    }
}

/**
 * @brief Serialize configuration to YAML
 * @param aces_handle ACES handle
 * @param yaml_str Output parameter for YAML string
 * @return Error code (0 for success)
 */
extern "C" int aces_c_config_to_yaml(void* aces_handle, char** yaml_str) {
    if (aces_handle == nullptr || yaml_str == nullptr) {
        set_error_message("Invalid arguments: aces_handle and yaml_str must not be null");
        return ACES_ERROR_INVALID_CONFIG;
    }

    try {
        std::lock_guard<std::mutex> lock(g_aces_mutex);

        if (g_aces_instance == nullptr || !g_aces_instance->initialized) {
            set_error_message("ACES is not initialized");
            return ACES_ERROR_NOT_INITIALIZED;
        }

        if (g_aces_instance.get() != static_cast<AcesInstance*>(aces_handle)) {
            set_error_message("Invalid ACES handle");
            return ACES_ERROR_INVALID_CONFIG;
        }

        // TODO: Serialize config to YAML
        // For now, return empty YAML
        *yaml_str = allocate_string("# ACES Configuration\n");
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

// ============================================================================
// Execution Space (2.5)
// ============================================================================

/**
 * @brief Set Kokkos execution space
 * @param space_name Execution space name ("Serial", "OpenMP", "CUDA", "HIP")
 * @return Error code (0 for success)
 */
extern "C" int aces_c_set_execution_space(const char* space_name) {
    if (space_name == nullptr) {
        set_error_message("Invalid argument: space_name must not be null");
        return ACES_ERROR_INVALID_EXECUTION_SPACE;
    }

    try {
        // Kokkos execution space is set at initialization time
        // This is a placeholder for future dynamic switching
        std::string space(space_name);

        // Validate space name
        if (space != "Serial" && space != "OpenMP" && space != "CUDA" && space != "HIP") {
            std::ostringstream oss;
            oss << "Invalid execution space: " << space;
            set_error_message(oss.str().c_str());
            return ACES_ERROR_INVALID_EXECUTION_SPACE;
        }

        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

/**
 * @brief Get current Kokkos execution space
 * @param space_name Output parameter for execution space name
 * @return Error code (0 for success)
 */
extern "C" int aces_c_get_execution_space(char** space_name) {
    if (space_name == nullptr) {
        set_error_message("Invalid argument: space_name must not be null");
        return ACES_ERROR_INVALID_EXECUTION_SPACE;
    }

    try {
        std::string current_space = aces::GetDefaultExecutionSpaceName();
        *space_name = allocate_string(current_space);
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

/**
 * @brief Get available Kokkos execution spaces
 * @param spaces_json Output parameter for JSON array of space names
 * @return Error code (0 for success)
 */
extern "C" int aces_c_get_available_execution_spaces(char** spaces_json) {
    if (spaces_json == nullptr) {
        set_error_message("Invalid argument: spaces_json must not be null");
        return ACES_ERROR_INVALID_EXECUTION_SPACE;
    }

    try {
        std::ostringstream oss;
        oss << "[";

        bool first = true;

#ifdef KOKKOS_ENABLE_SERIAL
        if (!first) oss << ", ";
        oss << "\"Serial\"";
        first = false;
#endif

#ifdef KOKKOS_ENABLE_OPENMP
        if (!first) oss << ", ";
        oss << "\"OpenMP\"";
        first = false;
#endif

#ifdef KOKKOS_ENABLE_CUDA
        if (!first) oss << ", ";
        oss << "\"CUDA\"";
        first = false;
#endif

#ifdef KOKKOS_ENABLE_HIP
        if (!first) oss << ", ";
        oss << "\"HIP\"";
        first = false;
#endif

        // Always include Serial as fallback
        if (first) {
            oss << "\"Serial\"";
        }

        oss << "]";

        *spaces_json = allocate_string(oss.str());
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

// ============================================================================
// Error Handling and Logging (2.6)
// ============================================================================

/**
 * @brief Get last error message
 * @param error_msg Output parameter for error message
 * @return Error code (0 for success)
 */
extern "C" int aces_c_get_last_error(char** error_msg) {
    if (error_msg == nullptr) {
        return ACES_ERROR_INVALID_CONFIG;
    }

    if (g_error_message != nullptr) {
        *error_msg = g_error_message;
        return ACES_SUCCESS;
    }

    *error_msg = nullptr;
    return ACES_SUCCESS;
}

/**
 * @brief Free C string allocated by C binding layer
 * @param str String to free
 */
extern "C" void aces_c_free_string(char* str) {
    if (str != nullptr) {
        free(str);
    }
}

/**
 * @brief Set logging level
 * @param level Log level ("DEBUG", "INFO", "WARNING", "ERROR")
 * @return Error code (0 for success)
 */
extern "C" int aces_c_set_log_level(const char* level) {
    if (level == nullptr) {
        set_error_message("Invalid argument: level must not be null");
        return ACES_ERROR_INVALID_CONFIG;
    }

    try {
        std::string log_level(level);

        // Validate log level
        if (log_level != "DEBUG" && log_level != "INFO" &&
            log_level != "WARNING" && log_level != "ERROR") {
            std::ostringstream oss;
            oss << "Invalid log level: " << log_level;
            set_error_message(oss.str().c_str());
            return ACES_ERROR_INVALID_CONFIG;
        }

        // TODO: Set ACES logger level
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

// ============================================================================
// Diagnostics (2.7)
// ============================================================================

/**
 * @brief Get performance and timing diagnostics
 * @param diagnostics_json Output parameter for JSON diagnostics
 * @return Error code (0 for success)
 */
extern "C" int aces_c_get_diagnostics(char** diagnostics_json) {
    if (diagnostics_json == nullptr) {
        set_error_message("Invalid argument: diagnostics_json must not be null");
        return ACES_ERROR_INVALID_CONFIG;
    }

    try {
        std::lock_guard<std::mutex> lock(g_aces_mutex);

        if (g_aces_instance == nullptr || !g_aces_instance->initialized) {
            set_error_message("ACES is not initialized");
            return ACES_ERROR_NOT_INITIALIZED;
        }

        // TODO: Collect diagnostics from ACES
        // For now, return empty JSON object
        *diagnostics_json = allocate_string("{}");
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}

/**
 * @brief Reset diagnostic data
 * @return Error code (0 for success)
 */
extern "C" int aces_c_reset_diagnostics() {
    try {
        std::lock_guard<std::mutex> lock(g_aces_mutex);

        if (g_aces_instance == nullptr || !g_aces_instance->initialized) {
            set_error_message("ACES is not initialized");
            return ACES_ERROR_NOT_INITIALIZED;
        }

        // TODO: Reset diagnostics in ACES
        set_error_message(nullptr);
        return ACES_SUCCESS;
    } catch (const std::exception& e) {
        set_error_message(e.what());
        return ACES_ERROR_UNKNOWN;
    }
}
