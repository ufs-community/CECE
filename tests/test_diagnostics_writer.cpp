// Tests for diagnostic publication through the standalone AMIO writer (feature
// 003, user story 1). Mirrors the real-NetCDF pattern of
// test_standalone_writer_attributes.cpp: drive the writer directly, then read
// the produced file back through the netCDF C API.

#include <gtest/gtest.h>
#include <mpi.h>
#include <netcdf.h>
#include <unistd.h>

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_standalone_writer.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kNx = 4, kNy = 3, kNz = 1;

// A deterministic, position-dependent pattern used to prove the diagnostic
// write loop uses the same LayoutLeft -> CF index transform as species.
double Pattern(int i, int j, int k) {
    return i * 100.0 + j * 10.0 + k;
}

class DiagnosticsWriterTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // ctest runs each discovered case as its own process, in parallel under
        // -j; the directory must be unique per process and per test so one
        // case's remove_all cannot delete another's in-flight output.
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        out_dir_ = fs::temp_directory_path() /
                   ("cece_diagnostics_writer_test_" + std::string{info->test_suite_name()} + '_' + info->name() + '_' + std::to_string(getpid()));
        fs::remove_all(out_dir_);
        fs::create_directories(out_dir_);
    }

    void TearDown() override {
        fs::remove_all(out_dir_);
    }

    // A grid-sized DualView filled with Pattern(i, j, k).
    static cece::DualView3D FilledField(const std::string& name, int nx, int ny, int nz) {
        cece::DualView3D view(name, nx, ny, nz);
        auto host = view.view_host();
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                for (int k = 0; k < nz; ++k) host(i, j, k) = Pattern(i, j, k);
        view.modify_host();
        return view;
    }

    cece::CeceOutputConfig BaseConfig(int nx = kNx, int ny = kNy, int nz = kNz) const {
        cece::CeceOutputConfig config;
        config.enabled = true;
        config.directory = out_dir_.string();
        config.filename_pattern = "diag_test_{HH}{mm}{ss}.nc";
        config.frequency_steps = 1;
        config.fields = {{"co", {{"units", "kg m-2 s-1"}, {"long_name", "carbon_monoxide_emission_flux"}}}};
        config.fields.SetTimeUnits("2010-01-01T00:00:00");
        return config;
    }

    // Writes one step with the given diagnostics payload; returns the rc and
    // the path the writer would produce.
    std::pair<int, fs::path> WriteOneStep(const cece::CeceOutputConfig& config, int nx, int ny, int nz,
                                          const std::map<std::string, cece::DiagnosticOut>& diagnostics) {
        cece::CeceStandaloneWriter writer(config);
        EXPECT_EQ(writer.Initialize("2010-01-01T00:00:00", nx, ny, nz), 0);

        std::unordered_map<std::string, cece::DualView3D> fields;
        fields.emplace("co", FilledField("co", nx, ny, nz));

        const int rc = writer.WriteTimeStep(fields, diagnostics, 3600.0, 1);
        writer.Finalize();
        return {rc, fs::path(config.directory) / "diag_test_010000.nc"};
    }

    // Wraps a host view + metadata into the writer's diagnostic input type.
    static cece::DiagnosticOut MakeDiagnostic(const cece::DualView3D& view, const std::string& units, const std::string& long_name) {
        cece::DiagnosticOut out;
        out.host = Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::HostSpace>(view.view_host());
        out.units = units;
        out.long_name = long_name;
        return out;
    }

    static bool HasVariable(const fs::path& nc_path, const std::string& name) {
        int nc_id = -1;
        if (nc_open(nc_path.c_str(), NC_NOWRITE, &nc_id) != NC_NOERR) return false;
        // A NULL varid pointer is accepted even for unknown names in this
        // netCDF build, so resolve into a real variable to make the check
        // meaningful.
        int var_id = -1;
        const bool present = nc_inq_varid(nc_id, name.c_str(), &var_id) == NC_NOERR && var_id >= 0;
        nc_close(nc_id);
        return present;
    }

    static std::optional<std::string> ReadTextAttribute(const fs::path& nc_path, const std::string& variable, const std::string& attribute) {
        int nc_id = -1;
        EXPECT_EQ(nc_open(nc_path.c_str(), NC_NOWRITE, &nc_id), NC_NOERR) << "cannot open " << nc_path;
        int var_id = -1;
        EXPECT_EQ(nc_inq_varid(nc_id, variable.c_str(), &var_id), NC_NOERR) << "variable " << variable << " missing";
        size_t length = 0;
        std::optional<std::string> value;
        if (nc_inq_attlen(nc_id, var_id, attribute.c_str(), &length) == NC_NOERR) {
            std::string text(length, '\0');
            EXPECT_EQ(nc_get_att_text(nc_id, var_id, attribute.c_str(), text.data()), NC_NOERR);
            value = text;
        }
        nc_close(nc_id);
        return value;
    }

    static std::vector<double> ReadAllValues(const fs::path& nc_path, const std::string& variable) {
        int nc_id = -1;
        EXPECT_EQ(nc_open(nc_path.c_str(), NC_NOWRITE, &nc_id), NC_NOERR) << "cannot open " << nc_path;
        int var_id = -1;
        EXPECT_EQ(nc_inq_varid(nc_id, variable.c_str(), &var_id), NC_NOERR) << "variable " << variable << " missing";
        int ndims = 0;
        nc_inq_varndims(nc_id, var_id, &ndims);
        std::vector<int> dims(ndims);
        nc_inq_vardimid(nc_id, var_id, dims.data());
        size_t n = 1;
        for (int d = 0; d < ndims; ++d) {
            size_t len = 0;
            nc_inq_dimlen(nc_id, dims[d], &len);
            n *= len;
        }
        std::vector<double> values(n);
        EXPECT_EQ(nc_get_var_double(nc_id, var_id, values.data()), NC_NOERR);
        nc_close(nc_id);
        return values;
    }

    // Sorted variable names present in the file.
    static std::vector<std::string> VariableNames(const fs::path& nc_path) {
        int nc_id = -1;
        EXPECT_EQ(nc_open(nc_path.c_str(), NC_NOWRITE, &nc_id), NC_NOERR) << "cannot open " << nc_path;
        int nvars = 0;
        nc_inq_nvars(nc_id, &nvars);
        std::vector<std::string> names;
        for (int v = 0; v < nvars; ++v) {
            char name[NC_MAX_NAME + 1];
            if (nc_inq_varname(nc_id, v, name) == NC_NOERR) names.emplace_back(name);
        }
        nc_close(nc_id);
        std::sort(names.begin(), names.end());
        return names;
    }

    fs::path out_dir_;
};

