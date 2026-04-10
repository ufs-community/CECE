#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "cece/cece_config.hpp"
#include "cece/cece_config_validator.hpp"
#include "cece/cece_state.hpp"
#include "cece/cece_compute.hpp"
#include "cece/cece_stacking_engine.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_kokkos_config.hpp"

namespace py = pybind11;

/**
 * @brief Pybind11 trampoline class for cece::FieldResolver.
 *
 * Allows Python subclasses to override the pure virtual methods of
 * FieldResolver. Each override acquires the GIL before calling into Python.
 */

// Type aliases to avoid commas inside PYBIND11_OVERRIDE_PURE macro
using ConstDeviceView3D =
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;
using MutableDeviceView3D =
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;

class PyFieldResolver : public cece::FieldResolver {
   public:
    using cece::FieldResolver::FieldResolver;

    cece::UnmanagedHostView3D ResolveImport(const std::string& name, int nx, int ny,
                                            int nz) override {
        PYBIND11_OVERRIDE_PURE(cece::UnmanagedHostView3D, cece::FieldResolver, ResolveImport, name,
                               nx, ny, nz);
    }

    cece::UnmanagedHostView3D ResolveExport(const std::string& name, int nx, int ny,
                                            int nz) override {
        PYBIND11_OVERRIDE_PURE(cece::UnmanagedHostView3D, cece::FieldResolver, ResolveExport, name,
                               nx, ny, nz);
    }

    ConstDeviceView3D ResolveImportDevice(const std::string& name, int nx, int ny,
                                          int nz) override {
        PYBIND11_OVERRIDE_PURE(ConstDeviceView3D, cece::FieldResolver, ResolveImportDevice, name,
                               nx, ny, nz);
    }

    MutableDeviceView3D ResolveExportDevice(const std::string& name, int nx, int ny,
                                            int nz) override {
        PYBIND11_OVERRIDE_PURE(MutableDeviceView3D, cece::FieldResolver, ResolveExportDevice, name,
                               nx, ny, nz);
    }
};

/**
 * @brief Wrapper around CeceStateResolver that owns the mapping dictionaries.
 *
 * The C++ CeceStateResolver stores const references to the mapping maps.
 * When pybind11 converts Python dicts to C++ unordered_maps, the temporaries
 * are destroyed after the constructor returns, leaving dangling references.
 * This wrapper owns the maps via a helper base that is initialized first.
 */
struct PyStateResolverMaps {
    std::unordered_map<std::string, std::string> met;
    std::unordered_map<std::string, std::string> sf;
    std::unordered_map<std::string, std::string> mask;
};

class PyCeceStateResolver : private PyStateResolverMaps, public cece::CeceStateResolver {
   public:
    PyCeceStateResolver(const cece::CeceImportState& imp,
                        const cece::CeceExportState& exp,
                        std::unordered_map<std::string, std::string> met_map,
                        std::unordered_map<std::string, std::string> sf_map = {},
                        std::unordered_map<std::string, std::string> mask_map = {})
        : PyStateResolverMaps{std::move(met_map), std::move(sf_map), std::move(mask_map)},
          cece::CeceStateResolver(imp, exp, met, sf, mask) {}
};

// Static storage for Python exception classes (initialized in module init)
static PyObject* s_cece_exception = nullptr;
static PyObject* s_cece_config_error = nullptr;
static PyObject* s_cece_state_error = nullptr;

// Exception translator function (non-capturing, uses static pointers)
// Only translates CECE-specific C++ exceptions; pybind11's own exceptions
// (py::value_error, py::key_error, etc.) are handled by pybind11 itself.
static void cece_exception_translator(std::exception_ptr p) {
    try {
        if (p) std::rethrow_exception(p);
    } catch (const py::builtin_exception&) {
        // Let pybind11 handle its own exception types (ValueError, KeyError, etc.)
        throw;
    } catch (const std::invalid_argument& e) {
        PyErr_SetString(s_cece_config_error, e.what());
    } catch (const std::out_of_range& e) {
        PyErr_SetString(s_cece_state_error, e.what());
    } catch (const std::runtime_error& e) {
        PyErr_SetString(s_cece_exception, e.what());
    } catch (...) {
        PyErr_SetString(s_cece_exception, "Unknown C++ exception");
    }
}

