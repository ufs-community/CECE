#include "aces/aces_data_ingestor.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "aces/aces_utils.hpp"

// Forward declarations for ACES-CDEPS bridge (defined in aces_cdeps_bridge.F90)
extern "C" {
void aces_cdeps_init(void* gcomp, void* clock, void* mesh, const char* stream_file, int* rc)
    __attribute__((weak));
void aces_cdeps_advance(void* clock, int* rc) __attribute__((weak));
void aces_cdeps_get_ptr(int stream_idx, const char* fldname, void** data_ptr, int* rc)
    __attribute__((weak));
void aces_cdeps_finalize() __attribute__((weak));
void aces_get_mesh_from_field(void* field, void** mesh, int* rc) __attribute__((weak));
}

namespace aces {

static DualView3D CreateDualViewFromESMF(ESMC_State state, const char* name, int nx, int ny,
                                         int nz) {
    ESMC_Field field;
    int rc = ESMC_StateGetField(state, name, &field);
    if (rc != ESMF_SUCCESS) {
        return {};
    }
    UnmanagedHostView3D host_view = WrapESMCField(field, nx, ny, nz);
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
        std::string("device_") + name, nx, ny, nz);
    return DualView3D(device_view, host_view);
}

void AcesDataIngestor::IngestMeteorology(ESMC_State importState,
                                         const std::vector<std::string>& field_names,
                                         AcesImportState& aces_state, int nx, int ny, int nz) {
    for (const auto& name : field_names) {
        // Try to get field info to handle 2D or special 3D (like ak/bk)
        ESMC_Field field;
        int local_nx = nx, local_ny = ny, local_nz = nz;
        if (ESMC_StateGetField(importState, name.c_str(), &field) == ESMF_SUCCESS) {
            std::array<int, 3> lbound = {1, 1, 1}, ubound = {1, 1, 1};
            int localDe = 0;
            // Robustly discover rank and dimensions by trying different dimCounts
            if (ESMC_FieldGetBounds(field, &localDe, lbound.data(), ubound.data(), 3) ==
                ESMF_SUCCESS) {
                local_nx = ubound[0] - lbound[0] + 1;
                local_ny = ubound[1] - lbound[1] + 1;
                local_nz = ubound[2] - lbound[2] + 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound.data(), ubound.data(), 2) ==
                       ESMF_SUCCESS) {
                local_nx = ubound[0] - lbound[0] + 1;
                local_ny = ubound[1] - lbound[1] + 1;
                local_nz = 1;
            } else if (ESMC_FieldGetBounds(field, &localDe, lbound.data(), ubound.data(), 1) ==
                       ESMF_SUCCESS) {
                local_nx = ubound[0] - lbound[0] + 1;
                local_ny = 1;
                local_nz = 1;
            }
        }

        aces_state.fields.try_emplace(
            name, CreateDualViewFromESMF(importState, name.c_str(), local_nx, local_ny, local_nz));

        // Sync host to device to ensure Kokkos kernels see updated ESMF data
        auto& dv = aces_state.fields[name];
        if (dv.view_host().data() != nullptr) {
            dv.modify<Kokkos::HostSpace>();
            dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
        }
    }
}

