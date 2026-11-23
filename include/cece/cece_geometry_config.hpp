// SPDX-License-Identifier: Apache-2.0
#ifndef CECE_GEOMETRY_CONFIG_HPP
#define CECE_GEOMETRY_CONFIG_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace cece::io {

/// Explicit policy for constructing cell boundaries from centers.
enum class ReconstructionModel : std::uint8_t { None, RectilinearMidpoint, GaussianLatLon, CurvilinearApproximate };

/// Parse a case-insensitive configuration value into a geometry policy.
ReconstructionModel parse_reconstruction_model(const std::string& name);

/// Regridding methods and their normalized configuration values.
enum class RegridMethod : std::uint8_t { Nearest, Bilinear, Bicubic, Conservative1, Conservative2, Passthrough };

struct RegridMethodSettings {
    RegridMethod method{RegridMethod::Conservative1};
    std::string canonical_name{"consd"};
    std::string normalization{"dstarea"};
    std::string unmapped{"ignore"};
};

/// Resolve a supported method name, returning no value for unsupported names.
std::optional<RegridMethodSettings> parse_regrid_method(const std::string& name);

}  // namespace cece::io
#endif
