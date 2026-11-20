// SPDX-License-Identifier: Apache-2.0
#include "cece/cece_fire_input.hpp"

#include <netcdf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace cece {
namespace {

constexpr double kEarthRadiusM = 6371008.8;

[[noreturn]] void NetcdfError(int status, std::string_view operation, const std::string& path) {
    throw std::runtime_error("Fire product '" + path + "': " + std::string(operation) + ": " + nc_strerror(status));
}

class NetcdfFile {
   public:
    explicit NetcdfFile(std::string path) : path_(std::move(path)) {
        const int status = nc_open(path_.c_str(), NC_NOWRITE, &id_);
        if (status != NC_NOERR) NetcdfError(status, "cannot open NetCDF", path_);
    }

    NetcdfFile(const NetcdfFile&) = delete;
    NetcdfFile& operator=(const NetcdfFile&) = delete;

    ~NetcdfFile() {
        if (id_ >= 0) nc_close(id_);
    }

    const std::string& Path() const {
        return path_;
    }

    int Variable(std::string_view name) const {
        int id = -1;
        const std::string key(name);
        const int status = nc_inq_varid(id_, key.c_str(), &id);
        if (status != NC_NOERR) NetcdfError(status, "cannot find variable '" + key + "'", path_);
        return id;
    }

    std::size_t Dimension(std::string_view name) const {
        int id = -1;
        const std::string key(name);
        const int status = nc_inq_dimid(id_, key.c_str(), &id);
        if (status != NC_NOERR) NetcdfError(status, "cannot find dimension '" + key + "'", path_);
        std::size_t length = 0;
        const int length_status = nc_inq_dimlen(id_, id, &length);
        if (length_status != NC_NOERR) NetcdfError(length_status, "cannot read dimension '" + key + "'", path_);
        return length;
    }

    std::string GlobalText(std::string_view name) const {
        return TextAttribute(NC_GLOBAL, name);
    }

    std::string TextAttribute(int variable, std::string_view name) const {
        const std::string key(name);
        nc_type type = NC_NAT;
        std::size_t length = 0;
        const int status = nc_inq_att(id_, variable, key.c_str(), &type, &length);
        if (status != NC_NOERR) NetcdfError(status, "cannot find attribute '" + key + "'", path_);
        if (type != NC_CHAR || length == 0) {
            throw std::invalid_argument("Fire product '" + path_ + "': attribute '" + key + "' must be a nonempty string");
        }
        std::string value(length, '\0');
        const int read_status = nc_get_att_text(id_, variable, key.c_str(), value.data());
        if (read_status != NC_NOERR) NetcdfError(read_status, "cannot read attribute '" + key + "'", path_);
        while (!value.empty() && value.back() == '\0') value.pop_back();
        return value;
    }

    double NumericAttribute(int variable, std::string_view name) const {
        const std::string key(name);
        nc_type type = NC_NAT;
        const int status = nc_inq_atttype(id_, variable, key.c_str(), &type);
        if (status != NC_NOERR) NetcdfError(status, "cannot find attribute '" + key + "'", path_);
        if (type == NC_CHAR || type == NC_STRING) {
            throw std::invalid_argument("Fire product '" + path_ + "': attribute '" + key + "' must be numeric");
        }
        double value = 0;
        const int read_status = nc_get_att_double(id_, variable, key.c_str(), &value);
        if (read_status != NC_NOERR) NetcdfError(read_status, "cannot read attribute '" + key + "'", path_);
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Fire product '" + path_ + "': attribute '" + key + "' must be finite");
        }
        return value;
    }

    void RequireVariableDimensions(int variable, const std::vector<std::string>& expected) const {
        int dimensions = 0;
        int status = nc_inq_varndims(id_, variable, &dimensions);
        if (status != NC_NOERR) NetcdfError(status, "cannot inspect variable dimensions", path_);
        if (dimensions != static_cast<int>(expected.size())) {
            throw std::invalid_argument("Fire product '" + path_ + "': variable dimensions do not match the supported product layout");
        }
        std::vector<int> dimension_ids(expected.size());
        status = nc_inq_vardimid(id_, variable, dimension_ids.data());
        if (status != NC_NOERR) NetcdfError(status, "cannot inspect variable dimension ids", path_);
        for (std::size_t index = 0; index < expected.size(); ++index) {
            char name[NC_MAX_NAME + 1]{};
            status = nc_inq_dimname(id_, dimension_ids[index], name);
            if (status != NC_NOERR) NetcdfError(status, "cannot inspect variable dimension names", path_);
            if (expected[index] != name) {
                throw std::invalid_argument("Fire product '" + path_ + "': variable dimension order is unsupported");
            }
        }
    }

