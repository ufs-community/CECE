#include "cece/cece_scheme_c_abi.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

#include "cece/cece_scheme_runtime.hpp"

namespace {
std::mutex api_mutex;
std::map<cece_scheme_handle, std::unique_ptr<cece::SchemeRuntime>> instances;
cece_scheme_handle next_handle = 1;
bool owns_kokkos = false;
void message(char* dest, int cap, const std::string& text) {
    if (!dest || cap <= 0) return;
    auto length = std::min(text.size(), static_cast<std::size_t>(cap - 1));
    std::memcpy(dest, text.data(), length);
    dest[length] = '\0';
}
template <class Function>
int guarded(char* error, int cap, Function&& operation) noexcept {
    std::lock_guard<std::mutex> lock(api_mutex);
    try {
        operation();
        message(error, cap, "");
        return 0;
    } catch (const std::exception& exception) {
        message(error, cap, exception.what());
        return -1;
    } catch (...) {
        message(error, cap, "Unknown CECE scheme failure");
        return -1;
    }
}
std::string required(const char* value, const char* name) {
    if (!value || !*value) throw std::invalid_argument(std::string("Missing ") + name);
    return value;
}
cece::SchemeRuntime& runtime(cece_scheme_handle handle) {
    auto it = instances.find(handle);
    if (it == instances.end()) throw std::invalid_argument("Invalid or destroyed CECE scheme handle");
    return *it->second;
}
std::vector<std::string> output_names(cece::SchemeRuntime& instance) {
    std::vector<std::string> names;
    for (const auto& descriptor : instance.descriptors())
        for (const auto& field : descriptor.fields)
            if (field.output) names.push_back(field.name);
    return names;
}
}  // namespace

extern "C" {
int cece_scheme_create(const char* name, const char* options, int ncol, int nlev, cece_scheme_handle* handle, char* error, int cap) {
    if (handle) *handle = 0;
    return guarded(error, cap, [&] {
        if (!handle) throw std::invalid_argument("Missing output handle");
        auto scheme = required(name, "scheme name");
        if (Kokkos::is_finalized()) throw std::runtime_error("Kokkos has already been finalized");
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize();
            owns_kokkos = true;
        }
        auto instance = std::make_unique<cece::SchemeRuntime>(std::vector<cece::SchemeRequest>{{scheme, options ? options : "{}"}}, ncol, nlev);
        if (next_handle == std::numeric_limits<cece_scheme_handle>::max()) throw std::overflow_error("CECE scheme handle limit reached");
        auto id = next_handle++;
        instances.emplace(id, std::move(instance));
        *handle = id;
    });
}
int cece_scheme_set_field(cece_scheme_handle handle, const char* name, const double* data, size_t count, int extent, char* error, int cap) {
    return guarded(error, cap, [&] { runtime(handle).set_field(required(name, "field name"), data, count, extent); });
}
int cece_scheme_step(cece_scheme_handle handle, double time, double dt, char* error, int cap) {
    return guarded(error, cap, [&] { runtime(handle).step(time, dt); });
}
int cece_scheme_get_field(cece_scheme_handle handle, const char* name, double* data, size_t count, char* error, int cap) {
    return guarded(error, cap, [&] {
        if (!data) throw std::invalid_argument("Missing output field buffer");
        auto values = runtime(handle).get_field(required(name, "field name"));
        if (values.size() != count) throw std::invalid_argument("Output field dimensions do not match");
        std::copy(values.begin(), values.end(), data);
    });
}
int cece_scheme_output_count(cece_scheme_handle handle, int* count, char* error, int cap) {
    return guarded(error, cap, [&] {
        if (!count) throw std::invalid_argument("Missing output count");
        *count = static_cast<int>(output_names(runtime(handle)).size());
    });
}
int cece_scheme_output_name(cece_scheme_handle handle, int index, char* name, int name_cap, char* error, int cap) {
    return guarded(error, cap, [&] {
        auto names = output_names(runtime(handle));
        if (index < 0 || static_cast<std::size_t>(index) >= names.size()) throw std::invalid_argument("Invalid output index");
        if (!name || name_cap <= static_cast<int>(names[index].size())) throw std::invalid_argument("Output name buffer too short");
        message(name, name_cap, names[index]);
    });
}
int cece_scheme_destroy(cece_scheme_handle handle, char* error, int cap) {
    return guarded(error, cap, [&] {
        runtime(handle).close();
        instances.erase(handle);
    });
}
int cece_scheme_finalize_runtime(char* error, int cap) {
    return guarded(error, cap, [&] {
        if (!instances.empty()) throw std::runtime_error("Destroy all scheme instances before runtime finalization");
        if (owns_kokkos) {
            Kokkos::finalize();
            owns_kokkos = false;
        }
    });
}
}  // extern C
