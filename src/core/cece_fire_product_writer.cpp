// SPDX-License-Identifier: Apache-2.0
#include "cece/cece_fire_input.hpp"

#include <netcdf.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cece {
namespace {

void NetcdfCheck(int status, std::string_view operation) {
    if (status != NC_NOERR) throw std::runtime_error(std::string(operation) + ": " + nc_strerror(status));
}

void TextAttribute(int file, int variable, std::string_view name, std::string_view value) {
    NetcdfCheck(nc_put_att_text(file, variable, std::string(name).c_str(), value.size(), value.data()), "cannot write NetCDF text attribute");
}

int DefineVariable(int file, std::string_view name, nc_type type, const std::vector<int>& dimensions) {
    int variable = -1;
    NetcdfCheck(nc_def_var(file, std::string(name).c_str(), type, static_cast<int>(dimensions.size()), dimensions.data(), &variable),
                "cannot define NetCDF variable");
    if (type == NC_DOUBLE) NetcdfCheck(nc_def_var_deflate(file, variable, 1, 1, 2), "cannot configure NetCDF compression");
    return variable;
}

void DefineAndWriteDouble(int file, int variable, const std::vector<double>& values) {
    NetcdfCheck(nc_put_var_double(file, variable, values.data()), "cannot write NetCDF double variable");
}

std::string QuantityKindName(FireProductQuantityKind kind) {
    switch (kind) {
        case FireProductQuantityKind::IntervalMass:
            return "interval_mass";
        case FireProductQuantityKind::MeanRate:
            return "mean_rate";
        case FireProductQuantityKind::Flux:
            return "flux";
    }
    throw std::invalid_argument("fire product has an invalid source quantity kind");
}

std::string QualityName(FireQualityState state) {
    switch (state) {
        case FireQualityState::Observed:
            return "observed";
        case FireQualityState::Reconstructed:
            return "reconstructed";
        case FireQualityState::ValidZero:
            return "valid_zero";
        default:
            throw std::invalid_argument("fire product contains a non-normalizable quality state");
    }
}

class NetcdfOutput {
   public:
    explicit NetcdfOutput(const std::string& path) {
        NetcdfCheck(nc_create(path.c_str(), NC_NETCDF4 | NC_CLOBBER, &file_), "cannot create normalized NetCDF file");
    }

    NetcdfOutput(const NetcdfOutput&) = delete;
    NetcdfOutput& operator=(const NetcdfOutput&) = delete;

    ~NetcdfOutput() {
        if (file_ >= 0) nc_close(file_);
    }

    int Id() const {
        return file_;
    }

    void Close() {
        const int file = file_;
        file_ = -1;
        NetcdfCheck(nc_close(file), "cannot close normalized NetCDF file");
    }

   private:
    int file_ = -1;
};

}  // namespace