    std::vector<double> Read(std::string_view name, const std::vector<std::size_t>& start, const std::vector<std::size_t>& count,
                             const std::vector<std::string>& expected_dimensions) const {
        const int variable = Variable(name);
        RequireVariableDimensions(variable, expected_dimensions);
        if (start.size() != count.size() || start.size() != expected_dimensions.size()) {
            throw std::logic_error("Fire NetCDF reader received inconsistent hyperslab dimensions");
        }
        std::size_t size = 1;
        for (const auto length : count) {
            if (length == 0 || size > std::numeric_limits<std::size_t>::max() / length) {
                throw std::invalid_argument("Fire product '" + path_ + "': data hyperslab is empty or too large");
            }
            size *= length;
        }
        std::vector<double> values(size);
        const int status = nc_get_vara_double(id_, variable, start.data(), count.data(), values.data());
        if (status != NC_NOERR) NetcdfError(status, "cannot read variable '" + std::string(name) + "'", path_);
        return values;
    }

    std::vector<double> ReadVector(std::string_view name, std::size_t start, std::size_t count, std::string_view dimension) const {
        return Read(name, {start}, {count}, {std::string(dimension)});
    }

    std::vector<double> ReadGrid(std::string_view name, std::size_t x_start, std::size_t y_start, std::size_t width, std::size_t height,
                                 std::string_view y_dimension, std::string_view x_dimension) const {
        return Read(name, {y_start, x_start}, {height, width}, {std::string(y_dimension), std::string(x_dimension)});
    }

    std::vector<double> ReadSpecies(std::string_view name, std::size_t y_start, std::size_t x_start, std::size_t width, std::size_t height,
                                    std::string_view time_dimension, std::string_view y_dimension, std::string_view x_dimension) const {
        return Read(name, {0, y_start, x_start}, {1, height, width},
                    {std::string(time_dimension), std::string(y_dimension), std::string(x_dimension)});
    }

    double FillValue(int variable) const {
        nc_type type = NC_NAT;
        const int status = nc_inq_atttype(id_, variable, "_FillValue", &type);
        if (status == NC_ENOTATT) {
            throw std::invalid_argument("Fire product '" + path_ + "': mass variable is missing its _FillValue attribute");
        }
        if (status != NC_NOERR) NetcdfError(status, "cannot inspect _FillValue", path_);
        if (type == NC_CHAR || type == NC_STRING) {
            throw std::invalid_argument("Fire product '" + path_ + "': _FillValue must be numeric");
        }
        double value = 0;
        const int read_status = nc_get_att_double(id_, variable, "_FillValue", &value);
        if (read_status != NC_NOERR) NetcdfError(read_status, "cannot read _FillValue", path_);
        if (!std::isfinite(value)) throw std::invalid_argument("Fire product '" + path_ + "': _FillValue must be finite");
        return value;
    }

   private:
    std::string path_;
    int id_ = -1;
};

struct GridWindow {
    std::size_t x_start;
    std::size_t y_start;
    std::size_t width;
    std::size_t height;
};

GridWindow ResolveWindow(const std::optional<FireProductDecodeWindow>& requested, std::size_t nx, std::size_t ny) {
    const GridWindow window = requested ? GridWindow{requested->x_start, requested->y_start, requested->width, requested->height}
                                        : GridWindow{0, 0, nx, ny};
    if (window.width == 0 || window.height == 0 || window.x_start > nx || window.y_start > ny || window.width > nx - window.x_start ||
        window.height > ny - window.y_start || window.width > std::numeric_limits<std::size_t>::max() / window.height) {
        throw std::invalid_argument("Fire product decode window is empty or outside the source grid");
    }
    return window;
}

void RequireUnits(const NetcdfFile& file, std::string_view variable_name, std::string_view expected) {
    const auto variable = file.Variable(variable_name);
    const auto units = file.TextAttribute(variable, "units");
    if (units != expected) {
        throw std::invalid_argument("Fire product '" + file.Path() + "': variable '" + std::string(variable_name) + "' has unsupported units '" +
                                    units + "'");
    }
}

