#ifndef CECE_SCHEME_RUNTIME_HPP
#define CECE_SCHEME_RUNTIME_HPP

#include <conf/config.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "cece/cece_physics_factory.hpp"

namespace cece {

/// A physics scheme and its scheme-specific YAML configuration.
struct SchemeRequest {
    std::string name;             ///< Registered scheme name.
    std::string options_yaml = "{}";  ///< YAML mapping of scheme options.
};

/// The third internal axis is explicitly typed; categories are not vertical levels.
struct SchemeFieldSpec {
    std::string name;                 ///< Canonical field name.
    std::string units;                ///< Canonical units or contract marker.
    std::string axis = "surface";    ///< Surface, level, or category axis.
    int extent = 1;                    ///< Number of elements along the axis.
    bool required = true;             ///< Whether an input must be supplied.
    bool output = false;              ///< Whether a scheme produces the field.
};

/// Reviewed field and history contract for one scheme.
struct SchemeDescriptor {
    std::string name;                     ///< Registered scheme name.
    std::vector<SchemeFieldSpec> fields;  ///< Input and output field contracts.
    bool evolving_history = false;        ///< Whether history evolves across calls.
};

/** Describe a reviewed column implementation.
 * @param request Scheme name and options.
 * @param nlev Number of atmospheric levels.
 * @return The scheme's field and history contract.
 * @throws std::invalid_argument for unsupported schemes or invalid configuration.
 */
SchemeDescriptor DescribeScheme(const SchemeRequest& request, int nlev = 1);

/** Persistent, driver-independent executor over existing PhysicsScheme kernels.
 * The caller owns Kokkos lifetime. Geometry is fixed; fields update in place.
 * Concurrent calls on one instance are rejected. A kernel failure invalidates
 * the instance and requires a new runtime.
 */
class SchemeRuntime {
 public:
    /** Construct ordered schemes for fixed geometry.
     * @param requests Schemes in producer-before-consumer order.
     * @param ncol Number of horizontal columns.
     * @param nlev Number of levels per column.
     */
    SchemeRuntime(const std::vector<SchemeRequest>& requests, int ncol, int nlev = 1);
    ~SchemeRuntime();
    SchemeRuntime(const SchemeRuntime&) = delete;
    SchemeRuntime& operator=(const SchemeRuntime&) = delete;
    /** Copy an input field in Fortran (column, axis) order.
     * @param name Canonical input name.
     * @param values Host values borrowed for this call.
     * @param count Number of doubles, equal to ncol times extent.
     * @param extent Size of the field's non-column axis.
     */
    void set_field(const std::string& name, const double* values, std::size_t count, int extent = 1);
    /** Copy an output field in Fortran (column, axis) order. @param name Output name. @return Owned host values. */
    std::vector<double> get_field(const std::string& name);
    /** Run the interval [start_seconds, start_seconds + dt_seconds).
     * @param start_seconds Start time in seconds.
     * @param dt_seconds Positive interval length in seconds.
     */
    void step(double start_seconds, double dt_seconds);
    /// Release scheme resources; does not finalize Kokkos.
    void close();
    /// Return the scheme contracts retained by this runtime.
    const std::vector<SchemeDescriptor>& descriptors() const { return descriptors_; }
    /// Return the required start time for the next interval, if one is set.
    std::optional<double> next_time();
    /// Restore the next interval start time; this does not restore scheme history.
    void restore_time(std::optional<double> next_time);
    /// Return the fixed number of horizontal columns.
    int ncol() const { return ncol_; }
    /// Return the fixed number of levels.
    int nlev() const { return nlev_; }

 private:
    void require_open() const;
    int ncol_, nlev_;
    bool closed_ = false, failed_ = false;
    std::mutex mutex_;
    std::optional<double> next_time_;
    std::vector<conf::Config> configs_;
    std::vector<SchemeDescriptor> descriptors_;
    std::vector<std::unique_ptr<PhysicsScheme>> schemes_;
    CeceDiagnosticManager diagnostics_;
    CeceImportState imports_;
    CeceExportState exports_;
    std::unordered_map<std::string, SchemeFieldSpec> specs_;
    std::unordered_map<std::string, bool> supplied_;
    std::unordered_map<std::string, bool> external_exports_;
};
}  // namespace cece
#endif
