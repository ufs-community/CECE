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

struct SchemeRequest {
    std::string name;
    std::string options_yaml = "{}";
};

/// The third internal axis is explicitly typed; categories are not vertical levels.
struct SchemeFieldSpec {
    std::string name;
    std::string units;
    std::string axis = "surface";
    int extent = 1;
    bool required = true;
    bool output = false;
};

struct SchemeDescriptor {
    std::string name;
    std::vector<SchemeFieldSpec> fields;
    bool evolving_history = false;
};

/// Describe the reviewed column implementations; unsupported schemes fail explicitly.
SchemeDescriptor DescribeScheme(const SchemeRequest& request, int nlev = 1);

/// Persistent, driver-independent executor over the existing PhysicsScheme kernels.
/// The caller owns Kokkos lifetime. Geometry is fixed; field values update in-place.
class SchemeRuntime {
 public:
    SchemeRuntime(const std::vector<SchemeRequest>& requests, int ncol, int nlev = 1);
    ~SchemeRuntime();
    SchemeRuntime(const SchemeRuntime&) = delete;
    SchemeRuntime& operator=(const SchemeRuntime&) = delete;
    void set_field(const std::string& name, const double* values, std::size_t count, int extent = 1);
    std::vector<double> get_field(const std::string& name);
    void step(double start_seconds, double dt_seconds);
    void close();
    const std::vector<SchemeDescriptor>& descriptors() const { return descriptors_; }
    std::optional<double> next_time();
    void restore_time(std::optional<double> next_time);
    int ncol() const { return ncol_; }
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