std::vector<double> Coordinates(const NetcdfFile& file, std::string_view name, std::size_t start, std::size_t count,
                                std::string_view dimension) {
    auto values = file.ReadVector(name, start, count, dimension);
    if (!std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("Fire product '" + file.Path() + "': coordinate variable '" + std::string(name) + "' has nonfinite values");
    }
    return values;
}

double SphericalCellArea(const std::array<double, 4>& longitude, const std::array<double, 4>& latitude) {
    double sum = 0;
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const std::size_t next = (corner + 1) % 4;
        const double delta_lon = std::remainder(longitude[next] - longitude[corner], 360.0) * std::numbers::pi / 180.0;
        const double lat_a = latitude[corner] * std::numbers::pi / 180.0;
        const double lat_b = latitude[next] * std::numbers::pi / 180.0;
        sum += delta_lon * (std::sin(lat_a) + std::sin(lat_b));
    }
    const double area = std::abs(sum) * (kEarthRadiusM * kEarthRadiusM) / 2.0;
    if (!std::isfinite(area) || area <= 0) throw std::invalid_argument("Fire product grid contains a degenerate geographic cell");
    return area;
}

std::shared_ptr<const FireGrid> MakeGrid(FireGridDefinition definition, const GridWindow& window) {
    std::ostringstream identity;
    identity << std::setprecision(std::numeric_limits<double>::max_digits10) << definition.grid_id << ':' << definition.crs << ':'
             << static_cast<int>(definition.longitude_convention) << ':' << window.x_start << ':' << window.y_start << ':' << window.width
             << ':' << window.height;
    const auto append_values = [&identity](const auto& values) {
        for (const auto& value : values) identity << ':' << value;
    };
    append_values(definition.longitude_deg);
    append_values(definition.latitude_deg);
    for (const auto& bounds : definition.longitude_bounds_deg) append_values(bounds);
    for (const auto& bounds : definition.latitude_bounds_deg) append_values(bounds);
    append_values(definition.area_m2);
    definition.grid_sha256 = FireContentSha256(identity.str());
    return std::make_shared<const FireGrid>(std::move(definition));
}

double NormalizeLongitude(double longitude) {
    longitude = std::fmod(longitude, 360.0);
    if (longitude < 0) longitude += 360.0;
    return longitude;
}

std::shared_ptr<const FireGrid> MakeGbbepxGrid(const NetcdfFile& file, const GridWindow& window) {
    FireGridDefinition definition;
    definition.grid_id = "GBBEPx-all01GRID-v4r0";
    definition.nx = window.width;
    definition.ny = window.height;
    definition.crs = "EPSG:4326";
    definition.longitude_convention = FireLongitudeConvention::ZeroTo360;

    const auto source_latitudes = Coordinates(file, "Latitude", 0, file.Dimension("Latitude"), "Latitude");
    const auto source_longitudes = Coordinates(file, "Longitude", 0, file.Dimension("Longitude"), "Longitude");
    const auto validate_spacing = [](const std::vector<double>& axis, std::string_view name) {
        for (std::size_t index = 1; index < axis.size(); ++index) {
            if (std::abs((axis[index] - axis[index - 1]) - 0.1) > 5e-5) {
                throw std::invalid_argument("GBBEPx " + std::string(name) + " coordinates are not the supported 0.1-degree grid");
            }
        }
    };
    validate_spacing(source_latitudes, "Latitude");
    validate_spacing(source_longitudes, "Longitude");
    if (source_latitudes.size() == 1801 &&
        (std::abs(source_latitudes.front() + 90.0) > 5e-5 || std::abs(source_latitudes.back() - 90.0) > 5e-5)) {
        throw std::invalid_argument("GBBEPx latitude coordinates do not match the supported global grid");
    }
    if (source_longitudes.size() == 3600 &&
        (std::abs(source_longitudes.front() + 180.0) > 5e-5 || std::abs(source_longitudes.back() - 179.9) > 5e-5)) {
        throw std::invalid_argument("GBBEPx longitude coordinates do not match the supported global grid");
    }

    const auto latitudes = Coordinates(file, "Latitude", window.y_start, window.height, "Latitude");
    auto longitudes = Coordinates(file, "Longitude", window.x_start, window.width, "Longitude");
    for (auto& longitude : longitudes) longitude = NormalizeLongitude(longitude);
    const auto cells = window.width * window.height;
    definition.longitude_deg.resize(cells);
    definition.latitude_deg.resize(cells);
    definition.longitude_bounds_deg.resize(cells);
    definition.latitude_bounds_deg.resize(cells);
    definition.area_m2.resize(cells);
    for (std::size_t row = 0; row < window.height; ++row) {
        const double latitude = latitudes[row];
        const double lower_latitude = std::max(-90.0, latitude - 0.05);
        const double upper_latitude = std::min(90.0, latitude + 0.05);
        if (!(upper_latitude > lower_latitude)) throw std::invalid_argument("GBBEPx latitude coordinates define a degenerate cell");
        for (std::size_t column = 0; column < window.width; ++column) {
            const auto cell = row * window.width + column;
            const double longitude = longitudes[column];
            const double lower_longitude = NormalizeLongitude(longitude - 0.05);
            const double upper_longitude = NormalizeLongitude(longitude + 0.05);
            definition.longitude_deg[cell] = longitude;
            definition.latitude_deg[cell] = latitude;
            definition.longitude_bounds_deg[cell] = {lower_longitude, upper_longitude, upper_longitude, lower_longitude};
            definition.latitude_bounds_deg[cell] = {lower_latitude, lower_latitude, upper_latitude, upper_latitude};
            definition.area_m2[cell] = SphericalCellArea(definition.longitude_bounds_deg[cell], definition.latitude_bounds_deg[cell]);
        }
    }
    return MakeGrid(std::move(definition), window);
}

