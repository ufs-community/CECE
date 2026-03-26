#include "aces/aces_data_ingestor.hpp"
#include <Kokkos_Core.hpp>
#include "aces/aces_logger.hpp"
#include "aces/aces_state.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <cstring>

namespace aces {

AcesDataIngestor::AcesDataIngestor() {}
AcesDataIngestor::~AcesDataIngestor() {}

void AcesDataIngestor::SetField(const std::string& name, const double* data, int n_lev, int n_elem,
                                int nx, int ny, int nz, int* rc) {
    if (!data) {
        ACES_LOG_ERROR("[ACES] SetField received null data pointer for field: " + name);
        if (rc) *rc = -1;
        return;
    }

    if (n_lev * n_elem != nx * ny * nz && n_lev * n_elem != nx * ny) {
        ACES_LOG_WARNING("[ACES] SetField dimension mismatch for field: " + name +
                         " Received: " + std::to_string(n_lev) + "x" + std::to_string(n_elem) +
                         " (total: " + std::to_string(n_lev * n_elem) + ")" +
                         " Expected: " + std::to_string(nx) + "x" + std::to_string(ny) + "x" + std::to_string(nz) +
                         " (total: " + std::to_string(nx * ny * nz) + ")");
    }

    std::cout << "[ACES] SetField: Setting field " << name << " " << n_elem << "x" << n_lev << " (total=" << (n_lev*n_elem) << ")" << std::endl;

    // Create a host mirror view with correct dimensions (nx, ny, n_lev)
    // Assuming n_elem corresponds to the horizontal grid (nx * ny), but we know the data layout
    // is compatible with LayoutLeft (Fortran) regardless of dimensionality interpretation.

    // We omit HostSpace explicit template arg to rely on default execution space (CPU)
    // This avoids potential template deduction issues if HostSpace isn't strictly compatible
    // with certain View specializations in this context.
    using HostMirrorView = Kokkos::View<double***, Kokkos::LayoutLeft>;
    HostMirrorView host_view("host_" + name, nx, ny, n_lev);

    // Copy data from raw pointer to host mirror
    // We use a flat copy for efficiency
    std::memcpy(host_view.data(), data, n_lev * n_elem * sizeof(double));

    // Allocate device view if it doesn't exist or has wrong shape
    // Assuming DefaultExecutionSpace is compatible with Host copy for now (since CUDA=OFF)
    if (field_cache_.find(name) == field_cache_.end() ||
        field_cache_[name].extent(0) != (size_t)nx ||
        field_cache_[name].extent(1) != (size_t)ny ||
        field_cache_[name].extent(2) != (size_t)n_lev) {

        using DeviceView = Kokkos::View<double***, Kokkos::LayoutLeft>;
        field_cache_[name] = DeviceView(name, nx, ny, n_lev);
    }

    // Deep copy to device
    Kokkos::deep_copy(field_cache_[name], host_view);

    if (rc) *rc = 0;
}

void AcesDataIngestor::IngestEmissionsInline(const AcesDataConfig& config, AcesImportState& aces_state,
                                            int nx, int ny, int nz) {
    for (const auto& stream : config.streams) {
        for (const auto& var : stream.variables) {
            const std::string& model_name = var.name_in_model;

            if (HasCachedField(model_name)) {
                auto cached_view = field_cache_[model_name];

                // Ensure the field exists in the import state
                auto it = aces_state.fields.find(model_name);
                if (it == aces_state.fields.end()) {
                    // Create the DualView3D if not already present
                    // We deduce dimensions from the cached view
                    int c_nx = cached_view.extent(0);
                    int c_ny = cached_view.extent(1);
                    int c_nz = cached_view.extent(2);

                    // Create uninitialized DualView
                    DualView3D new_field(model_name, c_nx, c_ny, c_nz);
                    aces_state.fields.emplace(model_name, new_field);
                    it = aces_state.fields.find(model_name);

                    ACES_LOG_INFO("[ACES] IngestEmissionsInline: Created import field " + model_name +
                                  " (" + std::to_string(c_nx) + "x" + std::to_string(c_ny) + "x" + std::to_string(c_nz) + ")");
                }

                auto& dual_view = it->second;
                auto device_view = dual_view.view_device();

                // Validate dimensions match
                if (device_view.extent(0) != cached_view.extent(0) ||
                    device_view.extent(1) != cached_view.extent(1) ||
                    device_view.extent(2) != cached_view.extent(2)) {
                    ACES_LOG_ERROR("[ACES] IngestEmissionsInline: Dimension mismatch for field " + model_name +
                                   ". Expected " + std::to_string(device_view.extent(0)) + "x" +
                                   std::to_string(device_view.extent(1)) + "x" + std::to_string(device_view.extent(2)) +
                                   " but cached view is " + std::to_string(cached_view.extent(0)) + "x" +
                                   std::to_string(cached_view.extent(1)) + "x" + std::to_string(cached_view.extent(2)));
                    continue;
                }

                // Copy data from cache to the state field
                Kokkos::deep_copy(device_view, cached_view);

                // Mark device as modified so subsequent syncs work correctly
                dual_view.modify_device();

                // Debug log
                // ACES_LOG_INFO("[ACES] IngestEmissionsInline: Ingested " + model_name);
            } else {
                 // ACES_LOG_WARNING("[ACES] IngestEmissionsInline: Field not found in cache: " + model_name);
            }
        }
    }
}

Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
             Kokkos::MemoryTraits<Kokkos::Unmanaged>>
AcesDataIngestor::ResolveField(const std::string& name, int nx, int ny, int nz) {
    if (!HasCachedField(name)) {
        ACES_LOG_ERROR("[ACES] ResolveField: field not found in cache: " + name);
        return Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                            Kokkos::MemoryTraits<Kokkos::Unmanaged>>();
    }

    return field_cache_[name];
}

bool AcesDataIngestor::HasCachedField(const std::string& name) const {
    return field_cache_.find(name) != field_cache_.end();
}

std::string AcesDataIngestor::SerializeTideESMFConfig(const AcesDataConfig& config) {
    YAML::Node root;
    YAML::Node streams_list_node = YAML::Node(YAML::NodeType::Sequence);

    for (const auto& stream : config.streams) {
        YAML::Node stream_node;

        // TIDE configuration mapping
        stream_node["name"] = stream.name;

        // Input files
        stream_node["input_files"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& path : stream.file_paths) {
            stream_node["input_files"].push_back(path);
        }

        // Field maps
        stream_node["field_maps"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& var : stream.variables) {
            YAML::Node field_map;
            field_map["file_var"] = var.name_in_file;
            field_map["model_var"] = var.name_in_model;
            stream_node["field_maps"].push_back(field_map);
        }

        // Configuration parameters
        stream_node["tax_mode"] = stream.taxmode;
        stream_node["time_interp"] = stream.tintalgo;
        stream_node["map_algo"] = stream.mapalgo;
        stream_node["read_mode"] = "single"; // Default as per tide_yaml_c.cpp fallback
        stream_node["dt_limit"] = stream.dtlimit;
        stream_node["year_first"] = stream.yearFirst;
        stream_node["year_last"] = stream.yearLast;
        stream_node["year_align"] = stream.yearAlign;
        stream_node["offset"] = stream.offset;

        if (!stream.lev_dimname.empty()) {
            stream_node["lev_dimname"] = stream.lev_dimname;
        } else {
             stream_node["lev_dimname"] = "";
        }

        if (!stream.meshfile.empty()) {
            stream_node["mesh_file"] = stream.meshfile;
        } else {
             stream_node["mesh_file"] = "";
        }

        streams_list_node.push_back(stream_node);
    }

    root["streams"] = streams_list_node;

    YAML::Emitter out;
    out << root;
    return std::string(out.c_str());
}

}  // namespace aces
