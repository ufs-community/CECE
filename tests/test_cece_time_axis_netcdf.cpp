/**
 * @file test_cece_time_axis_netcdf.cpp
 * @brief Integration coverage for the AMIO-backed time-axis path.
 *
 * The unit tests in test_cece_time_indexing.cpp drive bracket_from_coords()
 * with hand-built arrays. This file exercises bracket_from_dataset() against
 * real NetCDF files opened through AMIO, so the parts that only exist on the
 * production path are covered: reading the time coordinate variable, honouring
 * its declared element type, reading the `units`/`calendar` attributes, and
 * falling back to alternate time-variable names.
 *
 * The fixtures are deliberately tiny (a handful of hourly records on a 2x3
 * grid). If AMIO cannot open the file at all -- some builds lack a functional
 * parallel HDF5 -- the tests skip rather than fail, mirroring AMIO's own
 * lifecycle tests.
 */

#include <amio/amio.h>
#include <gtest/gtest.h>
#include <mpi.h>
#include <netcdf.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cece/cece_driver_facade.hpp"

extern "C" {
void amio_set_parent_communicator(MPI_Fint comm);
}

namespace cece {
namespace {

constexpr int kNumRecords = 12;  // hourly, 2020-03-01T00Z .. T11Z

struct AxisSpec {
    const char* time_var = "time";
    nc_type time_type = NC_DOUBLE;
    const char* units = "hours since 2020-03-01 00:00:00";
    const char* calendar = "gregorian";
    double scale_factor = 0.0;  // 0 = do not write the attribute
};

/// Write a small CF-ish file whose time axis follows @p spec. Returns false if
/// the netCDF library rejects anything, so callers can skip.
bool write_axis_file(const std::string& path, const AxisSpec& spec) {
    int ncid = -1;
    if (nc_create(path.c_str(), NC_NETCDF4 | NC_CLOBBER, &ncid) != NC_NOERR) return false;

    int time_dim = -1, lat_dim = -1, lon_dim = -1;
    bool ok = nc_def_dim(ncid, "time", kNumRecords, &time_dim) == NC_NOERR && nc_def_dim(ncid, "lat", 2, &lat_dim) == NC_NOERR &&
              nc_def_dim(ncid, "lon", 3, &lon_dim) == NC_NOERR;

    int tvar = -1;
    ok = ok && nc_def_var(ncid, spec.time_var, spec.time_type, 1, &time_dim, &tvar) == NC_NOERR;
    if (ok && spec.units != nullptr) {
        ok = nc_put_att_text(ncid, tvar, "units", std::strlen(spec.units), spec.units) == NC_NOERR;
    }
    if (ok && spec.calendar != nullptr) {
        ok = nc_put_att_text(ncid, tvar, "calendar", std::strlen(spec.calendar), spec.calendar) == NC_NOERR;
    }
    if (ok && spec.scale_factor != 0.0) {
        ok = nc_put_att_double(ncid, tvar, "scale_factor", NC_DOUBLE, 1, &spec.scale_factor) == NC_NOERR;
    }

    int lat_var = -1, lon_var = -1, data_var = -1;
    const int data_dims[3] = {time_dim, lat_dim, lon_dim};
    ok = ok && nc_def_var(ncid, "lat", NC_DOUBLE, 1, &lat_dim, &lat_var) == NC_NOERR &&
         nc_def_var(ncid, "lon", NC_DOUBLE, 1, &lon_dim, &lon_var) == NC_NOERR &&
         nc_def_var(ncid, "emis", NC_FLOAT, 3, data_dims, &data_var) == NC_NOERR && nc_enddef(ncid) == NC_NOERR;

    if (ok) {
        // Record k is hour k, pre-divided when the axis declares a scale_factor.
        const double stored_scale = (spec.scale_factor != 0.0) ? spec.scale_factor : 1.0;
        if (spec.time_type == NC_INT) {
            std::vector<int> vals(kNumRecords);
            for (int k = 0; k < kNumRecords; ++k) vals[k] = static_cast<int>(k / stored_scale);
            ok = nc_put_var_int(ncid, tvar, vals.data()) == NC_NOERR;
        } else {
            std::vector<double> vals(kNumRecords);
            for (int k = 0; k < kNumRecords; ++k) vals[k] = k / stored_scale;
            ok = nc_put_var_double(ncid, tvar, vals.data()) == NC_NOERR;
        }
    }

    if (ok) {
        const double lats[2] = {-45.0, 45.0};
        const double lons[3] = {-90.0, 0.0, 90.0};
        std::vector<float> field(static_cast<size_t>(kNumRecords) * 2 * 3, 1.0f);
        ok = nc_put_var_double(ncid, lat_var, lats) == NC_NOERR && nc_put_var_double(ncid, lon_var, lons) == NC_NOERR &&
             nc_put_var_float(ncid, data_var, field.data()) == NC_NOERR;
    }

    nc_close(ncid);
    return ok;
}

void write_manifest(const std::string& path, const std::string& data_path) {
    std::ofstream f(path);
    f << "backend: netcdf4\n"
      << "path: " << data_path << "\n"
      << "data_model: classic\n"
      << "staging_pool:\n"
      << "  buffer_count: 4\n"
      << "  buffer_capacity_bytes: 1048576\n"
      << "worker_pool:\n"
      << "  threads: 1\n"
      << "prefetch:\n"
      << "  depth: 2\n"
      << "  read_timeout_s: 60\n"
      << "staging_timeout_ms: 30000\n";
}

/// Opens a fixture through AMIO for the lifetime of the object.
class AmioFixture {
   public:
    explicit AmioFixture(const AxisSpec& spec) {
        const std::string stem = "cece_time_axis_" + std::to_string(getpid()) + "_" + std::to_string(counter_++);
        nc_path_ = stem + ".nc";
        manifest_path_ = stem + "_manifest.yaml";

        wrote_ = write_axis_file(nc_path_, spec);
        if (!wrote_) return;
        write_manifest(manifest_path_, nc_path_);

        // The netCDF backend issues nc_open_par, so it needs a communicator.
        amio_set_parent_communicator(MPI_Comm_c2f(MPI_COMM_SELF));
        if (amio_init(manifest_path_.c_str(), &core_) != AMIO_OK) return;
        if (amio_open_dataset(core_, manifest_path_.c_str(), AMIO_MODE_READ, &dataset_) != AMIO_OK) {
            dataset_ = nullptr;
        }
    }

