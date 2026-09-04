// Regression tests for output-field attributes in the standalone writer.

#include <gtest/gtest.h>
#include <mpi.h>
#include <netcdf.h>

#include <Kokkos_Core.hpp>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include "cece/cece_config.hpp"
#include "cece/cece_standalone_writer.hpp"
#include "test_mpi_singleton.hpp"

namespace fs = std::filesystem;

namespace {

class StandaloneWriterAttributesTest : public ::testing::Test {
   protected:
    void SetUp() override {
        out_dir_ = fs::temp_directory_path() / "cece_writer_attr_test";
        fs::remove_all(out_dir_);
        fs::create_directories(out_dir_);
    }

    void TearDown() override {
        fs::remove_all(out_dir_);
    }

    // Run one write through the real writer; return the produced file path.
    fs::path WriteOneStep(const cece::CeceOutputConfig& config) {
        constexpr int nx = 4, ny = 3, nz = 1;
        cece::CeceStandaloneWriter writer(config);
        EXPECT_EQ(writer.Initialize("2010-01-01T00:00:00", nx, ny, nz), 0);

        cece::DualView3D co("co", nx, ny, nz);
        auto host = co.view_host();
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                host(i, j, 0) = 1.0;
            }
        }
        co.modify_host();

        std::unordered_map<std::string, cece::DualView3D> fields;
        fields.emplace("co", co);
        EXPECT_EQ(writer.WriteTimeStep(fields, 3600.0, 0), 0);
        writer.Finalize();

        return fs::path(config.directory) / "attr_test_010000.nc";
    }

    // The variable's attribute value, or nullopt when the attribute is absent.
    static std::optional<std::string> ReadTextAttribute(const fs::path& nc_path, const std::string& variable, const std::string& attribute) {
        int nc_id = -1;
        EXPECT_EQ(nc_open(nc_path.c_str(), NC_NOWRITE, &nc_id), NC_NOERR) << "cannot open " << nc_path;
        int var_id = -1;
        EXPECT_EQ(nc_inq_varid(nc_id, variable.c_str(), &var_id), NC_NOERR) << "variable " << variable << " missing";

        size_t length = 0;
        int rc = nc_inq_attlen(nc_id, var_id, attribute.c_str(), &length);
        std::optional<std::string> value;
        if (rc == NC_NOERR) {
            std::string text(length, '\0');
            EXPECT_EQ(nc_get_att_text(nc_id, var_id, attribute.c_str(), text.data()), NC_NOERR);
            value = text;
        }
        nc_close(nc_id);
        return value;
    }

    // Configs are constructed fully formed (the collection is immutable
    // after construction apart from SetTimeUnits): co's attributes are a
    // parameter rather than patched in afterwards.
    cece::CeceOutputConfig BaseConfig(std::map<std::string, std::string> co_attributes = {}) const {
        cece::CeceOutputConfig config;
        config.enabled = true;
        config.directory = out_dir_.string();
        config.filename_pattern = "attr_test_{HH}{mm}{ss}.nc";
        config.frequency_steps = 1;
        config.fields = {{"co", std::move(co_attributes)}};
        // Configs built by hand (no ParseConfig) set time's units themselves,
        // matching the start time passed to Initialize in WriteOneStep.
        config.fields.SetTimeUnits("2010-01-01T00:00:00");
        return config;
    }

    fs::path out_dir_;
};

// A disabled config produces no output at all: every writer entry point
// no-ops.
TEST_F(StandaloneWriterAttributesTest, DisabledConfigWritesNothing) {
    cece::CeceOutputConfig config = BaseConfig();
    config.enabled = false;

    const fs::path nc_path = WriteOneStep(config);
    EXPECT_FALSE(fs::exists(nc_path)) << "disabled writer produced " << nc_path;
    EXPECT_TRUE(fs::is_empty(out_dir_)) << "disabled writer left files in the output directory";
}