std::shared_ptr<const FireGrid> MakeRaveGrid(const NetcdfFile& file, const GridWindow& window) {
    FireGridDefinition definition;
    definition.grid_id = "RAVE-HrlyEmiss-13km-v2r0";
    definition.nx = window.width;
    definition.ny = window.height;
    definition.crs = "EPSG:4326";
    definition.longitude_convention = FireLongitudeConvention::ZeroTo360;

    const auto centers_lon = file.ReadGrid("grid_lont", window.x_start, window.y_start, window.width, window.height, "grid_yt", "grid_xt");
    const auto centers_lat = file.ReadGrid("grid_latt", window.x_start, window.y_start, window.width, window.height, "grid_yt", "grid_xt");
    const auto corners_lon =
        file.ReadGrid("grid_lon", window.x_start, window.y_start, window.width + 1, window.height + 1, "grid_y", "grid_x");
    const auto corners_lat =
        file.ReadGrid("grid_lat", window.x_start, window.y_start, window.width + 1, window.height + 1, "grid_y", "grid_x");
    const std::size_t corner_width = window.width + 1;
    const std::size_t cells = window.width * window.height;
    definition.longitude_deg = centers_lon;
    definition.latitude_deg = centers_lat;
    definition.longitude_bounds_deg.resize(cells);
    definition.latitude_bounds_deg.resize(cells);
    definition.area_m2.resize(cells);
    for (std::size_t row = 0; row < window.height; ++row) {
        for (std::size_t column = 0; column < window.width; ++column) {
            const auto cell = row * window.width + column;
            const auto top_left = row * corner_width + column;
            const auto top_right = top_left + 1;
            const auto bottom_left = (row + 1) * corner_width + column;
            const auto bottom_right = bottom_left + 1;
            definition.longitude_bounds_deg[cell] = {corners_lon[top_left], corners_lon[top_right], corners_lon[bottom_right],
                                                     corners_lon[bottom_left]};
            definition.latitude_bounds_deg[cell] = {corners_lat[top_left], corners_lat[top_right], corners_lat[bottom_right],
                                                    corners_lat[bottom_left]};
            definition.area_m2[cell] =
                SphericalCellArea(definition.longitude_bounds_deg[cell], definition.latitude_bounds_deg[cell]);
        }
    }
    return MakeGrid(std::move(definition), window);
}

struct IntervalMetadata {
    FireUtcTime start;
    FireUtcTime end;
    FireUtcTime available_at;
};

