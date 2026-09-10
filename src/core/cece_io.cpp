#include "cece/cece_io.hpp"

#include <conf/conf.hpp>
#include <fstream>
#include <stdexcept>

namespace cece {
namespace io {

void CeceIO::Initialize(const std::string& config_file, int nx, int ny, int nz) {
    std::ifstream f(config_file);
    if (!f.good()) {
        throw std::runtime_error("File not found: " + config_file);
    }

    conf::Config config = conf::Config::from_file(config_file);
    nx_ = nx;
    ny_ = ny;
    nz_ = nz;

    if (config.has("cece_data.streams")) {
        conf::Value streams = config.at("cece_data.streams");
        for (std::size_t si = 0; si < streams.size(); ++si) {
            conf::Value stream = streams[si];
            conf::Value variables = stream["variables"];
            for (std::size_t vi = 0; vi < variables.size(); ++vi) {
                conf::Value var = variables[vi];
                std::string var_name;
                // Each field's layer count is per-variable (e.g. BDSNP land-use
                // categories use 24 layers); fall back to the grid nz only when
                // the stream does not declare an explicit level count.
                int field_levels = nz_;
                if (var.kind() == conf::Node_Kind::Scalar) {
                    var_name = var.as_string();
                } else if (var.kind() == conf::Node_Kind::Map && var["model"]) {
                    var_name = var["model"].as_string();
                    field_levels = var["levels"].int_or(nz_);
                } else {
                    throw std::runtime_error("cece_data variable must be a scalar name or a map containing 'model'");
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