// (a) A non-empty diagnostics payload lands in the NetCDF as a variable with
// the manifest-supplied units/long_name/coordinates (FR-004).
TEST_F(DiagnosticsWriterTest, DiagnosticVariableReachesOutputWithMetadata) {
    const cece::CeceOutputConfig config = BaseConfig();
    const cece::DualView3D anomaly = FilledField("temperature_anomaly", kNx, kNy, kNz);

    const auto [rc, nc_path] =
        WriteOneStep(config, kNx, kNy, kNz, {{"temperature_anomaly", MakeDiagnostic(anomaly, "K", "Temperature deviation from reference")}});
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(fs::exists(nc_path)) << nc_path << " was not written";

    ASSERT_TRUE(HasVariable(nc_path, "temperature_anomaly")) << "diagnostic variable missing";

    const auto units = ReadTextAttribute(nc_path, "temperature_anomaly", "units");
    ASSERT_TRUE(units.has_value());
    EXPECT_EQ(*units, "K");

    const auto long_name = ReadTextAttribute(nc_path, "temperature_anomaly", "long_name");
    ASSERT_TRUE(long_name.has_value());
    EXPECT_EQ(*long_name, "Temperature deviation from reference");

    const auto coordinates = ReadTextAttribute(nc_path, "temperature_anomaly", "coordinates");
    ASSERT_TRUE(coordinates.has_value());
    EXPECT_EQ(*coordinates, "time lev lat lon");

    // Shape must be the grid, not the 1x1x1 placeholder (FR-008).
    EXPECT_EQ(ReadAllValues(nc_path, "temperature_anomaly").size(), static_cast<size_t>(kNx) * kNy * kNz);
}

// (b) An empty diagnostics payload changes nothing: the variable inventory is
// exactly the coordinates plus the configured species (FR-010). File-level
// byte equality is not a usable signal here — NetCDF-4/HDF5 embeds a
// nondeterministic superblock UUID — so the regression proof is the per-
// variable content checksum in scripts/diagnostics_baseline.sh (T002/T027).
TEST_F(DiagnosticsWriterTest, EmptyDiagnosticsPayloadChangesNothing) {
    const cece::CeceOutputConfig config = BaseConfig();

    const auto [rc, nc_path] = WriteOneStep(config, kNx, kNy, kNz, {});
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(fs::exists(nc_path)) << nc_path << " was not written";

    EXPECT_EQ(VariableNames(nc_path), std::vector<std::string>({"co", "lat", "lat_bnds", "lev", "lon", "lon_bnds", "time"}));
    EXPECT_FALSE(HasVariable(nc_path, "temperature_anomaly"));
}