    ~AmioFixture() {
        if (dataset_ != nullptr) amio_close(dataset_);
        if (core_ != nullptr) amio_finalize(core_);
        std::error_code ec;
        std::filesystem::remove(nc_path_, ec);
        std::filesystem::remove(manifest_path_, ec);
    }

    AmioFixture(const AmioFixture&) = delete;
    AmioFixture& operator=(const AmioFixture&) = delete;

    bool usable() const {
        return wrote_ && dataset_ != nullptr;
    }
    amio_dataset_handle dataset() const {
        return dataset_;
    }

   private:
    static int counter_;
    std::string nc_path_;
    std::string manifest_path_;
    bool wrote_ = false;
    amio_core_handle core_ = nullptr;
    amio_dataset_handle dataset_ = nullptr;
};

int AmioFixture::counter_ = 0;

}  // namespace

TEST(CeceTimeAxisNetcdf, ResolvesRecordFromDecodedFileAxis) {
    using namespace cece::detail;

    AmioFixture fx{AxisSpec{}};
    if (!fx.usable()) GTEST_SKIP() << "AMIO could not open the NetCDF fixture in this build";

    // 05Z is the sixth hourly record.
    const RecordBracket br = bracket_from_dataset(fx.dataset(), "time", parse_sim_datetime("2020-03-01T05:00:00"), kNumRecords, "nearest");
    ASSERT_TRUE(br.valid) << "the file's units/calendar attributes should have decoded";
    EXPECT_EQ(br.i0, 5);

    // Half past blends the two neighbouring records evenly.
    const RecordBracket lin = bracket_from_dataset(fx.dataset(), "time", parse_sim_datetime("2020-03-01T05:30:00"), kNumRecords, "linear");
    ASSERT_TRUE(lin.valid);
    EXPECT_EQ(lin.i0, 5);
    EXPECT_EQ(lin.i1, 6);
    EXPECT_NEAR(lin.weight, 0.5, 1e-9);
}

TEST(CeceTimeAxisNetcdf, ReadsIntegerTimeCoordinate) {
    using namespace cece::detail;

    // int32 "hours since ..." is a very common CF encoding, and the one that a
    // payload-size guess reinterprets as garbage float.
    AxisSpec spec;
    spec.time_type = NC_INT;
    AmioFixture fx{spec};
    if (!fx.usable()) GTEST_SKIP() << "AMIO could not open the NetCDF fixture in this build";

    const RecordBracket br = bracket_from_dataset(fx.dataset(), "time", parse_sim_datetime("2020-03-01T09:00:00"), kNumRecords, "nearest");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 9);
}