IntervalMetadata ReadIntervalMetadata(const NetcdfFile& file, std::int64_t expected_duration_seconds) {
    const FireUtcTime start(file.GlobalText("time_coverage_start"));
    const FireUtcTime inclusive_end(file.GlobalText("time_coverage_end"));
    if (inclusive_end.EpochSeconds() < start.EpochSeconds()) {
        throw std::invalid_argument("Fire product '" + file.Path() + "': time_coverage_end precedes time_coverage_start");
    }
    const auto end = FireUtcTime::FromEpochSeconds(inclusive_end.EpochSeconds() + 1);
    if (end.EpochSeconds() - start.EpochSeconds() != expected_duration_seconds) {
        throw std::invalid_argument("Fire product '" + file.Path() + "': time coverage does not match the supported product interval");
    }
    return {start, end, FireUtcTime(file.GlobalText("date_created"))};
}

FireInputIdentity MakeInputIdentity(const NetcdfFile& file, const std::string& product_id, const std::string& product_version,
                                    const std::string& content_hash, const IntervalMetadata& interval,
                                    const FireProductDecodeOptions& options, std::shared_ptr<const FireGrid> grid) {
    if (!options.availability_cutoff) {
        throw std::invalid_argument("Fire product decode requires an explicit availability cutoff timestamp");
    }
    if (options.cutoff_mode != FireCutoffMode::Fixed && options.cutoff_mode != FireCutoffMode::HostSupplied) {
        throw std::invalid_argument("Fire product decode requires a fixed or host-supplied availability cutoff mode");
    }
    return FireInputIdentity({1,
                              product_id,
                              product_version,
                              product_version,
                              std::filesystem::path(file.Path()).filename().string(),
                              content_hash,
                              interval.start,
                              interval.end,
                              interval.available_at,
                              interval.available_at,
                              *options.availability_cutoff,
                              options.cutoff_mode,
                              std::move(grid)});
}

void SetAvailability(FireProductEmissionsIntervalData& data) {
    for (std::size_t species = 0; species < 11; ++species) {
        const auto canonical = static_cast<FireBulkSpecies>(species);
        const bool present = std::find(data.species.begin(), data.species.end(), canonical) != data.species.end();
        data.species_availability.emplace(canonical, present ? FireProductSpeciesAvailability::Available : FireProductSpeciesAvailability::Unavailable);
    }
}

std::vector<double> ReadRaveField(const NetcdfFile& file, const std::string& name, const std::string& units, const GridWindow& window) {
    const int variable = file.Variable(name);
    file.RequireVariableDimensions(variable, {"time", "grid_yt", "grid_xt"});
    if (file.TextAttribute(variable, "units") != units) {
        throw std::invalid_argument("RAVE variable '" + name + "' must use units '" + units + "'");
    }
    return file.ReadSpecies(name, window.y_start, window.x_start, window.width, window.height, "time", "grid_yt", "grid_xt");
}

std::vector<double> ReadGbbepxField(const NetcdfFile& file, const std::string& name, const GridWindow& window) {
    const int variable = file.Variable(name);
    file.RequireVariableDimensions(variable, {"Time", "Latitude", "Longitude"});
    if (file.TextAttribute(variable, "units") != "kg s-1 m-2") {
        throw std::invalid_argument("GBBEPx variable '" + name + "' must use units 'kg s-1 m-2'");
    }
    return file.ReadSpecies(name, window.y_start, window.x_start, window.width, window.height, "Time", "Latitude", "Longitude");
}

void ValidateWindowValues(const std::vector<double>& values, double fill_value, std::string_view source, std::vector<double>& output,
                          std::vector<bool>& missing) {
    if (values.size() != output.size() || values.size() != missing.size()) {
        throw std::logic_error("Fire product field size does not match the selected grid window");
    }
    for (std::size_t cell = 0; cell < values.size(); ++cell) {
        const double value = values[cell];
        if (value == fill_value) {
            output[cell] = 0;
            missing[cell] = true;
        } else if (!std::isfinite(value) || value < 0) {
            throw std::invalid_argument("Fire product '" + std::string(source) + "' contains a negative or nonfinite emission value");
        } else {
            output[cell] = value;
        }
    }
}

std::vector<double> ReadRaveDiagnostic(const NetcdfFile& file, const std::string& name, const std::string& units, const GridWindow& window) {
    auto values = ReadRaveField(file, name, units, window);
    const double fill = file.FillValue(file.Variable(name));
    for (auto& value : values) {
        if (value == fill) {
            value = 0;
        } else if (!std::isfinite(value) || value < 0) {
            throw std::invalid_argument("RAVE diagnostic '" + name + "' contains a negative or nonfinite value");
        }
    }
    return values;
}

