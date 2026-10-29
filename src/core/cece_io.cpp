#include "cece/cece_io.hpp"

#include <stdexcept>

#include "cece/cece_config.hpp"

namespace cece {
namespace io {

void CeceIO::Initialize(const CeceConfig& config, int nx, int ny, int nz) {
    nx_ = nx;
    ny_ = ny;
    nz_ = nz;

    const std::vector<CeceDataStreamConfig>& streams = config.cece_data.streams;
    for (std::size_t si = 0; si < streams.size(); ++si) {
        const CeceDataStreamConfig stream = streams[si];
        if (stream.source == "earthaccess") {
            continue;
        }
        const std::vector<CeceDataVariableConfig>& variables = stream.variables;
        bool implicit_variable = variables.size() == 0;
        const std::size_t variable_count = implicit_variable ? 1 : variables.size();
        for (std::size_t vi = 0; vi < variable_count; ++vi) {
            const CeceDataVariableConfig var = implicit_variable ? CeceDataVariableConfig(stream.name, stream.name) : variables[vi];
            std::string var_name = var.name_in_model;
            // Each field's layer count is per-variable (e.g. BDSNP land-use
            // categories use 24 layers); fall back to the grid nz only when
            // the stream does not declare an explicit level count.
            int field_levels = nz_;
            if (var.field_levels > 0) {
                field_levels = var.field_levels;
            }
            if (var_name.empty()) {
                throw std::runtime_error("cece_data variable requires a non-empty name");
            }
            if (field_levels < 1) {
                throw std::runtime_error("cece_data variable '" + var_name + "' has invalid levels=" + std::to_string(field_levels));
            }
            var_names_.push_back(var_name);

            DeviceView view(var_name, nx_, ny_, field_levels);
            Kokkos::deep_copy(view, 0.0);
            field_views_[var_name] = view;
        }
    }
}

Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> CeceIO::GetFieldView(const std::string& name) {
    return field_views_.at(name);
}

void CeceIO::Finalize() {
    field_views_.clear();
    var_names_.clear();
}

}  // namespace io
}  // namespace cece
