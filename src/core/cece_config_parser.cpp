#include <sys/stat.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#include "cece/cece_config.hpp"
#include "conf/config.hpp"

namespace cece {

CeceConfig ParseConfig(const std::string& filename) {
    struct stat buffer;
    if (stat(filename.c_str(), &buffer) != 0) throw std::runtime_error("File not found: " + filename);
    conf::Config parsed = conf::Config::from_file(filename);
    conf::Value root = parsed.root();
    CeceConfig config;

    auto string_or = [](const conf::Value& node, const std::string& key, const std::string& fallback = std::string{}) {
        return node[key].string_or(fallback);
    };

    conf::Value species = root["species"];
    if (species && species.kind() == conf::Node_Kind::Map) {
        for (const auto& species_name : species.keys()) {
            conf::Value entries = species[species_name];
            std::vector<EmissionLayer> layers;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                conf::Value node = entries[i];
                EmissionLayer layer;
                layer.operation = node["operation"].as_string();
                layer.field_name = node["field"].as_string();
                conf::Value masks = node["mask"];
                if (masks)
                    layer.masks = masks.kind() == conf::Node_Kind::Sequence ? masks.as_string_list() : std::vector<std::string>{masks.as_string()};
                layer.scale = node["scale"].double_or(layer.scale);
                layer.hierarchy = node["hierarchy"].int_or(layer.hierarchy);
                layer.category = string_or(node, "category", layer.category);
                if (node["scale_fields"]) layer.scale_fields = node["scale_fields"].as_string_list();
                layer.diurnal_cycle = string_or(node, "diurnal_cycle");
                layer.weekly_cycle = string_or(node, "weekly_cycle");
                layer.seasonal_cycle = string_or(node, "seasonal_cycle");
                conf::Value vdist = node["vdist"];
                if (vdist && vdist.is_defined()) {
                    std::string method = string_or(vdist, "method");
                    if (method == "single" || method.empty())
                        layer.vdist_method = VerticalDistributionMethod::SINGLE;
                    else if (method == "range")
                        layer.vdist_method = VerticalDistributionMethod::RANGE;
                    else if (method == "pressure")
                        layer.vdist_method = VerticalDistributionMethod::PRESSURE;
                    else if (method == "height")
                        layer.vdist_method = VerticalDistributionMethod::HEIGHT;
                    else if (method == "pbl")
                        layer.vdist_method = VerticalDistributionMethod::PBL;
                    else
                        throw std::invalid_argument("Unknown vertical distribution method '" + method + "' in species '" + species_name + "'");
                    layer.vdist_layer_start = vdist["layer_start"].int_or(layer.vdist_layer_start);
                    layer.vdist_layer_end = vdist["layer_end"].int_or(layer.vdist_layer_end);
                    layer.vdist_p_start = vdist["p_start"].double_or(layer.vdist_p_start);
                    layer.vdist_p_end = vdist["p_end"].double_or(layer.vdist_p_end);
                    layer.vdist_h_start = vdist["h_start"].double_or(layer.vdist_h_start);
                    layer.vdist_h_end = vdist["h_end"].double_or(layer.vdist_h_end);
                }
                layers.push_back(std::move(layer));
            }
            config.species_layers[species_name] = std::move(layers);
        }
    }

    auto read_string_map = [](const conf::Value& node, auto& output) {
        if (!node || node.kind() != conf::Node_Kind::Map) return;
        for (const auto& key : node.keys()) output[key] = node[key].as_string();
    };
    read_string_map(root["meteorology"], config.met_mapping);
    read_string_map(root["scale_factors"], config.scale_factor_mapping);
    read_string_map(root["masks"], config.mask_mapping);
    conf::Value registry = root["met_registry"];
    if (registry && registry.kind() == conf::Node_Kind::Map)
        for (const auto& key : registry.keys()) {
            conf::Value value = registry[key];
            config.met_registry[key] =
                value.kind() == conf::Node_Kind::Sequence ? value.as_string_list() : std::vector<std::string>{value.as_string()};
        }
    auto read_cycles = [](const conf::Value& node, auto& output) {
        if (!node || node.kind() != conf::Node_Kind::Map) return;
        for (const auto& key : node.keys()) output[key].factors = node[key].as_double_list();
    };
    read_cycles(root["temporal_cycles"], config.temporal_cycles);
    read_cycles(root["temporal_profiles"], config.temporal_profiles);