TEST(CeceTimeAxisNetcdf, FallsBackToAlternateTimeVariableName) {
    using namespace cece::detail;

    // No variable called "time"; the reader should find "valid_time" and read
    // that variable's attributes, not some other variable's.
    AxisSpec spec;
    spec.time_var = "valid_time";
    AmioFixture fx{spec};
    if (!fx.usable()) GTEST_SKIP() << "AMIO could not open the NetCDF fixture in this build";

    const RecordBracket br = bracket_from_dataset(fx.dataset(), "time", parse_sim_datetime("2020-03-01T03:00:00"), kNumRecords, "nearest");
    ASSERT_TRUE(br.valid) << "alternate time-variable names should be tried";
    EXPECT_EQ(br.i0, 3);
}

TEST(CeceTimeAxisNetcdf, ConfigUnitsOverrideRescuesAFileWithNoUnits) {
    using namespace cece::detail;

    AxisSpec spec;
    spec.units = nullptr;  // nothing to decode from the file itself
    AmioFixture fx{spec};
    if (!fx.usable()) GTEST_SKIP() << "AMIO could not open the NetCDF fixture in this build";

    const SimDateTime dt = parse_sim_datetime("2020-03-01T07:00:00");

    // Without units the axis is undecodable, so the caller degrades.
    EXPECT_FALSE(bracket_from_dataset(fx.dataset(), "time", dt, kNumRecords, "nearest").valid);

    // The stream's time_units setting supplies what the file lacks.
    const RecordBracket br = bracket_from_dataset(fx.dataset(), "time", dt, kNumRecords, "nearest", 0, "", "hours since 2020-03-01 00:00:00");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 7);
}

TEST(CeceTimeAxisNetcdf, AppliesScaleFactorOnTheTimeAxis) {
    using namespace cece::detail;

    // Stored as int half-hour counts with scale_factor 0.5, so record k is
    // still hour k once unpacked. Ignoring the packing would double every
    // record time and halve the resolved index.
    AxisSpec spec;
    spec.time_type = NC_INT;
    spec.scale_factor = 0.5;
    AmioFixture fx{spec};
    if (!fx.usable()) GTEST_SKIP() << "AMIO could not open the NetCDF fixture in this build";

    const RecordBracket br = bracket_from_dataset(fx.dataset(), "time", parse_sim_datetime("2020-03-01T06:00:00"), kNumRecords, "nearest");
    ASSERT_TRUE(br.valid);
    EXPECT_EQ(br.i0, 6);
}

TEST(CeceTimeAxisNetcdf, AppliesTaxmodeToTimesOutsideTheFile) {
    using namespace cece::detail;

    AmioFixture fx{AxisSpec{}};
    if (!fx.usable()) GTEST_SKIP() << "AMIO could not open the NetCDF fixture in this build";

    // The file stops at 11Z; 15Z is four hours past the end.
    const SimDateTime past_end = parse_sim_datetime("2020-03-01T15:00:00");

    const RecordBracket ext = bracket_from_dataset(fx.dataset(), "time", past_end, kNumRecords, "nearest", 0, "extend");
    ASSERT_TRUE(ext.valid);
    EXPECT_EQ(ext.i0, kNumRecords - 1);

    const RecordBracket lim = bracket_from_dataset(fx.dataset(), "time", past_end, kNumRecords, "nearest", 0, "limit");
    EXPECT_FALSE(lim.valid);
    EXPECT_TRUE(lim.out_of_range);

    // A 12-hour file cycles with a 12-hour period, so 15Z is record 3.
    const RecordBracket cyc = bracket_from_dataset(fx.dataset(), "time", past_end, kNumRecords, "nearest", 0, "cycle");
    ASSERT_TRUE(cyc.valid);
    EXPECT_EQ(cyc.i0, 3);
}

}  // namespace cece

namespace {

class MpiEnvironment : public ::testing::Environment {
   public:
    void SetUp() override {
        int initialized = 0;
        MPI_Initialized(&initialized);
        if (!initialized) {
            int provided = 0;
            MPI_Init_thread(nullptr, nullptr, MPI_THREAD_MULTIPLE, &provided);
        }
    }
    void TearDown() override {
        int initialized = 0;
        MPI_Initialized(&initialized);
        if (initialized) MPI_Finalize();
    }
};

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new MpiEnvironment());
    return RUN_ALL_TESTS();
}
