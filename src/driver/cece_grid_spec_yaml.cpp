// SPDX-License-Identifier: Apache-2.0
// Copyright (c) HELM Project Contributors

/**
 * @file cece_grid_spec_yaml.cpp
 * @brief GridSpec::from_yaml / validate — the single YAML grid-resolution
 * path shared by both CECE drivers.
 *
 * This is the grid-dimension and coordinate-resolution logic relocated from
 * the standalone driver's main() so the NUOPC cap's config-built branch runs
 * literally the same code (parity by construction). Named-grid resolution via
 * AXIS, explicit/stream-inferred GRIDSPEC coordinate loading through AMIO
 * (with CF unpacking and radian handling), and the uniform-extents fallback
 * all live here; failures throw std::invalid_argument with the same
 * diagnostics the driver has always logged, and the facade converts them to
 * rc<0 (never a silent fallback).
 */

#include <amio/amio.h>

#include <algorithm>
#include <axis/topology/named_grid_registry.hpp>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cece/cece_amio_utils.hpp"
#include "cece/cece_grid_detail.hpp"
#include "cece/cece_logger.hpp"
#include "cece/cece_simulation.hpp"

namespace {

// Build the in-memory AMIO coordinate-manifest YAML for reading lon/lat out
// of `path`. Kept as a named helper so the manifest schema (backend, staging
// pool, worker pool, prefetch tuning) lives in exactly one place; the call
// site only opens/reads with the returned string. Writing it to a shared-disk
// file (e.g. Lustre) races when multiple MPI ranks per node truncate/rewrite
// the same path concurrently, which produces torn reads (empty/partial YAML)
// and spurious open failures — hence in-memory.
std::string BuildCoordinateManifest(const std::string& path) {
    std::ostringstream manifest;
    manifest << "backend: netcdf4\n"
             << "path: " << path << "\n"
             << "data_model: enhanced\n"
             << "staging_pool:\n"
             << "  buffer_count: 16\n"
             << "  buffer_capacity_bytes: 33554432\n"
             << "worker_pool:\n"
             << "  threads: 1\n"
             << "prefetch:\n"
             << "  depth: 4\n"
             << "  read_timeout_s: 60\n"
             << "staging_timeout_ms: 10000\n";
    return manifest.str();
}

/// Outcome of a coordinate-array read: `found` says whether any candidate
/// variable loaded; `values` carries the decoded coordinates; `extent0` /
/// `extent1` are the leading-dimension extents of the variable that loaded
/// (extent1 is 0 for rank-1 variables).
struct CoordRead {
    bool found = false;
    std::vector<double> values;
    int extent0 = 0;
    int extent1 = 0;
};

/// Read one flattened coordinate array (lon or lat) out of an opened AMIO
/// dataset, trying each candidate variable name in order. Applies CF packing,
/// radian conversion (when `is_radian`), and longitude wrapping (when
/// `wrap_lon`). A decode failure logs and yields not-found (empty extents) so
/// an unreadable variable can never pass as a loaded gridspec.
CoordRead read_coord(amio_dataset_handle dataset, const std::vector<std::string>& names, bool is_radian, bool wrap_lon) {
    CoordRead result;
    for (const auto& name : names) {
        amio_view_handle view = nullptr;
        if (amio_read(dataset, name.c_str(), 0, nullptr, &view) != AMIO_OK) {
            continue;
        }
        const void* view_data = nullptr;
        size_t view_size = 0;
        if (amio_view_data(view, &view_data, &view_size) != AMIO_OK) {
            amio_release_view(view);
            return result;
        }
        amio_shape_t shape{};
        if (amio_view_shape(view, &shape) != AMIO_OK) {
            amio_release_view(view);
            return result;
        }
        if (shape.rank == 1) {
            result.extent0 = static_cast<int>(shape.extents[0]);
        } else if (shape.rank == 2) {
            result.extent0 = static_cast<int>(shape.extents[0]);
            result.extent1 = static_cast<int>(shape.extents[1]);
        } else {
            amio_release_view(view);
            return result;
        }
        int total_len = 1;
        for (int r = 0; r < shape.rank; ++r) {
            total_len *= static_cast<int>(shape.extents[r]);
        }
        amio_dtype_t dtype = AMIO_DTYPE_F64;
        double scale = 1.0;
        double offset = 0.0;
        cece::detail::read_cf_packing(dataset, name, scale, offset);
        std::vector<double> widened;
        const bool ok = amio_view_dtype(view, &dtype) == AMIO_OK &&
                        cece::detail::widen_amio_elements(view_data, dtype, static_cast<std::size_t>(total_len), scale, offset, widened);
        if (!ok) {
            // Resetting the result keeps a failed decode indistinguishable
            // from "no variable found", so an empty coordinate array can never
            // pass as a loaded gridspec.
            CECE_LOG_ERROR("Could not decode gridspec coordinate variable '" + name + "'");
            result = CoordRead{};
            amio_release_view(view);
            return result;
        }
        result.values.resize(total_len);
        for (int i = 0; i < total_len; ++i) {
            double val = widened[i];
            if (is_radian) {
                val = cece::detail::radians_to_degrees(val);
            }
            result.values[i] = wrap_lon ? cece::detail::wrap_longitude(val) : val;
        }
        amio_release_view(view);
        result.found = true;
        return result;
    }
    return result;
}

}  // namespace