    conf::Value schemes = root["physics_schemes"];
    for (std::size_t i = 0; schemes && i < schemes.size(); ++i) {
        conf::Value node = schemes[i];
        PhysicsSchemeConfig scheme;
        scheme.name = node["name"].as_string();
        scheme.language = string_or(node, "language", "cpp");
        scheme.language_type = StringToSchemeLanguage(scheme.language);
        if (node["options"]) scheme.options = node["options"];
        scheme.refresh_interval_seconds = node["refresh_interval_seconds"].int_or(0);
        config.physics_schemes.push_back(std::move(scheme));
    }

    conf::Value diagnostics = root["diagnostics"];
    if (diagnostics) {
        if (diagnostics.kind() == conf::Node_Kind::Sequence)
            config.diagnostics.variables = diagnostics.as_string_list();
        else {
            config.diagnostics.output_interval_seconds = diagnostics["output_interval"].int_or(0);
            config.diagnostics.grid_type = string_or(diagnostics, "grid_type", config.diagnostics.grid_type);
            config.diagnostics.grid_file = string_or(diagnostics, "grid_file");
            config.diagnostics.nx = diagnostics["nx"].int_or(0);
            config.diagnostics.ny = diagnostics["ny"].int_or(0);
            if (diagnostics["variables"]) config.diagnostics.variables = diagnostics["variables"].as_string_list();
        }
    }

    conf::Value vertical = root["vertical_grid"];
    if (vertical && vertical.is_defined()) {
        std::string type = string_or(vertical, "type");
        if (type == "fv3")
            config.vertical_config.type = VerticalCoordType::FV3;
        else if (type == "mpas")
            config.vertical_config.type = VerticalCoordType::MPAS;
        else if (type == "wrf")
            config.vertical_config.type = VerticalCoordType::WRF;
        else if (type == "none" || type.empty())
            config.vertical_config.type = VerticalCoordType::NONE;
        else
            throw std::invalid_argument("Unknown vertical_grid type: '" + type + "'. Supported types: 'fv3', 'mpas', 'wrf', 'none'");
        config.vertical_config.ak_field = string_or(vertical, "ak_field", config.vertical_config.ak_field);
        config.vertical_config.bk_field = string_or(vertical, "bk_field", config.vertical_config.bk_field);
        config.vertical_config.p_surf_field = string_or(vertical, "p_surf_field", config.vertical_config.p_surf_field);
        config.vertical_config.z_field = string_or(vertical, "z_field", config.vertical_config.z_field);
        config.vertical_config.pbl_field = string_or(vertical, "pbl_field", config.vertical_config.pbl_field);
    }

    conf::Value data = root["cece_data"];
    if (data) {
        config.cece_data.debug_level = data["debug_level"].int_or(0);
        conf::Value streams = data["streams"];
        for (std::size_t i = 0; i < streams.size(); ++i) {
            conf::Value node = streams[i];
            CeceDataStreamConfig stream;
            stream.name = string_or(node, "name");
            conf::Value files = node["file"];
            if (files)
                stream.file_paths = files.kind() == conf::Node_Kind::Sequence ? files.as_string_list() : std::vector<std::string>{files.as_string()};
            conf::Value variables = node["variables"];
            for (std::size_t j = 0; j < variables.size(); ++j) {
                conf::Value value = variables[j];
                CeceDataVariableConfig variable;
                if (value.kind() == conf::Node_Kind::Scalar)
                    variable.name_in_file = variable.name_in_model = value.as_string();
                else {
                    variable.name_in_file = string_or(value, "file");
                    variable.name_in_model = string_or(value, "model");
                }
                stream.variables.push_back(std::move(variable));
            }
            if (variables.size() == 0 && !stream.name.empty()) stream.variables.push_back({stream.name, stream.name});
            stream.taxmode = string_or(node, "taxmode", stream.taxmode);
            stream.tintalgo = string_or(node, "tintalgo", string_or(node, "interpolation", stream.tintalgo));
            stream.mapalgo = string_or(node, "mapalgo", stream.mapalgo);
            stream.dtlimit = node["dtlimit"].int_or(stream.dtlimit);
            stream.yearFirst = node["yearFirst"].int_or(stream.yearFirst);
            stream.yearLast = node["yearLast"].int_or(stream.yearLast);
            stream.yearAlign = node["yearAlign"].int_or(stream.yearAlign);
            stream.offset = node["offset"].int_or(stream.offset);
            stream.meshfile = string_or(node, "meshfile");
            stream.lev_dimname = string_or(node, "lev_dimname", stream.lev_dimname);
            stream.time_var = string_or(node, "time_var", stream.time_var);
            stream.lon_var = string_or(node, "lon_var", stream.lon_var);
            stream.lat_var = string_or(node, "lat_var", stream.lat_var);
            stream.refresh_interval_seconds = node["refresh_interval_seconds"].int_or(0);
            config.cece_data.streams.push_back(std::move(stream));
        }
    }