// (c) A diagnostic named like a coordinate, or duplicating a species field, is
// refused with -1 BEFORE the dataset is opened — no partial file (FR-009).
TEST_F(DiagnosticsWriterTest, CollisionRejectedBeforeDatasetOpens) {
    const cece::CeceOutputConfig config = BaseConfig();
    const cece::DualView3D view = FilledField("lon", kNx, kNy, kNz);

    const auto [rc, nc_path] = WriteOneStep(config, kNx, kNy, kNz, {{"lon", MakeDiagnostic(view, "K", "collision")}});
    EXPECT_EQ(rc, -1) << "a coordinate-named diagnostic must be refused";
    EXPECT_FALSE(fs::exists(nc_path)) << "a refused write must not produce a NetCDF file";
}

TEST_F(DiagnosticsWriterTest, SpeciesDuplicateRejectedBeforeDatasetOpens) {
    const cece::CeceOutputConfig config = BaseConfig();
    const cece::DualView3D view = FilledField("co", kNx, kNy, kNz);

    const auto [rc, nc_path] = WriteOneStep(config, kNx, kNy, kNz, {{"co", MakeDiagnostic(view, "K", "duplicate species")}});
    EXPECT_EQ(rc, -1) << "a diagnostic duplicating a species field must be refused";
    EXPECT_FALSE(fs::exists(nc_path)) << "a refused write must not produce a NetCDF file";
}

// (d) Diagnostics use the same LayoutLeft -> CF index transform as species:
// written from the identical positional pattern, the two variables must agree
// element-for-element (single source of truth for ordering).
TEST_F(DiagnosticsWriterTest, DiagnosticUsesSpeciesIndexTransform) {
    const cece::CeceOutputConfig config = BaseConfig();
    const cece::DualView3D anomaly = FilledField("temperature_anomaly", kNx, kNy, kNz);

    const auto [rc, nc_path] = WriteOneStep(config, kNx, kNy, kNz, {{"temperature_anomaly", MakeDiagnostic(anomaly, "K", "anomaly")}});
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(fs::exists(nc_path));

    const std::vector<double> species = ReadAllValues(nc_path, "co");
    const std::vector<double> diagnostic = ReadAllValues(nc_path, "temperature_anomaly");

    ASSERT_EQ(species.size(), diagnostic.size());
    ASSERT_FALSE(species.empty());
    for (size_t i = 0; i < species.size(); ++i) {
        EXPECT_DOUBLE_EQ(species[i], diagnostic[i]) << "divergent ordering at flat index " << i;
    }
    // And the pattern itself survived the transform (not just self-consistent).
    EXPECT_DOUBLE_EQ(species[0], Pattern(0, 0, 0));
}

// (e) On an unstructured (ny == 1) layout the diagnostic follows the same UGRID
// manifest rules as species: mesh reference plus face location.
TEST_F(DiagnosticsWriterTest, UnstructuredGridCarriesMeshAttributes) {
    constexpr int nx = 6, ny = 1, nz = 1;
    cece::CeceOutputConfig config = BaseConfig(nx, ny, nz);

    const cece::DualView3D anomaly = FilledField("temperature_anomaly", nx, ny, nz);
    const auto [rc, nc_path] = WriteOneStep(config, nx, ny, nz, {{"temperature_anomaly", MakeDiagnostic(anomaly, "K", "anomaly")}});
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(fs::exists(nc_path)) << nc_path << " was not written";

    ASSERT_TRUE(HasVariable(nc_path, "temperature_anomaly"));

    const auto mesh = ReadTextAttribute(nc_path, "temperature_anomaly", "mesh");
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(*mesh, "mesh");

    const auto location = ReadTextAttribute(nc_path, "temperature_anomaly", "location");
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(*location, "face");
}

}  // namespace

// The writer needs a live MPI environment for AMIO and Kokkos views for data.
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    MPI_Init(&argc, &argv);
    Kokkos::initialize(argc, argv);
    const int rc = RUN_ALL_TESTS();
    Kokkos::finalize();
    MPI_Finalize();
    return rc;
}