void WriteNormalizedFireProductFile(const FireProductEmissionsInterval& product, const FireNormalizedProductWriteOptions& options,
                                    const std::string& path) {
    if (options.source_id.empty()) throw std::invalid_argument("normalized fire product requires a source_id");
    if (options.time_allocation != "uniform" && options.time_allocation != "source_resolved" && options.time_allocation != "profile") {
        throw std::invalid_argument("normalized fire product requires a supported temporal allocation policy");
    }
    if ((options.time_allocation == "profile") != options.time_profile.has_value()) {
        throw std::invalid_argument("normalized fire product profile identity must match its temporal allocation policy");
    }

    const auto& data = product.Data();
    if (data.chemistry_state != FireProductChemistryState::Bulk) {
        throw std::invalid_argument("normalized product writer currently requires bulk chemistry");
    }
    const auto& metadata = data.product_identity.common_identity.Metadata();
    const auto& grid = metadata.grid_definition->Definition();
    const std::size_t cells = grid.nx * grid.ny;
    if (grid.nx == 0 || grid.ny == 0 || grid.nx > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        grid.ny > static_cast<std::size_t>(std::numeric_limits<int>::max()) || cells != grid.area_m2.size()) {
        throw std::invalid_argument("normalized fire product has an invalid grid shape");
    }

    NetcdfOutput output(path);
    const int file = output.Id();
    const int schema_version = 1;
    NetcdfCheck(nc_put_att_int(file, NC_GLOBAL, "fire_normalized_schema_version", NC_INT, 1, &schema_version),
                "cannot write normalized schema version");
    NetcdfCheck(nc_put_att_int(file, NC_GLOBAL, "source_identity_schema_version", NC_INT, 1, &metadata.schema_version),
                "cannot write source identity schema version");
    int time_dim = -1;
    int y_dim = -1;
    int x_dim = -1;
    int vertex_dim = -1;
    int time_bound_dim = -1;
    NetcdfCheck(nc_def_dim(file, "time", 1, &time_dim), "cannot define time dimension");
    NetcdfCheck(nc_def_dim(file, "y", grid.ny, &y_dim), "cannot define y dimension");
    NetcdfCheck(nc_def_dim(file, "x", grid.nx, &x_dim), "cannot define x dimension");
    NetcdfCheck(nc_def_dim(file, "vertex", 4, &vertex_dim), "cannot define grid vertex dimension");
    NetcdfCheck(nc_def_dim(file, "time_bound", 2, &time_bound_dim), "cannot define time bound dimension");

    const std::array<int, 1> time_dims{time_dim};
    const std::array<int, 2> horizontal_dims{y_dim, x_dim};
    const std::array<int, 3> bounds_dims{y_dim, x_dim, vertex_dim};
    const std::array<int, 2> time_bounds_dims{time_dim, time_bound_dim};
    const std::array<int, 3> species_dims{time_dim, y_dim, x_dim};
    const int time_var = DefineVariable(file, "time", NC_DOUBLE, std::vector<int>(time_dims.begin(), time_dims.end()));
    const int time_bounds_var = DefineVariable(file, "time_bounds", NC_DOUBLE, std::vector<int>(time_bounds_dims.begin(), time_bounds_dims.end()));
    const int longitude_var = DefineVariable(file, "longitude", NC_DOUBLE, std::vector<int>(horizontal_dims.begin(), horizontal_dims.end()));
    const int latitude_var = DefineVariable(file, "latitude", NC_DOUBLE, std::vector<int>(horizontal_dims.begin(), horizontal_dims.end()));
    const int longitude_bounds_var = DefineVariable(file, "longitude_bounds", NC_DOUBLE, std::vector<int>(bounds_dims.begin(), bounds_dims.end()));
    const int latitude_bounds_var = DefineVariable(file, "latitude_bounds", NC_DOUBLE, std::vector<int>(bounds_dims.begin(), bounds_dims.end()));
    const int area_var = DefineVariable(file, "cell_area", NC_DOUBLE, std::vector<int>(horizontal_dims.begin(), horizontal_dims.end()));
    const int coverage_var = DefineVariable(file, "coverage_fraction", NC_DOUBLE, std::vector<int>(horizontal_dims.begin(), horizontal_dims.end()));
    const int quality_var = DefineVariable(file, "quality_state", NC_BYTE, std::vector<int>(horizontal_dims.begin(), horizontal_dims.end()));
    const int crs_var = DefineVariable(file, "crs", NC_INT, {});

    TextAttribute(file, NC_GLOBAL, "Conventions", "CF-1.10");
    TextAttribute(file, NC_GLOBAL, "title", "Normalized CECE fire product interval");
    TextAttribute(file, NC_GLOBAL, "source_id", metadata.source_id);
    TextAttribute(file, NC_GLOBAL, "source_manifest_id", options.source_id);
    TextAttribute(file, NC_GLOBAL, "delivery_id", metadata.delivery_id);
    TextAttribute(file, NC_GLOBAL, "product_id", data.product_identity.product_id);
    TextAttribute(file, NC_GLOBAL, "product_version", data.product_identity.version);
    TextAttribute(file, NC_GLOBAL, "product_variant", data.product_identity.variant);
    TextAttribute(file, NC_GLOBAL, "algorithm_version", metadata.algorithm_version);
    TextAttribute(file, NC_GLOBAL, "source_content_sha256", metadata.content_sha256);
    TextAttribute(file, NC_GLOBAL, "interval_start", metadata.interval_start.ToString());
    TextAttribute(file, NC_GLOBAL, "interval_end", metadata.interval_end.ToString());
    TextAttribute(file, NC_GLOBAL, "acquired_at", metadata.acquired_at.ToString());
    TextAttribute(file, NC_GLOBAL, "available_at", metadata.available_at.ToString());
    TextAttribute(file, NC_GLOBAL, "availability_cutoff", metadata.availability_cutoff.ToString());
    TextAttribute(file, NC_GLOBAL, "cutoff_mode", metadata.cutoff_mode == FireCutoffMode::Fixed ? "fixed" : "host_supplied");
    TextAttribute(file, NC_GLOBAL, "grid_id", grid.grid_id);
    TextAttribute(file, NC_GLOBAL, "grid_sha256", grid.grid_sha256);
    TextAttribute(file, NC_GLOBAL, "crs", grid.crs);
    TextAttribute(file, NC_GLOBAL, "longitude_convention",
                  grid.longitude_convention == FireLongitudeConvention::ZeroTo360 ? "0_to_360" : "minus180_to_180");
    TextAttribute(file, NC_GLOBAL, "chemistry_state", "bulk");
    TextAttribute(file, NC_GLOBAL, "scaling_state",
                  data.scaling_state.kind == FireProductScalingKind::Applied ? "applied" : "unscaled");
    if (data.scaling_state.profile) {
        TextAttribute(file, NC_GLOBAL, "scaling_profile_id", data.scaling_state.profile->id);
        TextAttribute(file, NC_GLOBAL, "scaling_profile_version", data.scaling_state.profile->version);
        TextAttribute(file, NC_GLOBAL, "scaling_profile_sha256", data.scaling_state.profile->content_sha256);
    }
    TextAttribute(file, NC_GLOBAL, "product_time_allocation", options.time_allocation);
    if (options.time_profile) {
        TextAttribute(file, NC_GLOBAL, "product_time_profile_id", options.time_profile->id);
        TextAttribute(file, NC_GLOBAL, "product_time_profile_version", options.time_profile->version);
        TextAttribute(file, NC_GLOBAL, "product_time_profile_sha256", options.time_profile->content_sha256);
    }
    std::string available_species;
    std::string unavailable_species;
    for (const auto& [species, availability] : data.species_availability) {
        std::string& list = availability == FireProductSpeciesAvailability::Available ? available_species : unavailable_species;
        if (!list.empty()) list += ',';
        list += FireSpeciesName(species);
    }
    TextAttribute(file, NC_GLOBAL, "available_species", available_species);
    if (!unavailable_species.empty()) TextAttribute(file, NC_GLOBAL, "unavailable_species", unavailable_species);

    TextAttribute(file, time_var, "standard_name", "time");
    TextAttribute(file, time_var, "units", "seconds since 1970-01-01 00:00:00 UTC");
    TextAttribute(file, time_var, "calendar", "proleptic_gregorian");
    TextAttribute(file, time_var, "bounds", "time_bounds");
    TextAttribute(file, time_bounds_var, "units", "seconds since 1970-01-01 00:00:00 UTC");
    TextAttribute(file, longitude_var, "standard_name", "longitude");
    TextAttribute(file, longitude_var, "units", "degrees_east");
    TextAttribute(file, longitude_var, "bounds", "longitude_bounds");
    TextAttribute(file, latitude_var, "standard_name", "latitude");
    TextAttribute(file, latitude_var, "units", "degrees_north");
    TextAttribute(file, latitude_var, "bounds", "latitude_bounds");
    TextAttribute(file, area_var, "standard_name", "cell_area");
    TextAttribute(file, area_var, "units", "m2");
    TextAttribute(file, coverage_var, "long_name", "fraction of valid product coverage");
    TextAttribute(file, coverage_var, "units", "1");
    TextAttribute(file, quality_var, "long_name", "normalized fire product quality state");
    const std::array<signed char, 3> quality_values{0, 1, 2};
    NetcdfCheck(nc_put_att_schar(file, quality_var, "flag_values", NC_BYTE, quality_values.size(), quality_values.data()),
                "cannot write quality flag values");
    TextAttribute(file, quality_var, "flag_meanings", "observed reconstructed valid_zero");
    NetcdfCheck(nc_put_att_int(file, crs_var, "epsg_code", NC_INT, 1, std::array<int, 1>{4326}.data()),
                "cannot write CRS code");
    TextAttribute(file, crs_var, "grid_mapping_name", "latitude_longitude");
    TextAttribute(file, crs_var, "spatial_ref", grid.crs);

    std::vector<int> species_variables;
    species_variables.reserve(data.species.size());
    for (const auto species : data.species) {
        const std::string name = "fire_mass_" + std::string(FireSpeciesName(species));
        const int variable = DefineVariable(file, name, NC_DOUBLE, std::vector<int>(species_dims.begin(), species_dims.end()));
        TextAttribute(file, variable, "long_name", "fire " + std::string(FireSpeciesName(species)) + " interval mass");
        TextAttribute(file, variable, "units", "kg cell-1 interval-1");
        TextAttribute(file, variable, "coordinates", "latitude longitude");
        TextAttribute(file, variable, "cell_measures", "area: cell_area");
        TextAttribute(file, variable, "grid_mapping", "crs");
        const auto semantics = data.quantity_semantics.find(species);
        if (semantics == data.quantity_semantics.end()) throw std::invalid_argument("fire product is missing source quantity semantics");
        TextAttribute(file, variable, "source_units", semantics->second.units);
        TextAttribute(file, variable, "source_quantity_kind", QuantityKindName(semantics->second.kind));
        species_variables.push_back(variable);
    }

    int frp_var = -1;
    if (data.optional_diagnostics.frp_mw) {
        frp_var = DefineVariable(file, "fire_frp_mean", NC_DOUBLE, std::vector<int>(species_dims.begin(), species_dims.end()));
        TextAttribute(file, frp_var, "long_name", "product mean fire radiative power diagnostic");
        TextAttribute(file, frp_var, "units", "MW");
    }
    int fre_var = -1;
    if (data.optional_diagnostics.fre_mj) {
        fre_var = DefineVariable(file, "fire_fre", NC_DOUBLE, std::vector<int>(species_dims.begin(), species_dims.end()));
        TextAttribute(file, fre_var, "long_name", "product fire radiative energy diagnostic");
        TextAttribute(file, fre_var, "units", "MJ");
    }
    int dry_matter_var = -1;
    if (data.optional_diagnostics.dry_matter_kg) {
        dry_matter_var = DefineVariable(file, "fire_dry_matter", NC_DOUBLE, std::vector<int>(species_dims.begin(), species_dims.end()));
        TextAttribute(file, dry_matter_var, "long_name", "product dry matter diagnostic");
        TextAttribute(file, dry_matter_var, "units", "kg cell-1 interval-1");
    }

    NetcdfCheck(nc_enddef(file), "cannot finalize normalized NetCDF schema");
    const double interval_start = static_cast<double>(metadata.interval_start.EpochSeconds());
    const double interval_end = static_cast<double>(metadata.interval_end.EpochSeconds());
    const std::array<double, 1> time{interval_start + (interval_end - interval_start) / 2.0};
    const std::array<double, 2> time_bounds{interval_start, interval_end};
    DefineAndWriteDouble(file, time_var, {time.begin(), time.end()});
    DefineAndWriteDouble(file, time_bounds_var, {time_bounds.begin(), time_bounds.end()});
    DefineAndWriteDouble(file, longitude_var, grid.longitude_deg);
    DefineAndWriteDouble(file, latitude_var, grid.latitude_deg);
    std::vector<double> longitude_bounds;
    std::vector<double> latitude_bounds;
    longitude_bounds.reserve(cells * 4);
    latitude_bounds.reserve(cells * 4);
    for (const auto& bounds : grid.longitude_bounds_deg) longitude_bounds.insert(longitude_bounds.end(), bounds.begin(), bounds.end());
    for (const auto& bounds : grid.latitude_bounds_deg) latitude_bounds.insert(latitude_bounds.end(), bounds.begin(), bounds.end());
    DefineAndWriteDouble(file, longitude_bounds_var, longitude_bounds);
    DefineAndWriteDouble(file, latitude_bounds_var, latitude_bounds);
    DefineAndWriteDouble(file, area_var, grid.area_m2);
    DefineAndWriteDouble(file, coverage_var, data.coverage_fraction);

    std::vector<signed char> quality(cells);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        switch (data.quality_state[cell]) {
            case FireQualityState::Observed:
                quality[cell] = 0;
                break;
            case FireQualityState::Reconstructed:
                quality[cell] = 1;
                break;
            case FireQualityState::ValidZero:
                quality[cell] = 2;
                break;
            default:
                throw std::invalid_argument("fire product contains an unsupported quality state");
        }
    }
    NetcdfCheck(nc_put_var_schar(file, quality_var, quality.data()), "cannot write quality state");
    const auto& masses = product.MassKg();
    for (std::size_t species_index = 0; species_index < species_variables.size(); ++species_index) {
        std::array<std::size_t, 3> start{0, 0, 0};
        std::array<std::size_t, 3> count{1, grid.ny, grid.nx};
        NetcdfCheck(nc_put_vara_double(file, species_variables[species_index], start.data(), count.data(), masses.data() + species_index * cells),
                    "cannot write normalized species mass");
    }
    if (frp_var >= 0) DefineAndWriteDouble(file, frp_var, *data.optional_diagnostics.frp_mw);
    if (fre_var >= 0) DefineAndWriteDouble(file, fre_var, *data.optional_diagnostics.fre_mj);
    if (dry_matter_var >= 0) DefineAndWriteDouble(file, dry_matter_var, *data.optional_diagnostics.dry_matter_kg);
    output.Close();
}

}  // namespace cece