    conf::Value output = root["output"];
    if (output) {
        config.output_config.enabled = output["enabled"] ? output["enabled"].as_bool() : true;
        config.output_config.directory = string_or(output, "directory", config.output_config.directory);
        config.output_config.filename_pattern = string_or(output, "filename_pattern", config.output_config.filename_pattern);
        config.output_config.frequency_steps = output["frequency_steps"].int_or(config.output_config.frequency_steps);
        conf::Value fields = output["fields"];
        for (std::size_t i = 0; i < fields.size(); ++i) {
            conf::Value value = fields[i];
            CeceOutputField field;
            field.name = value.kind() == conf::Node_Kind::Scalar ? value.as_string() : value["name"].as_string();
            conf::Value attributes = value["attributes"];
            if (attributes.kind() == conf::Node_Kind::Map)
                for (const auto& key : attributes.keys()) field.attributes[key] = attributes[key].as_string();
            config.output_config.fields.push_back(std::move(field));
        }
        if (output["amio_worker_threads"]) {
            int threads = output["amio_worker_threads"].as_int();
            if (threads < 1) {
                throw std::invalid_argument("output.amio_worker_threads must be >= 1; got " + std::to_string(threads) + ".");
            }
            config.output_config.amio_worker_threads = threads;
        }
    }

    conf::Value driver = root["driver"];
    if (driver) {
        config.driver_config.start_time = string_or(driver, "start_time", config.driver_config.start_time);
        config.driver_config.end_time = string_or(driver, "end_time", config.driver_config.end_time);
        config.driver_config.timestep_seconds = driver["timestep_seconds"].int_or(config.driver_config.timestep_seconds);
        config.driver_config.gridspec_file = string_or(driver, "gridspec_file");
        conf::Value grid = driver["grid"];
        config.driver_config.grid.nx = grid["nx"].int_or(config.driver_config.grid.nx);
        config.driver_config.grid.ny = grid["ny"].int_or(config.driver_config.grid.ny);
        config.driver_config.grid.nz = grid["nz"].int_or(config.driver_config.grid.nz);
        config.driver_config.grid.lon_min = grid["lon_min"].double_or(config.driver_config.grid.lon_min);
        config.driver_config.grid.lon_max = grid["lon_max"].double_or(config.driver_config.grid.lon_max);
        config.driver_config.grid.lat_min = grid["lat_min"].double_or(config.driver_config.grid.lat_min);
        config.driver_config.grid.lat_max = grid["lat_max"].double_or(config.driver_config.grid.lat_max);
        config.driver_config.stacking_refresh_interval_seconds = driver["stacking_refresh_interval_seconds"].int_or(0);
        if (driver["amio_worker_threads"]) {
            int threads = driver["amio_worker_threads"].as_int();
            if (threads < 1) {
                throw std::invalid_argument("driver.amio_worker_threads must be >= 1; got " + std::to_string(threads) + ".");
            }
            config.driver_config.amio_worker_threads = threads;
        }
        if (driver["amio_staging_buffer_count"]) {
            int count = driver["amio_staging_buffer_count"].as_int();
            if (count < 1) {
                throw std::invalid_argument("driver.amio_staging_buffer_count must be >= 1; got " + std::to_string(count) + ".");
            }
            config.driver_config.amio_staging_buffer_count = count;
        }
    }
    config.output_config.fields.SetTimeUnits(config.driver_config.start_time);
    return config;
}

void AddSpecies(CeceConfig& config, const std::string& species_name, std::vector<EmissionLayer> layers) {
    config.species_layers[species_name] = std::move(layers);
}
void AddScaleFactor(CeceConfig& config, const std::string& internal_name, const std::string& external_name) {
    config.scale_factor_mapping[internal_name] = external_name;
}
void AddMask(CeceConfig& config, const std::string& internal_name, const std::string& external_name) {
    config.mask_mapping[internal_name] = external_name;
}

}  // namespace cece
