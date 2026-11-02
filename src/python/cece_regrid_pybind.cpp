// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "cece/cece_regridder_utils.hpp"

namespace py = pybind11;

PYBIND11_MODULE(_cece_regrid, module) {
    module.doc() = "Explicit access to CECE's driver regridding implementation.";
    module.def("regrid_rectilinear", [](const std::vector<double>& input,
                                      const std::vector<double>& source_lon, const std::vector<double>& source_lat,
                                      const std::vector<double>& target_lon, const std::vector<double>& target_lat,
                                      const std::string& method) {
        const int sx = static_cast<int>(source_lon.size()), sy = static_cast<int>(source_lat.size());
        const int nx = static_cast<int>(target_lon.size()), ny = static_cast<int>(target_lat.size());
        if (sx < 2 || sy < 2 || nx < 2 || ny < 2 || input.size() != static_cast<std::size_t>(sx) * sy)
            throw std::invalid_argument("Rectilinear regridding needs two or more cells per axis and matching source values");
        if (method != "nearest" && method != "passthrough")
            throw std::invalid_argument("Unsupported regridding method");
        for (double value : input)
            if (!std::isfinite(value)) throw std::invalid_argument("Masked or non-finite source values are not supported");
        if (!Kokkos::is_initialized() || Kokkos::is_finalized())
            throw std::runtime_error("Kokkos must be initialized before regridding");
        cece::io::RegridPlan plan;
        cece::io::build_regrid_plan_from_coordinates(sx, sy, source_lon, source_lat, nx, ny, target_lon, target_lat,
                                                      method, 0, ny, "", plan);
        if (!plan.identity) {
            auto rows = plan.matrix.row_ptr();
            if (rows.extent(0) != static_cast<std::size_t>(nx) * ny + 1)
                throw std::runtime_error("Invalid regridding weight matrix");
            for (std::size_t i = 0; i < static_cast<std::size_t>(nx) * ny; ++i)
                if (rows(i) == rows(i + 1)) throw std::invalid_argument("Target grid contains unmapped cells");
        }
        std::vector<double> output;
        if (!cece::io::apply_regrid_plan(plan, 0, false, input.data(), sx, sy, nx, output))
            throw std::runtime_error("Unable to apply regridding plan");
        return output;
    });
}