std::vector<double> ReadGbbepxFrp(const NetcdfFile& file, const GridWindow& window) {
    const int variable = file.Variable("MeanFRP");
    file.RequireVariableDimensions(variable, {"Time", "Latitude", "Longitude"});
    if (file.TextAttribute(variable, "units") != "MW") {
        throw std::invalid_argument("GBBEPx MeanFRP diagnostic must use units 'MW'");
    }
    auto values = file.ReadSpecies("MeanFRP", window.y_start, window.x_start, window.width, window.height, "Time", "Latitude", "Longitude");
    const double fill = file.FillValue(variable);
    for (auto& value : values) {
        if (value == fill) value = 0;
        if (!std::isfinite(value) || value < 0) throw std::invalid_argument("GBBEPx MeanFRP contains a negative or nonfinite value");
    }
    return values;
}

}  // namespace

FireProductEmissionsInterval DecodeRaveProductFile(const std::string& path, const FireProductDecodeOptions& options) {
    NetcdfFile file(path);
    constexpr std::string_view product_id = "RAVE-HrlyEmiss-13km";
    constexpr std::string_view version = "v2r0";
    if (file.GlobalText("PRODUCT_ALGORITHM_VERSION") != version || file.GlobalText("TIME_RANGE") != "1 hour") {
        throw std::invalid_argument("RAVE decoder supports only the v2r0 one-hour product");
    }
    if (file.Dimension("time") != 1 || file.Dimension("grid_yt") + 1 != file.Dimension("grid_y") ||
        file.Dimension("grid_xt") + 1 != file.Dimension("grid_x")) {
        throw std::invalid_argument("RAVE decoder requires one timestep with edge coordinates one larger than cell dimensions");
    }
    if (options.scaling != FireProductScalingKind::Unscaled && options.scaling != FireProductScalingKind::Applied) {
        throw std::invalid_argument("RAVE decoder received an invalid scaling selection");
    }
    const auto window = ResolveWindow(options.window, file.Dimension("grid_xt"), file.Dimension("grid_yt"));
    const auto content_hash = FireFileSha256(path);
    auto grid = MakeRaveGrid(file, window);
    const auto interval = ReadIntervalMetadata(file, 3600);
    auto identity = MakeInputIdentity(file, std::string(product_id), std::string(version), content_hash, interval, options, std::move(grid));

    FireProductIdentity product_identity{std::string(product_id),
                                         std::string(version),
                                         options.scaling == FireProductScalingKind::Applied ? "scaled" : "unscaled",
                                         std::move(identity)};
    FireProductEmissionsIntervalData data(std::move(product_identity));
    const std::array<std::pair<std::string_view, FireBulkSpecies>, 11> species{{
        {"CO2", FireBulkSpecies::CO2}, {"CO", FireBulkSpecies::CO},     {"CH4", FireBulkSpecies::CH4}, {"PM25", FireBulkSpecies::PM25},
        {"OC", FireBulkSpecies::OC},   {"BC", FireBulkSpecies::BC},     {"SO2", FireBulkSpecies::SO2}, {"TPM", FireBulkSpecies::TPM},
        {"NH3", FireBulkSpecies::NH3}, {"NOx", FireBulkSpecies::NOX},   {"VOCs", FireBulkSpecies::VOC},
    }};
    const std::size_t cells = window.width * window.height;
    data.quality_state.assign(cells, FireQualityState::ValidZero);
    data.coverage_fraction.assign(cells, 1.0);
    data.species.reserve(species.size());
    data.source_values.assign(species.size() * cells, 0);
    std::vector<bool> missing_union(cells, false);
    std::vector<bool> missing_intersection(cells, true);
    for (std::size_t index = 0; index < species.size(); ++index) {
        const std::string base(species[index].first);
        const std::string variable = options.scaling == FireProductScalingKind::Applied ? base + "_scaled" : base;
        const auto values = ReadRaveField(file, variable, "kg", window);
        const double fill = file.FillValue(file.Variable(variable));
        std::vector<bool> missing(cells, false);
        std::vector<double> output(cells, 0);
        ValidateWindowValues(values, fill, variable, output, missing);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            missing_union[cell] = missing_union[cell] || missing[cell];
            missing_intersection[cell] = missing_intersection[cell] && missing[cell];
            data.source_values[index * cells + cell] = output[cell];
            if (output[cell] > 0) data.quality_state[cell] = FireQualityState::Observed;
        }
        data.species.push_back(species[index].second);
        data.quantity_semantics.emplace(species[index].second, FireProductQuantitySemantics{FireProductQuantityKind::IntervalMass, "kg/cell/interval"});
    }
    for (std::size_t cell = 0; cell < cells; ++cell) {
        if (missing_union[cell] != missing_intersection[cell]) {
            throw std::invalid_argument("RAVE mass species have inconsistent fill coverage; cannot normalize this interval safely");
        }
        if (missing_intersection[cell] && data.quality_state[cell] == FireQualityState::Observed) {
            throw std::invalid_argument("RAVE marks an active emission cell as missing for another mass species");
        }
    }
    data.optional_diagnostics.frp_mw = ReadRaveDiagnostic(file, "FRP_MEAN", "MW", window);
    data.optional_diagnostics.fre_mj = ReadRaveDiagnostic(file, "FRE", "MJ", window);
    if (options.scaling == FireProductScalingKind::Applied) {
        data.scaling_state = {FireProductScalingKind::Applied,
                              FireProfileIdentity{"RAVE-embedded-scaled-variant", std::string(version), content_hash}};
    }
    SetAvailability(data);
    return FireProductEmissionsInterval(std::move(data));
}