namespace cece {

// Candidate longitude / latitude variable names, in priority order. Shared
// with the historical driver behavior exactly.
static const std::vector<std::string> kLonNames = {"grid_lont", "grid_lon",        "XLONG",         "lonCell",   "geolon",  "clon",
                                                   "glamt",     "mesh2d_face_lon", "lon",           "longitude", "LON",     "lon_rho",
                                                   "nav_lon",   "mesh_node_x",     "mesh2d_node_x", "node_x",    "grid_xt", "x"};
static const std::vector<std::string> kLatNames = {"grid_latt", "grid_lat",        "XLAT",          "latCell",  "geolat",  "clat",
                                                   "gphit",     "mesh2d_face_lat", "lat",           "latitude", "LAT",     "lat_rho",
                                                   "nav_lat",   "mesh_node_y",     "mesh2d_node_y", "node_y",   "grid_yt", "y"};

namespace {

/// Return the first candidate variable name present in the dataset, or an
/// empty string when none is. For longitude candidates, `is_radian` is set
/// when the name that matched is one of the cell-centered cubed-sphere
/// variables, which store radians (the historical driver rule). Purely
/// observational: a probe failure just means "try the next name".
std::string probe_coord_name(amio_dataset_handle dataset, const std::vector<std::string>& names, bool& is_radian) {
    for (const auto& name : names) {
        amio_view_handle probe = nullptr;
        if (amio_read(dataset, name.c_str(), 0, nullptr, &probe) == AMIO_OK) {
            amio_release_view(probe);
            if (name == "lonCell" || name == "latCell" || name == "lonVertex" || name == "latVertex") {
                is_radian = true;
            }
            return name;
        }
    }
    return {};
}

/// The file to read target-grid coordinates from: the explicit
/// `driver.gridspec_file` when set (and not the `none` sentinel), otherwise
/// the first data stream's file. `explicit_gridspec` reports which one it
/// was: an explicit file that fails to load is fatal, while a stream-inferred
/// miss simply falls through to generated coordinates.
std::string gridspec_input_file(conf::Config& config, bool& explicit_gridspec) {
    explicit_gridspec = false;
    auto gridspec_opt = config.try_string("driver.gridspec_file");
    if (gridspec_opt.has_value() && !gridspec_opt->empty() && *gridspec_opt != "none" && *gridspec_opt != "NONE") {
        explicit_gridspec = true;
        return *gridspec_opt;
    }
    if (config.has("cece_data.streams")) {
        auto streams = config.at("cece_data.streams");
        if (streams.size() > 0) {
            auto file_val = streams[static_cast<std::size_t>(0)]["file"];
            if (file_val.is_defined()) {
                return file_val.as_string();
            }
        }
    }
    return {};
}

/// Outcome of reading a file's coordinate arrays: `loaded` is true only when
/// the file's shapes match the declared (nx, ny); `lon`/`lat` carry the
/// decoded coordinates and `topology` the shape-derived classification.
struct FileGridLoad {
    bool loaded = false;
    int nx = 0;
    int ny = 0;
    std::vector<double> lon;
    std::vector<double> lat;
    GridTopology topology = GridTopology::Rectilinear;
};

/// Read and classify the coordinate arrays of an opened gridspec dataset
/// against the declared (nx, ny). Purely observational: a mismatch (or a
/// dataset with no recognizable coordinates) reports not-loaded and leaves
/// the caller's uniform-extents fallback in charge.
FileGridLoad load_gridspec_coords(amio_dataset_handle dataset, int nx, int ny) {
    FileGridLoad out;

    // Longitude: is_radian only for the cell-centered cubed-sphere names,
    // matching the historical driver rule.
    bool is_radian = false;
    (void)probe_coord_name(dataset, kLonNames, is_radian);
    const CoordRead lon = read_coord(dataset, kLonNames, is_radian, /*wrap_lon=*/true);
    int file_nx = 0;
    if (lon.found) {
        // 2-D: extents[1] is the x extent.
        file_nx = lon.extent1 != 0 ? lon.extent1 : lon.extent0;
    }

    const CoordRead lat = read_coord(dataset, kLatNames, /*is_radian=*/false, /*wrap_lon=*/false);
    int file_ny = 0;
    if (lat.found) {
        // Rank-1 or rank-2 latitude: the y extent is extents[0].
        file_ny = lat.extent0;
    }

    // If nx and ny are not specified in the configuration, dynamically
    // inherit them from the gridspec file. (The dimension guard in
    // from_yaml requires them to be declared today; this stays for the
    // historical behavior contract.)
    if (nx == 0 && file_nx > 0) {
        nx = file_nx;
    }
    if (ny == 0 && file_ny > 0) {
        ny = (file_ny == file_nx) ? 1 : file_ny;
    }

    if (nx == file_nx && (ny == file_ny || (ny == 1 && file_ny == file_nx)) && file_nx > 0 && file_ny > 0) {
        out.loaded = true;
        out.nx = nx;
        out.ny = ny;
        out.lon = lon.values;
        out.lat = lat.values;
        if (ny == 1) {
            out.topology = GridTopology::Unstructured;
        } else if (out.lon.size() == static_cast<size_t>(nx) * static_cast<size_t>(ny) &&
                   out.lat.size() == static_cast<size_t>(nx) * static_cast<size_t>(ny)) {
            out.topology = GridTopology::Curvilinear;
        } else {
            out.topology = GridTopology::Rectilinear;
        }
    }
    return out;
}

}  // namespace

GridSpec GridSpec::from_yaml(const std::string& config_file, conf::Config& config) {
    GridSpec spec;

    // --- A. Grid dimensions ------------------------------------------------
    int nx = 0;
    int ny = 0;
    int nz = 1;
    std::string grid_name = "";
    if (config.has("driver.grid")) {
        nz = config.get_or("driver.grid.nz", 1);
        grid_name = config.get_or<std::string>("driver.grid.grid_name", "");
        if (grid_name.empty()) {
            nx = config.get_or("driver.grid.nx", 0);
            ny = config.get_or("driver.grid.ny", 0);
        } else {
            try {
                auto parsed = axis::topology::NamedGridRegistry::parse(grid_name);
                if (parsed.family == 'F' || parsed.family == 'R') {
                    int expected_nx = 4 * parsed.number;
                    int expected_ny = 2 * parsed.number;

                    int declared_nx = config.get_or("driver.grid.nx", 0);
                    int declared_ny = config.get_or("driver.grid.ny", 0);
                    if (declared_nx != 0 && declared_ny != 0) {
                        if (declared_nx != expected_nx || declared_ny != expected_ny) {
                            throw std::invalid_argument("Grid dimensions nx=" + std::to_string(declared_nx) + ", ny=" + std::to_string(declared_ny) +
                                                        " do not match the expected dimensions for Named Grid " + grid_name + " (" +
                                                        std::to_string(expected_nx) + "x" + std::to_string(expected_ny) + ")!");
                        }
                    }
                    nx = expected_nx;
                    ny = expected_ny;
                } else {
                    throw std::invalid_argument(
                        "Only regular Gaussian grids (family 'F', e.g. 'F360') and regular lat-lon grids (family 'R', e.g. "
                        "'R360') are currently supported as structured CECE target grids.");
                }
            } catch (const std::exception& e) {
                throw std::invalid_argument(std::string("Failed to parse named grid '") + grid_name + "': " + e.what());
            }
        }
    }

    if (nx <= 0 || ny <= 0 || nz <= 0) {
        throw std::invalid_argument(
            "driver.grid.nx, driver.grid.ny, and driver.grid.nz must be positive, or "
            "driver.grid.grid_name must specify a supported named grid.");
    }

    CECE_LOG_DEBUG("[GRID] Parsed nx = " + std::to_string(nx) + ", ny = " + std::to_string(ny) + ", grid_name = '" + grid_name + "'");

    spec.nx = nx;
    spec.ny = ny;
    spec.nz = nz;

    // --- B. Coordinate arrays ----------------------------------------------
    std::vector<double> file_lons(static_cast<size_t>(nx), 0.0);
    std::vector<double> file_lats(static_cast<size_t>(ny == 1 ? nx : ny), 0.0);

    if (!grid_name.empty()) {
        // Named grid: generate the mesh via AXIS and extract 1-D center
        // coordinates (sorted).
        try {
            auto mesh = axis::topology::NamedGridRegistry::generate<Kokkos::HostSpace>(grid_name);
            auto coords = mesh.node_coords();
            for (int i = 0; i < nx; ++i) {
                file_lons[static_cast<size_t>(i)] = cece::detail::wrap_longitude(coords(i, 0));
            }
            for (int j = 0; j < ny; ++j) {
                file_lats[static_cast<size_t>(j)] = coords(static_cast<long>(j) * nx, 1);
            }
            std::sort(file_lons.begin(), file_lons.end());
            std::sort(file_lats.begin(), file_lats.end());
        } catch (const std::exception& e) {
            throw std::invalid_argument(std::string("Failed to retrieve coordinates from named grid '") + grid_name + "': " + e.what());
        }
        spec.topology = GridTopology::Rectilinear;
    } else {
        bool is_explicit_gridspec = false;
        const std::string input_file_path = gridspec_input_file(config, is_explicit_gridspec);
        bool loaded_from_file = false;

        if (!input_file_path.empty()) {
            const std::string coord_manifest_content = BuildCoordinateManifest(input_file_path);

            amio_core_handle coord_core = nullptr;
            amio_dataset_handle coord_dataset = nullptr;

            amio_status_t amio_rc = amio_init_from_string(coord_manifest_content.c_str(), "yaml", &coord_core);
            if (amio_rc != AMIO_OK) {
                CECE_LOG_ERROR(std::string("amio_init_from_string failed for coordinate manifest: ") + amio_strerror(amio_rc));
            } else {
                amio_rc = amio_open_dataset_from_string(coord_core, coord_manifest_content.c_str(), "yaml", AMIO_MODE_READ, &coord_dataset);
                if (amio_rc != AMIO_OK) {
                    CECE_LOG_ERROR("amio_open_dataset_from_string failed for dataset '" + input_file_path + "': " + amio_strerror(amio_rc));
                } else {
                    const FileGridLoad file_grid = load_gridspec_coords(coord_dataset, nx, ny);
                    amio_close(coord_dataset);
                    if (file_grid.loaded) {
                        // Adopt any dimensions inherited from the file.
                        nx = file_grid.nx;
                        ny = file_grid.ny;
                        file_lons = file_grid.lon;
                        file_lats = file_grid.lat;
                        loaded_from_file = true;
                        spec.gridspec_file = input_file_path;
                        spec.topology = file_grid.topology;
                    }
                }
                amio_finalize(coord_core);
            }
        }

        if (is_explicit_gridspec && !loaded_from_file) {
            throw std::invalid_argument("[GRID] Failed to load gridspec coordinates from explicitly specified gridspec file '" + input_file_path +
                                        "'");
        }

        if (!loaded_from_file) {
            if (nx <= 0 || ny <= 0) {
                throw std::invalid_argument(
                    "Grid dimensions (nx, ny) were not specified in driver.grid configuration and could not be determined from "
                    "input files!");
            }

            double lon_min = config.get_or("driver.grid.lon_min", -180.0);
            double lon_max = config.get_or("driver.grid.lon_max", 180.0);
            double lat_min = config.get_or("driver.grid.lat_min", -90.0);
            double lat_max = config.get_or("driver.grid.lat_max", 90.0);

            double dlon = (lon_max - lon_min) / nx;
            double dlat = (lat_max - lat_min) / ny;

            file_lons.assign(static_cast<size_t>(nx), 0.0);
            file_lats.assign(static_cast<size_t>(ny == 1 ? nx : ny), 0.0);
            for (int i = 0; i < nx; ++i) {
                file_lons[static_cast<size_t>(i)] = lon_min + dlon * (i + 0.5);
            }
            for (int j = 0; j < ny; ++j) {
                file_lats[static_cast<size_t>(j)] = lat_min + dlat * (j + 0.5);
            }
            spec.topology = (ny == 1) ? GridTopology::Unstructured : GridTopology::Rectilinear;
        }
    }

    if (nx <= 0 || ny <= 0 || nz <= 0) {
        throw std::invalid_argument("Invalid grid dimensions nx=" + std::to_string(nx) + ", ny=" + std::to_string(ny) + ", nz=" + std::to_string(nz));
    }

    spec.nx = nx;
    spec.ny = ny;
    spec.nz = nz;
    spec.lon_coords = std::move(file_lons);
    spec.lat_coords = std::move(file_lats);

    spec.validate();
    return spec;
}

void GridSpec::validate() const {
    if (nx <= 0 || ny <= 0 || nz <= 0) {
        throw std::invalid_argument("GridSpec: nx, ny, and nz must be positive (got nx=" + std::to_string(nx) + ", ny=" + std::to_string(ny) +
                                    ", nz=" + std::to_string(nz) + ")");
    }
    const auto expect_size = [&](size_t want, const std::string& which, size_t got) {
        if (got != want) {
            throw std::invalid_argument("GridSpec: " + which + " array length " + std::to_string(got) +
                                        " does not match the declared topology/dimensions (expected " + std::to_string(want) + ")");
        }
    };
    if (topology == GridTopology::Rectilinear) {
        expect_size(static_cast<size_t>(nx), "lon", lon_coords.size());
        expect_size(static_cast<size_t>(ny == 1 ? nx : ny), "lat", lat_coords.size());
    } else if (topology == GridTopology::Curvilinear) {
        expect_size(static_cast<size_t>(nx) * static_cast<size_t>(ny), "lon", lon_coords.size());
        expect_size(static_cast<size_t>(nx) * static_cast<size_t>(ny), "lat", lat_coords.size());
    } else if (topology == GridTopology::Unstructured) {
        expect_size(static_cast<size_t>(nx), "lon", lon_coords.size());
        expect_size(static_cast<size_t>(nx), "lat", lat_coords.size());
    } else {
        throw std::invalid_argument("GridSpec: unrecognized topology value " + std::to_string(static_cast<int>(topology)));
    }
}

}  // namespace cece