PYBIND11_MODULE(_cece_core, m) {
    m.doc() = "CECE C++ core bindings via pybind11";

    // --- Exception Translation ---
    // Import Python exception classes from cece.exceptions
    // Try "cece.exceptions" first (when imported as part of the cece package),
    // fall back to "exceptions" (when _cece_core.so is on sys.path directly)
    py::object cece_exceptions;
    try {
        cece_exceptions = py::module_::import("cece.exceptions");
    } catch (py::error_already_set&) {
        cece_exceptions = py::module_::import("exceptions");
    }
    s_cece_exception = cece_exceptions.attr("CeceException").ptr();
    s_cece_config_error = cece_exceptions.attr("CeceConfigError").ptr();
    s_cece_state_error = cece_exceptions.attr("CeceStateError").ptr();

    // Keep references alive for the lifetime of the module
    Py_INCREF(s_cece_exception);
    Py_INCREF(s_cece_config_error);
    Py_INCREF(s_cece_state_error);

    // Register exception translator
    py::register_exception_translator(cece_exception_translator);

    // --- Enum Bindings ---

    py::enum_<cece::VerticalDistributionMethod>(m, "VerticalDistributionMethod")
        .value("SINGLE", cece::VerticalDistributionMethod::SINGLE)
        .value("RANGE", cece::VerticalDistributionMethod::RANGE)
        .value("PRESSURE", cece::VerticalDistributionMethod::PRESSURE)
        .value("HEIGHT", cece::VerticalDistributionMethod::HEIGHT)
        .value("PBL", cece::VerticalDistributionMethod::PBL);

    py::enum_<cece::VerticalCoordType>(m, "VerticalCoordType")
        .value("NONE", cece::VerticalCoordType::NONE)
        .value("FV3", cece::VerticalCoordType::FV3)
        .value("MPAS", cece::VerticalCoordType::MPAS)
        .value("WRF", cece::VerticalCoordType::WRF);

    // --- EmissionLayer Binding ---

    py::class_<cece::EmissionLayer>(m, "EmissionLayer")
        .def(py::init<>())
        .def_readwrite("operation", &cece::EmissionLayer::operation)
        .def_readwrite("field_name", &cece::EmissionLayer::field_name)
        .def_readwrite("scale", &cece::EmissionLayer::scale)
        .def_readwrite("hierarchy", &cece::EmissionLayer::hierarchy)
        .def_readwrite("masks", &cece::EmissionLayer::masks)
        .def_readwrite("scale_fields", &cece::EmissionLayer::scale_fields)
        .def_readwrite("category", &cece::EmissionLayer::category)
        .def_readwrite("diurnal_cycle", &cece::EmissionLayer::diurnal_cycle)
        .def_readwrite("weekly_cycle", &cece::EmissionLayer::weekly_cycle)
        .def_readwrite("seasonal_cycle", &cece::EmissionLayer::seasonal_cycle)
        .def_readwrite("vdist_method", &cece::EmissionLayer::vdist_method)
        .def_readwrite("vdist_layer_start", &cece::EmissionLayer::vdist_layer_start)
        .def_readwrite("vdist_layer_end", &cece::EmissionLayer::vdist_layer_end)
        .def_readwrite("vdist_p_start", &cece::EmissionLayer::vdist_p_start)
        .def_readwrite("vdist_p_end", &cece::EmissionLayer::vdist_p_end)
        .def_readwrite("vdist_h_start", &cece::EmissionLayer::vdist_h_start)
        .def_readwrite("vdist_h_end", &cece::EmissionLayer::vdist_h_end);

    // --- TemporalCycle Binding (needed for CeceConfig.temporal_cycles) ---

    py::class_<cece::TemporalCycle>(m, "TemporalCycle")
        .def(py::init<>())
        .def_readwrite("factors", &cece::TemporalCycle::factors);

    // --- PhysicsSchemeConfig Binding (needed for CeceConfig.physics_schemes) ---

    py::class_<cece::PhysicsSchemeConfig>(m, "PhysicsSchemeConfig")
        .def(py::init<>())
        .def_readwrite("name", &cece::PhysicsSchemeConfig::name)
        .def_readwrite("language", &cece::PhysicsSchemeConfig::language);

    // --- CeceConfig Binding ---

    py::class_<cece::CeceConfig>(m, "CeceConfig")
        .def(py::init<>())
        .def_readwrite("species_layers", &cece::CeceConfig::species_layers)
        .def_readwrite("met_mapping", &cece::CeceConfig::met_mapping)
        .def_readwrite("scale_factor_mapping", &cece::CeceConfig::scale_factor_mapping)
        .def_readwrite("mask_mapping", &cece::CeceConfig::mask_mapping)
        .def_readwrite("temporal_cycles", &cece::CeceConfig::temporal_cycles)
        .def_readwrite("physics_schemes", &cece::CeceConfig::physics_schemes);

    // --- Module-level functions ---

    m.def("ParseConfig", &cece::ParseConfig, py::arg("filename"),
          "Parse an CECE YAML configuration file and return an CeceConfig object.");

    m.def("AddSpecies", &cece::AddSpecies,
          py::arg("config"), py::arg("species_name"), py::arg("layers"),
          "Add a new emission species with its layers to an existing config.");

    m.def("AddScaleFactor", &cece::AddScaleFactor,
          py::arg("config"), py::arg("internal_name"), py::arg("external_name"),
          "Add a new scale factor mapping to an existing config.");

    m.def("AddMask", &cece::AddMask,
          py::arg("config"), py::arg("internal_name"), py::arg("external_name"),
          "Add a new mask mapping to an existing config.");

    // --- CeceImportState Binding ---

    py::class_<cece::CeceImportState>(m, "CeceImportState")
        .def(py::init<>())
        .def("set_field", [](cece::CeceImportState& self, const std::string& name,
                             py::array arr) {
            // Validate dtype is float64
            if (!py::isinstance<py::array_t<double>>(arr)) {
                throw py::value_error("Array dtype must be float64, got " +
                                      std::string(py::str(arr.dtype())));
            }
            py::array_t<double> typed_arr = arr.cast<py::array_t<double>>();

            // Validate array is 3D
            if (typed_arr.ndim() != 3) {
                throw py::value_error("Array must be 3D, got " +
                                      std::to_string(typed_arr.ndim()) + "D");
            }

            py::array_t<double> fortran_arr;

            // Check if Fortran-contiguous (column-major)
            if ((typed_arr.flags() & py::array::f_style) != 0) {
                // Fortran-contiguous: use directly (zero-copy wrap)
                fortran_arr = typed_arr;
            } else if ((typed_arr.flags() & py::array::c_style) != 0) {
                // C-contiguous: convert to Fortran order, log warning
                auto& logger = cece::CeceLogger::GetInstance();
                logger.LogWarning("Array for field '" + name +
                    "' is C-contiguous; converting to Fortran order (copy overhead)");
                py::module_ np = py::module_::import("numpy");
                fortran_arr = np.attr("asfortranarray")(typed_arr);
            } else {
                throw py::value_error("Array must be either C-contiguous or Fortran-contiguous");
            }

            // Get buffer info from the Fortran-contiguous array
            py::buffer_info buf = fortran_arr.request();
            auto* ptr = static_cast<double*>(buf.ptr);
            int nx = static_cast<int>(buf.shape[0]);
            int ny = static_cast<int>(buf.shape[1]);
            int nz = static_cast<int>(buf.shape[2]);

            // Create UnmanagedHostView3D wrapping the buffer pointer (zero-copy)
            cece::UnmanagedHostView3D unmanaged_view(ptr, nx, ny, nz);

            // Create DualView3D, deep-copy from unmanaged view, sync to device
            cece::DualView3D dual_view("field_" + name, nx, ny, nz);
            Kokkos::deep_copy(dual_view.view_host(), unmanaged_view);
            dual_view.modify_host();
            dual_view.sync_device();

            // Store in fields map
            self.fields[name] = dual_view;
        }, py::arg("name"), py::arg("array"),
           "Set a 3D field from a NumPy float64 array. Fortran-contiguous preferred for zero-copy.")
        .def("get_field_names", [](const cece::CeceImportState& self) {
            py::list names;
            for (const auto& pair : self.fields) {
                names.append(pair.first);
            }
            return names;
        }, "Return a list of field name strings.");

    // --- CeceExportState Binding ---

    py::class_<cece::CeceExportState>(m, "CeceExportState")
        .def(py::init<>())
        .def("set_field", [](cece::CeceExportState& self, const std::string& name,
                             py::array arr) {
            // Validate dtype is float64
            if (!py::isinstance<py::array_t<double>>(arr)) {
                throw py::value_error("Array dtype must be float64, got " +
                                      std::string(py::str(arr.dtype())));
            }
            py::array_t<double> typed_arr = arr.cast<py::array_t<double>>();

            // Validate array is 3D
            if (typed_arr.ndim() != 3) {
                throw py::value_error("Array must be 3D, got " +
                                      std::to_string(typed_arr.ndim()) + "D");
            }

            py::array_t<double> fortran_arr;

            // Check if Fortran-contiguous (column-major)
            if ((typed_arr.flags() & py::array::f_style) != 0) {
                fortran_arr = typed_arr;
            } else if ((typed_arr.flags() & py::array::c_style) != 0) {
                py::module_ np = py::module_::import("numpy");
                fortran_arr = np.attr("asfortranarray")(typed_arr);
            } else {
                throw py::value_error("Array must be either C-contiguous or Fortran-contiguous");
            }

            // Get buffer info from the Fortran-contiguous array
            py::buffer_info buf = fortran_arr.request();
            auto* ptr = static_cast<double*>(buf.ptr);
            int nx = static_cast<int>(buf.shape[0]);
            int ny = static_cast<int>(buf.shape[1]);
            int nz = static_cast<int>(buf.shape[2]);

            // Create UnmanagedHostView3D wrapping the buffer pointer (zero-copy)
            cece::UnmanagedHostView3D unmanaged_view(ptr, nx, ny, nz);

            // Create DualView3D, deep-copy from unmanaged view, sync to device
            cece::DualView3D dual_view("export_" + name, nx, ny, nz);
            Kokkos::deep_copy(dual_view.view_host(), unmanaged_view);
            dual_view.modify_host();
            dual_view.sync_device();

            // Store in fields map
            self.fields[name] = dual_view;
        }, py::arg("name"), py::arg("array"),
           "Set a 3D export field from a NumPy float64 array.")
        .def("get_field", [](cece::CeceExportState& self, const std::string& name) {
            auto it = self.fields.find(name);
            if (it == self.fields.end()) {
                throw py::key_error("Field '" + name + "' not found in export state");
            }

            auto& dual_view = it->second;

            // Sync host data from device
            dual_view.sync_host();

            auto host_view = dual_view.view_host();
            int nx = static_cast<int>(host_view.extent(0));
            int ny = static_cast<int>(host_view.extent(1));
            int nz = static_cast<int>(host_view.extent(2));

            // Create a capsule that prevents deallocation while the NumPy array is alive.
            // The capsule captures a pointer to the DualView3D in the fields map.
            // The fields map entry stays alive as long as the CeceExportState is alive,
            // and the capsule prevents Python from freeing the data pointer.
            py::capsule capsule(host_view.data(), [](void*) {
                // No-op destructor: the data is owned by the DualView3D in the
                // CeceExportState.fields map. The capsule just prevents NumPy
                // from trying to free this pointer.
            });

            // Return py::array_t with shape/strides pointing to host view data
            // Kokkos LayoutLeft = Fortran column-major order
            std::vector<ssize_t> shape = {nx, ny, nz};
            std::vector<ssize_t> strides = {
                static_cast<ssize_t>(sizeof(double)),
                static_cast<ssize_t>(sizeof(double) * nx),
                static_cast<ssize_t>(sizeof(double) * nx * ny)
            };

            return py::array_t<double>(shape, strides, host_view.data(), capsule);
        }, py::arg("name"),
           "Get a 3D field as a NumPy array (zero-copy from Kokkos host view).")
        .def("get_field_names", [](const cece::CeceExportState& self) {
            py::list names;
            for (const auto& pair : self.fields) {
                names.append(pair.first);
            }
            return names;
        }, "Return a list of field name strings.");

    // --- FieldResolver Binding (abstract base with trampoline) ---
    // Must be registered before CeceStateResolver which inherits from it.

    py::class_<cece::FieldResolver, PyFieldResolver>(m, "FieldResolver")
        .def(py::init<>())
        .def("ResolveImport", &cece::FieldResolver::ResolveImport,
             py::arg("name"), py::arg("nx"), py::arg("ny"), py::arg("nz"))
        .def("ResolveExport", &cece::FieldResolver::ResolveExport,
             py::arg("name"), py::arg("nx"), py::arg("ny"), py::arg("nz"))
        .def("ResolveImportDevice", &cece::FieldResolver::ResolveImportDevice,
             py::arg("name"), py::arg("nx"), py::arg("ny"), py::arg("nz"))
        .def("ResolveExportDevice", &cece::FieldResolver::ResolveExportDevice,
             py::arg("name"), py::arg("nx"), py::arg("ny"), py::arg("nz"));

    // --- CeceStateResolver Binding ---

    py::class_<PyCeceStateResolver, cece::FieldResolver>(m, "CeceStateResolver")
        .def(py::init<const cece::CeceImportState&,
                      const cece::CeceExportState&,
                      std::unordered_map<std::string, std::string>,
                      std::unordered_map<std::string, std::string>,
                      std::unordered_map<std::string, std::string>>(),
             py::arg("import_state"),
             py::arg("export_state"),
             py::arg("met_mapping"),
             py::arg("sf_mapping") = std::unordered_map<std::string, std::string>{},
             py::arg("mask_mapping") = std::unordered_map<std::string, std::string>{},
             "Create a state resolver from import/export states and mapping dictionaries.");

    // --- ComputeEmissions Binding ---

    m.def("compute_emissions", [](const cece::CeceConfig& config,
                                   cece::FieldResolver& resolver,
                                   int nx, int ny, int nz,
                                   int hour, int day_of_week, int month,
                                   cece::StackingEngine* engine) {
        // Create an empty default mask (the C++ function accepts a default-constructed view)
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> default_mask;
        cece::ComputeEmissions(config, resolver, nx, ny, nz, default_mask,
                               hour, day_of_week, month, engine);
    },
    py::arg("config"),
    py::arg("resolver"),
    py::arg("nx"), py::arg("ny"), py::arg("nz"),
    py::arg("hour") = 0,
    py::arg("day_of_week") = 0,
    py::arg("month") = 0,
    py::arg("engine") = nullptr,
    py::call_guard<py::gil_scoped_release>(),
    "Perform emission computation for all species defined in the config.");

    // --- StackingEngine Binding ---

    py::class_<cece::StackingEngine>(m, "StackingEngine")
        .def(py::init<cece::CeceConfig>(), py::arg("config"),
             "Construct a StackingEngine from an CeceConfig.")
        .def("Execute", [](cece::StackingEngine& self,
                           cece::FieldResolver& resolver,
                           int nx, int ny, int nz,
                           int hour, int day_of_week, int month) {
            // Create an empty default mask
            Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> default_mask;
            self.Execute(resolver, nx, ny, nz, default_mask, hour, day_of_week, month, nullptr);
        },
        py::arg("resolver"),
        py::arg("nx"), py::arg("ny"), py::arg("nz"),
        py::arg("hour") = 0,
        py::arg("day_of_week") = 0,
        py::arg("month") = 0,
        py::call_guard<py::gil_scoped_release>(),
        "Execute the emission stacking for all species.")
        .def("ResetBindings", &cece::StackingEngine::ResetBindings,
             "Reset the bound field handles.")
        .def("AddSpecies", &cece::StackingEngine::AddSpecies,
             py::arg("species_name"),
             "Dynamically add a new species to the engine.");

    // --- ConfigValidator Bindings ---

    py::class_<cece::ValidationError>(m, "ValidationError")
        .def(py::init<>())
        .def_readonly("field", &cece::ValidationError::field)
        .def_readonly("message", &cece::ValidationError::message)
        .def_readonly("suggestion", &cece::ValidationError::suggestion);

    py::class_<cece::ValidationResult>(m, "ValidationResult")
        .def(py::init<>())
        .def_readonly("is_valid", &cece::ValidationResult::is_valid)
        .def_readonly("errors", &cece::ValidationResult::errors)
        .def_readonly("warnings", &cece::ValidationResult::warnings)
        .def("IsValid", &cece::ValidationResult::IsValid,
             "Check if validation passed (no errors).")
        .def("GetErrorCount", &cece::ValidationResult::GetErrorCount,
             "Get the number of validation errors.")
        .def("GetWarningCount", &cece::ValidationResult::GetWarningCount,
             "Get the number of validation warnings.");

    py::class_<cece::ConfigValidator>(m, "ConfigValidator")
        .def_static("ValidateConfig", [](const std::string& yaml_str) {
            YAML::Node config;
            try {
                config = YAML::Load(yaml_str);
            } catch (const YAML::Exception& e) {
                throw py::value_error(std::string("YAML parse error: ") + e.what());
            }
            return cece::ConfigValidator::ValidateConfig(config);
        }, py::arg("yaml_string"),
           "Validate an CECE YAML configuration string. Raises ValueError on parse failure.");

    // --- Logger Bindings ---

    m.def("set_log_level", [](const std::string& level) {
        auto& logger = cece::CeceLogger::GetInstance();
        if (level == "DEBUG") {
            logger.SetLogLevel(cece::LogLevel::DEBUG);
        } else if (level == "INFO") {
            logger.SetLogLevel(cece::LogLevel::INFO);
        } else if (level == "WARNING") {
            logger.SetLogLevel(cece::LogLevel::WARNING);
        } else if (level == "ERROR") {
            logger.SetLogLevel(cece::LogLevel::ERROR);
        } else {
            throw py::value_error(
                "Invalid log level: '" + level +
                "'. Valid levels are: DEBUG, INFO, WARNING, ERROR");
        }
    }, py::arg("level"),
       "Set the CECE log level. Valid values: DEBUG, INFO, WARNING, ERROR.");

    m.def("get_log_level", []() {
        auto& logger = cece::CeceLogger::GetInstance();
        switch (logger.GetLogLevel()) {
            case cece::LogLevel::DEBUG:   return std::string("DEBUG");
            case cece::LogLevel::INFO:    return std::string("INFO");
            case cece::LogLevel::WARNING: return std::string("WARNING");
            case cece::LogLevel::ERROR:   return std::string("ERROR");
            default:                      return std::string("UNKNOWN");
        }
    }, "Get the current CECE log level as a string.");

    // --- Execution Space Bindings ---

    m.def("get_default_execution_space_name", []() {
        return cece::GetDefaultExecutionSpaceName();
    }, "Get the name of the default Kokkos execution space.");

    m.def("get_available_execution_spaces", []() {
        py::list spaces;
#ifdef KOKKOS_ENABLE_SERIAL
        spaces.append("Serial");
#endif
#ifdef KOKKOS_ENABLE_OPENMP
        spaces.append("OpenMP");
#endif
#ifdef KOKKOS_ENABLE_CUDA
        spaces.append("CUDA");
#endif
#ifdef KOKKOS_ENABLE_HIP
        spaces.append("HIP");
#endif
        return spaces;
    }, "Get a list of available Kokkos execution spaces.");

    // --- Kokkos Initialization Helpers ---

    m.def("initialize_kokkos", []() {
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
        }
    }, "Initialize Kokkos runtime (no-op if already initialized).");

    m.def("finalize_kokkos", []() {
        if (Kokkos::is_initialized() && !Kokkos::is_finalized()) {
            Kokkos::finalize();
        }
    }, "Finalize Kokkos runtime (no-op if not initialized or already finalized).");

    m.def("is_kokkos_initialized", []() {
        return Kokkos::is_initialized();
    }, "Check if Kokkos is initialized.");
}