FireProductEmissionsInterval DecodeGbbepxProductFile(const std::string& path, const FireProductDecodeOptions& options) {
    NetcdfFile file(path);
    constexpr std::string_view product_id = "GBBEPx-all01GRID";
    constexpr std::string_view version = "v4r0";
    if (file.GlobalText("PRODUCT_ALGORITHM_VERSION") != version || file.GlobalText("TIME_RANGE") != "day") {
        throw std::invalid_argument("GBBEPx decoder supports only the v4r0 daily product");
    }
    if (options.scaling != FireProductScalingKind::Unscaled) {
        throw std::invalid_argument("GBBEPx v4r0 does not provide a scaled variant");
    }
    if (file.Dimension("Time") != 1) throw std::invalid_argument("GBBEPx decoder requires exactly one daily timestep per file");
    const auto window = ResolveWindow(options.window, file.Dimension("Longitude"), file.Dimension("Latitude"));
    const auto content_hash = FireFileSha256(path);
    auto grid = MakeGbbepxGrid(file, window);
    const auto interval = ReadIntervalMetadata(file, 86400);
    auto identity = MakeInputIdentity(file, std::string(product_id), std::string(version), content_hash, interval, options, std::move(grid));

    FireProductEmissionsIntervalData data(
        {std::string(product_id), std::string(version), "unscaled", std::move(identity)});
    const std::array<std::pair<std::string_view, FireBulkSpecies>, 8> species{{
        {"CO2", FireBulkSpecies::CO2}, {"CO", FireBulkSpecies::CO}, {"SO2", FireBulkSpecies::SO2}, {"OC", FireBulkSpecies::OC},
        {"BC", FireBulkSpecies::BC}, {"PM2.5", FireBulkSpecies::PM25}, {"NOx", FireBulkSpecies::NOX}, {"NH3", FireBulkSpecies::NH3},
    }};
    const std::size_t cells = window.width * window.height;
    data.quality_state.assign(cells, FireQualityState::ValidZero);
    data.coverage_fraction.assign(cells, 1.0);
    data.source_values.assign(species.size() * cells, 0);
    data.species.reserve(species.size());
    for (std::size_t index = 0; index < species.size(); ++index) {
        const std::string variable(species[index].first);
        const auto values = ReadGbbepxField(file, variable, window);
        const double fill = file.FillValue(file.Variable(variable));
        for (std::size_t cell = 0; cell < cells; ++cell) {
            const double value = values[cell];
            if (!std::isfinite(value) || value < 0) {
                throw std::invalid_argument("GBBEPx variable '" + variable + "' contains a negative or nonfinite emission flux");
            }
            data.source_values[index * cells + cell] = value == fill ? 0 : value;
            if (value != 0) data.quality_state[cell] = FireQualityState::Observed;
        }
        data.species.push_back(species[index].second);
        data.quantity_semantics.emplace(species[index].second, FireProductQuantitySemantics{FireProductQuantityKind::Flux, "kg m-2 s-1"});
    }
    data.optional_diagnostics.frp_mw = ReadGbbepxFrp(file, window);
    SetAvailability(data);
    return FireProductEmissionsInterval(std::move(data));
}

}  // namespace cece
