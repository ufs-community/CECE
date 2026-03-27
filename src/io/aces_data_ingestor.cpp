#include "aces/aces_data_ingestor.hpp"
#include <Kokkos_Core.hpp>
#include "aces/aces_logger.hpp"
#include "aces/aces_state.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <sstream>
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

    // Determine if this is 2D emission data from TIDE
    // TIDE provides 2D data where n_lev * n_elem == nx * ny (horizontal grid)
    bool is_2d_emission = (n_lev * n_elem == nx * ny);

    int actual_nz;
    if (is_2d_emission) {
        // For 2D emission data, use single vertical level initially
        actual_nz = 1;
        std::cout << "[ACES] SetField: Setting 2D emission field " << name << " "
                  << nx << "x" << ny << "x" << actual_nz
                  << " (from " << n_lev << "x" << n_elem << " TIDE data)" << std::endl;
    } else {
        // For 3D data, use provided dimensions
        actual_nz = (n_lev * n_elem == nx * ny * nz) ? nz : n_lev;
        std::cout << "[ACES] SetField: Setting 3D field " << name << " "
                  << nx << "x" << ny << "x" << actual_nz
                  << " (total=" << (n_lev*n_elem) << ")" << std::endl;
    }

    // Create a host mirror view with correct dimensions
    using HostMirrorView = Kokkos::View<double***, Kokkos::LayoutLeft>;
    HostMirrorView host_view("host_" + name, nx, ny, actual_nz);

    if (is_2d_emission) {
        // For 2D emission data, reshape from 1D array to 2D grid with 1 vertical level
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                int linear_idx = i * ny + j;  // Assuming column-major (Fortran) ordering
                if (linear_idx < n_lev * n_elem) {
                    host_view(i, j, 0) = data[linear_idx];
                } else {
                    host_view(i, j, 0) = 0.0;  // Fill with zeros if data is insufficient
                }
            }
        }
    } else {
        // For 3D data, use direct memory copy
        std::memcpy(host_view.data(), data, n_lev * n_elem * sizeof(double));
    }

    // Allocate device view if it doesn't exist or has wrong shape
    if (field_cache_.find(name) == field_cache_.end() ||
        field_cache_[name].extent(0) != (size_t)nx ||
        field_cache_[name].extent(1) != (size_t)ny ||
        field_cache_[name].extent(2) != (size_t)actual_nz) {

        using DeviceView = Kokkos::View<double***, Kokkos::LayoutLeft>;
        field_cache_[name] = DeviceView(name, nx, ny, actual_nz);
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

                // Ensure proper synchronization
                Kokkos::fence();

                // Debug log - verify field is properly set
                std::cout << "[ACES] IngestEmissionsInline: Successfully ingested " << model_name
                          << " with device view extents: " << device_view.extent(0) << "x"
                          << device_view.extent(1) << "x" << device_view.extent(2) << std::endl;
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
    std::ostringstream oss;
    
    // ESMF RC file header
    oss << "file_id: \"streams\"\n";
    oss << "file_version: 1.0\n";
    oss << "stream_info: " << config.streams.size() << "\n";
    oss << "\n";

    int stream_idx = 1;
    for (const auto& stream : config.streams) {
        std::string idx = (stream_idx < 10 ? "0" : "") + std::to_string(stream_idx);
        
        // Required ESMF RC parameters with stream index
        oss << "taxmode" << idx << ": " << stream.taxmode << "\n";
        oss << "tInterpAlgo" << idx << ": " << stream.tintalgo << "\n";
        oss << "readMode" << idx << ": single\n";
        oss << "mapalgo" << idx << ": " << stream.mapalgo << "\n";
        oss << "dtlimit" << idx << ": " << std::to_string(stream.dtlimit) << "\n";
        oss << "yearFirst" << idx << ": " << std::to_string(stream.yearFirst) << "\n";
        oss << "yearLast" << idx << ": " << std::to_string(stream.yearLast) << "\n";
        oss << "yearAlign" << idx << ": " << std::to_string(stream.yearAlign) << "\n";
        oss << "stream_offset" << idx << ": " << std::to_string(stream.offset) << "\n";
        oss << "stream_lev_dimname" << idx << ": " << stream.lev_dimname << "\n";
        
        // Coordinate variable configuration
        oss << "time_var" << idx << ": " << stream.time_var << "\n";
        oss << "lon_var" << idx << ": " << stream.lon_var << "\n"; 
        oss << "lat_var" << idx << ": " << stream.lat_var << "\n";
        
        // Required stream_mesh_file parameter (dummy value for grid-based data)
        oss << "stream_mesh_file" << idx << ": " << (stream.meshfile.empty() ? "none" : stream.meshfile) << "\n";
        
        // Data files (ESMF format: comma-separated values)
        oss << "stream_data_files" << idx << ": ";
        for (size_t i = 0; i < stream.file_paths.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << stream.file_paths[i];
        }
        oss << "\n";
        
        // Data variables (ESMF format: comma-separated pairs)
        oss << "stream_data_variables" << idx << ": ";
        for (size_t i = 0; i < stream.variables.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << stream.variables[i].name_in_file << " " << stream.variables[i].name_in_model;
        }
        oss << "\n";

        // Debug: Log coordinate variable configuration
        std::cout << "DEBUG: Stream '" << stream.name << "' time_var='" << stream.time_var
                  << "' lon_var='" << stream.lon_var << "' lat_var='" << stream.lat_var << "'" << std::endl;
                  
        oss << "\n";
        stream_idx++;
    }

    return oss.str();
}

}  // namespace aces