void AcesDataIngestor::InitializeCDEPS(ESMC_GridComp gcomp, ESMC_Clock clock,
                                       ESMC_State exportState, ESMC_Mesh mesh,
                                       const AcesCdepsConfig& config) {
    if (config.streams.empty()) {
        return;
    }

    // 1. Automatic mesh discovery if mesh is null (Requirement 1.5)
    ESMC_Mesh resolved_mesh = mesh;
    if (mesh.ptr == nullptr && aces_get_mesh_from_field != nullptr && exportState.ptr != nullptr) {
        std::cerr << "ACES: Mesh is null, attempting automatic mesh discovery from ESMF field\n";

        // Try to extract mesh from a known field in exportState
        // We'll try common field names that are likely to exist
        std::vector<std::string> candidate_fields = {
            "aces_discovery_emissions",  // Discovery field
        };

        // Also try the first species from config if available
        if (!config.streams.empty() && !config.streams[0].variables.empty()) {
            candidate_fields.push_back(config.streams[0].variables[0].name_in_model);
        }

        for (const auto& field_name : candidate_fields) {
            ESMC_Field field;
            int rc = ESMC_StateGetField(exportState, field_name.c_str(), &field);

            if (rc == ESMF_SUCCESS && field.ptr != nullptr) {
                // Extract mesh from field
                void* mesh_ptr = nullptr;
                int mesh_rc = 0;
                aces_get_mesh_from_field(field.ptr, &mesh_ptr, &mesh_rc);

                if (mesh_rc == 0 && mesh_ptr != nullptr) {
                    resolved_mesh.ptr = mesh_ptr;
                    std::cerr << "ACES: Successfully extracted mesh from field '" << field_name
                              << "'\n";
                    break;
                } else {
                    std::cerr << "ACES: Failed to extract mesh from field '" << field_name
                              << "'. RC=" << mesh_rc << "\n";
                }
            }
        }

        // Final check: if mesh is still null, log warning
        if (resolved_mesh.ptr == nullptr) {
            std::cerr << "ACES: WARNING - Mesh is null and automatic discovery failed. "
                      << "CDEPS initialization may fail.\n";
            std::cerr << "ACES: CORRECTIVE ACTION - Provide a valid mesh or ensure an ESMF field "
                      << "exists for mesh extraction.\n";
        }
    }

    // 2. Programmatically write CDEPS .streams file (ESMF Config format)
    std::ofstream stream_file("aces_emissions.streams");
    stream_file << "file_id: \"stream\"" << "\n";
    stream_file << "file_version: 2.0" << "\n";
    stream_file << "stream_info:" << "\n";

    for (size_t i = 0; i < config.streams.size(); ++i) {
        const auto& s = config.streams[i];
        std::string id = (i + 1 < 10) ? ("0" + std::to_string(i + 1)) : std::to_string(i + 1);
        stream_file << "taxmode" << id << ": " << s.taxmode << "\n";
        stream_file << "tInterpAlgo" << id << ": " << s.tintalgo << "\n";
        stream_file << "mapAlgo" << id << ": " << s.mapalgo << "\n";
        stream_file << "dtlimit" << id << ": " << s.dtlimit << "\n";
        stream_file << "yearFirst" << id << ": " << s.yearFirst << "\n";
        stream_file << "yearLast" << id << ": " << s.yearLast << "\n";
        stream_file << "yearAlign" << id << ": " << s.yearAlign << "\n";
        stream_file << "offset" << id << ": " << s.offset << "\n";
        if (!s.meshfile.empty()) {
            stream_file << "meshfile" << id << ": " << s.meshfile << "\n";
        }
        stream_file << "lev_dimname" << id << ": " << s.lev_dimname << "\n";

        stream_file << "stream_data_files" << id << ": " << "\n";
        for (const auto& f : s.file_paths) {
            stream_file << "  " << f << "\n";
        }

        stream_file << "stream_data_variables" << id << ": " << "\n";
        for (const auto& v : s.variables) {
            stream_file << "  " << v.name_in_file << " " << v.name_in_model << "\n";
        }
    }
    stream_file.close();

    // 3. Initialize CDEPS-inline
    if (aces_cdeps_init != nullptr) {
        int rc = 0;
        std::string stream_file_path = "aces_emissions.streams";
        aces_cdeps_init(gcomp.ptr, clock.ptr, resolved_mesh.ptr, stream_file_path.c_str(), &rc);
        if (rc != 0) {
            std::cerr << "ACES: Error initializing CDEPS. RC=" << rc << "\n";
            std::cerr << "ACES: CORRECTIVE ACTION - Check streams file format and NetCDF data files\n";
            std::cerr << "ACES: CORRECTIVE ACTION - Verify all variables in streams file exist in NetCDF files\n";
            return;
        }
        cdeps_initialized_ = true;
        std::cerr << "ACES: CDEPS initialization successful\n";
    } else {
        std::cerr << "ACES: WARNING - CDEPS bridge not available (aces_cdeps_init is null)\n";
    }
}

void AcesDataIngestor::AdvanceCDEPS(ESMC_Clock clock) {
    if (aces_cdeps_advance != nullptr) {
        int rc = 0;
        aces_cdeps_advance(clock.ptr, &rc);
    }
}

void AcesDataIngestor::FinalizeCDEPS() {
    if (aces_cdeps_finalize != nullptr) {
        aces_cdeps_finalize();
    }
}