// RED before the units fix: the writer fabricated units ("mol mol-1") and a
// mole-fraction long_name for every field. A field with no configured
// attributes must have none — absence over fabrication.
TEST_F(StandaloneWriterAttributesTest, DefaultConfigEmitsNoFabricatedAttributes) {
    const fs::path nc_path = WriteOneStep(BaseConfig());
    ASSERT_TRUE(fs::exists(nc_path)) << nc_path << " was not written";

    const auto units = ReadTextAttribute(nc_path, "co", "units");
    EXPECT_FALSE(units.has_value()) << "unconfigured field carries fabricated units: '" << *units << "'";

    const auto long_name = ReadTextAttribute(nc_path, "co", "long_name");
    EXPECT_FALSE(long_name.has_value()) << "unconfigured field carries fabricated long_name: '" << *long_name << "'";

    // coordinates is structural, not fabricated: present with its default,
    // matching the written field shape [time, lev, lat, lon].
    const auto coordinates = ReadTextAttribute(nc_path, "co", "coordinates");
    ASSERT_TRUE(coordinates.has_value());
    EXPECT_EQ(*coordinates, "time lev lat lon");
}

// A user-supplied coordinates attribute overrides the "time lev lat lon" default.
TEST_F(StandaloneWriterAttributesTest, ConfiguredCoordinatesOverrideTheDefault) {
    const cece::CeceOutputConfig config = BaseConfig({{"coordinates", "lon lat time"}});

    const fs::path nc_path = WriteOneStep(config);
    ASSERT_TRUE(fs::exists(nc_path)) << nc_path << " was not written";

    const auto coordinates = ReadTextAttribute(nc_path, "co", "coordinates");
    ASSERT_TRUE(coordinates.has_value());
    EXPECT_EQ(*coordinates, "lon lat time");
}

// Regression lock for configured per-field attributes: they arrive in the
// NetCDF verbatim.
TEST_F(StandaloneWriterAttributesTest, ConfiguredFieldAttributesReachTheOutput) {
    const cece::CeceOutputConfig config = BaseConfig({
        {"units", "kg m-2 s-1"},
        {"long_name", "carbon_monoxide_emission_flux"},
    });

    const fs::path nc_path = WriteOneStep(config);
    ASSERT_TRUE(fs::exists(nc_path)) << nc_path << " was not written";

    const auto units = ReadTextAttribute(nc_path, "co", "units");
    ASSERT_TRUE(units.has_value());
    EXPECT_EQ(*units, "kg m-2 s-1");

    const auto long_name = ReadTextAttribute(nc_path, "co", "long_name");
    ASSERT_TRUE(long_name.has_value());
    EXPECT_EQ(*long_name, "carbon_monoxide_emission_flux");
}

}  // namespace

// Custom GTest Environment to manage Kokkos & MPI lifecycle globally —
// mirrors the KokkosMpiEnvironment pattern in test_cece_utils.cpp. AMIO
// requires an initialized MPI environment and the writer uses Kokkos views,
// but neither may initialize during `--gtest_list_tests`: CMake's POST_BUILD
// test discovery runs this binary on login nodes, where MPI_Init aborts
// (no PMI job context).
class KokkosMpiEnvironment : public ::testing::Environment {
   private:
    int argc_;
    char** argv_;

   public:
    KokkosMpiEnvironment(int argc, char** argv) : argc_(argc), argv_(argv) {}

    void SetUp() override {
        // Initialize MPI first
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc_, &argv_, MPI_THREAD_MULTIPLE, &provided);
        }

        // Initialize Kokkos
        if (!Kokkos::is_initialized()) {
            Kokkos::initialize(argc_, argv_);
        }
    }
    void TearDown() override {
        // Finalize Kokkos
        if (Kokkos::is_initialized()) {
            Kokkos::finalize();
        }

        // Finalize MPI
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (mpi_initialized) {
            MPI_Finalize();
        }
    }
};

int main(int argc, char** argv) {
    bool is_discovery = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gtest_list_tests") {
            is_discovery = true;
            break;
        }
    }

    if (!is_discovery) {
        // Force standalone MPI init (no Slurm/PMI); see test_mpi_singleton.hpp.
        cece::test::force_mpi_singleton();

        // Initialize MPI to check rank and prevent parallel duplicate execution conflicts of local unit tests
        int mpi_initialized = 0;
        MPI_Initialized(&mpi_initialized);
        if (!mpi_initialized) {
            int provided = 0;
            MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
        }
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rank > 0) {
            MPI_Finalize();
            return 0;
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new KokkosMpiEnvironment(argc, argv));
    return RUN_ALL_TESTS();
}