void AcesDataIngestor::IngestEmissionsInline(const AcesCdepsConfig& config,
                                             AcesImportState& aces_state, int nx, int ny, int nz) {
    if (config.streams.empty()) {
        return;
    }

    // Trigger read and map pointers for all configured streams.
    // This allows the ingestion layer to be fully dynamic.
    for (size_t i = 0; i < config.streams.size(); ++i) {
        const auto& s = config.streams[i];
        int stream_idx = i + 1;

        for (const auto& v : s.variables) {
            if (aces_state.fields.find(v.name_in_model) == aces_state.fields.end()) {
                Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> host_view(
                    "host_" + v.name_in_model, nx, ny, nz);
                Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>
                    device_view("device_" + v.name_in_model, nx, ny, nz);
                aces_state.fields.try_emplace(v.name_in_model, device_view, host_view);
            }

            auto& dv = aces_state.fields[v.name_in_model];
            if (aces_cdeps_get_ptr != nullptr) {
                void* data_ptr = nullptr;
                int rc = 0;
                aces_cdeps_get_ptr(stream_idx, v.name_in_model.c_str(), &data_ptr, &rc);
                if (rc == 0 && data_ptr != nullptr) {
                    // Copy data from CDEPS internal pointer to our host view.
                    // Assuming dimensions match. CDEPS data is often 1D (flattened).
                    double* dptr = static_cast<double*>(data_ptr);
                    std::copy(dptr, dptr + nx * ny * nz, dv.view_host().data());

                    // Cache the CDEPS pointer for future ResolveField calls
                    cdeps_field_cache_[v.name_in_model] = data_ptr;
                } else if (rc != 0) {
                    std::cerr << "ACES: Error getting field pointer from CDEPS for "
                              << v.name_in_model << ". RC=" << rc << "\n";
                }
            }
            dv.modify<Kokkos::HostSpace>();
            dv.sync<Kokkos::DefaultExecutionSpace::memory_space>();
        }
    }
}

Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
             Kokkos::MemoryTraits<Kokkos::Unmanaged>>
AcesDataIngestor::ResolveField(const std::string& name, ESMC_State importState, int nx, int ny,
                               int nz) {
    // Priority 1: Check CDEPS field cache (Requirements 1.5, 1.6)
    // When a field exists in both CDEPS and ESMF, CDEPS version takes priority
    auto cdeps_it = cdeps_field_cache_.find(name);
    if (cdeps_it != cdeps_field_cache_.end() && cdeps_it->second != nullptr) {
        // Wrap CDEPS data pointer in Kokkos::View with Unmanaged trait (Requirement 1.8)
        // This prevents Kokkos from deallocating CDEPS-managed memory
        double* data_ptr = static_cast<double*>(cdeps_it->second);
        return Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                            Kokkos::MemoryTraits<Kokkos::Unmanaged>>(data_ptr, nx, ny, nz);
    }

    // Priority 2: Check ESMF field cache
    auto esmf_cache_it = esmf_field_cache_.find(name);
    if (esmf_cache_it != esmf_field_cache_.end()) {
        // Return cached ESMF field as unmanaged view
        auto device_view = esmf_cache_it->second.view_device();
        return Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                            Kokkos::MemoryTraits<Kokkos::Unmanaged>>(
            device_view.data(), device_view.extent(0), device_view.extent(1),
            device_view.extent(2));
    }

    // Priority 3: Query ESMF ImportState and cache result
    ESMC_Field field;
    int rc = ESMC_StateGetField(importState, name.c_str(), &field);
    if (rc == ESMF_SUCCESS) {
        // Create DualView from ESMF field
        UnmanagedHostView3D host_view = WrapESMCField(field, nx, ny, nz);
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> device_view(
            std::string("device_") + name, nx, ny, nz);

        DualView3D dual_view(device_view, host_view);
        dual_view.modify<Kokkos::HostSpace>();
        dual_view.sync<Kokkos::DefaultExecutionSpace::memory_space>();

        // Cache the field for subsequent queries (avoids redundant ESMF queries)
        esmf_field_cache_[name] = dual_view;

        // Return as unmanaged view
        return Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                            Kokkos::MemoryTraits<Kokkos::Unmanaged>>(
            device_view.data(), device_view.extent(0), device_view.extent(1),
            device_view.extent(2));
    }

    // Field not found in either CDEPS or ESMF
    std::cerr << "ACES: WARNING - Field '" << name << "' not found in CDEPS or ESMF ImportState\n";
    return Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                        Kokkos::MemoryTraits<Kokkos::Unmanaged>>();
}

bool AcesDataIngestor::HasCDEPSField(const std::string& name) const {
    auto it = cdeps_field_cache_.find(name);
    return (it != cdeps_field_cache_.end() && it->second != nullptr);
}

bool AcesDataIngestor::HasESMFField(const std::string& name, ESMC_State state) const {
    // Check cache first
    if (esmf_field_cache_.find(name) != esmf_field_cache_.end()) {
        return true;
    }

    // Query ESMF State
    ESMC_Field field;
    int rc = ESMC_StateGetField(state, name.c_str(), &field);
    return (rc == ESMF_SUCCESS);
}

}  // namespace aces
