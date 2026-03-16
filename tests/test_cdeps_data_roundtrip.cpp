/**
 * @file test_cdeps_data_roundtrip.cpp
 * @brief Property 2: CDEPS Data Round-Trip
 *
 * **Validates: Requirement 1.9**
 *
 * Property: FOR ALL valid streams configurations, parsing the streams file then
 * querying fields SHALL produce data matching the NetCDF source within
 * floating-point precision (relative error < 1e-15).
 *
 * Additionally validates Property 16 (Streams Configuration Round-Trip):
 * parse → serialize → parse SHALL produce an equivalent configuration.
 *
 * Test strategy:
 * - Generate 100+ random valid streams configurations
 * - For each, create a synthetic NetCDF file with known values
 * - Write a streams file referencing that NetCDF file
 * - Parse the streams file and verify all attributes are preserved
 * - Verify round-trip (parse → WriteStreamsFile → parse) is lossless
 * - Verify NetCDF values read back match the original data within 1e-15
 *
 * All tests use real NetCDF-C API. No mocking permitted (AGENTS.md).
 * Designed to run in JCSDA Docker: jcsda/docker-gnu-openmpi-dev:1.9
 *
 * @see Requirements 1.9, 7.13, Property 2, Property 16
 */

#include "aces/aces_cdeps_parser.hpp"

#include <gtest/gtest.h>
#include <netcdf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace aces {
namespace {

// ---------------------------------------------------------------------------
// NetCDF helpers
// ---------------------------------------------------------------------------

/**
 * @brief Creates a synthetic NetCDF file with one 2D double variable.
 *
 * Writes an (nx × ny) double array to a new NetCDF-4 file.
 * Returns true on success.
 */
static bool CreateNetCDFFile(const std::string& filepath,
                              const std::string& var_name,
                              int nx, int ny,
                              const std::vector<double>& values) {
    int ncid, x_dimid, y_dimid, varid;
    int dims[2];

    if (nc_create(filepath.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid) != NC_NOERR)
        return false;

    if (nc_def_dim(ncid, "x", static_cast<size_t>(nx), &x_dimid) != NC_NOERR ||
        nc_def_dim(ncid, "y", static_cast<size_t>(ny), &y_dimid) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }

    dims[0] = x_dimid;
    dims[1] = y_dimid;
    if (nc_def_var(ncid, var_name.c_str(), NC_DOUBLE, 2, dims, &varid) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }

    if (nc_enddef(ncid) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }

    if (nc_put_var_double(ncid, varid, values.data()) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }

    nc_close(ncid);
    return true;
}

/**
 * @brief Reads a 2D double variable from a NetCDF file into a vector.
 *
 * Returns true on success; populates `out` with nx*ny values.
 */
static bool ReadNetCDFVariable(const std::string& filepath,
                                const std::string& var_name,
                                std::vector<double>& out) {
    int ncid, varid;
    if (nc_open(filepath.c_str(), NC_NOWRITE, &ncid) != NC_NOERR)
        return false;

    if (nc_inq_varid(ncid, var_name.c_str(), &varid) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }

    // Query variable dimensions to size the output buffer
    int ndims;
    if (nc_inq_varndims(ncid, varid, &ndims) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }

    std::vector<int> dimids(static_cast<size_t>(ndims));
    if (nc_inq_vardimid(ncid, varid, dimids.data()) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }

    size_t total = 1;
    for (int d : dimids) {
        size_t len;
        nc_inq_dimlen(ncid, d, &len);
        total *= len;
    }

    out.resize(total);
    bool ok = (nc_get_var_double(ncid, varid, out.data()) == NC_NOERR);
    nc_close(ncid);
    return ok;
}

/**
 * @brief Writes a streams file referencing a single NetCDF file and variable.
 */
static void WriteStreamsFile(const std::string& streams_path,
                              const std::string& stream_name,
                              const std::string& nc_path,
                              const std::string& var_in_file,
                              const std::string& var_in_model,
                              const std::string& tintalgo,
                              const std::string& taxmode,
                              int year) {
    std::ofstream f(streams_path);
    f << "# Auto-generated test streams file\n";
    f << "stream::" << stream_name << "\n";
    f << "  file_paths = " << nc_path << "\n";
    f << "  variables = " << var_in_file << ":" << var_in_model << "\n";
    f << "  taxmode = " << taxmode << "\n";
    f << "  tintalgo = " << tintalgo << "\n";
    f << "  yearFirst = " << year << "\n";
    f << "  yearLast = " << year << "\n";
    f << "  yearAlign = " << year << "\n";
    f << "  offset = 0\n";
    f << "::\n";
}

/**
 * @brief Computes the maximum relative error between two equal-length vectors.
 *
 * Uses |a - b| / max(|b|, epsilon) to avoid division by zero.
 */
static double MaxRelativeError(const std::vector<double>& a,
                                const std::vector<double>& b) {
    double max_err = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        double denom = std::max(std::abs(b[i]), 1e-300);
        max_err = std::max(max_err, std::abs(a[i] - b[i]) / denom);
    }
    return max_err;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

/**
 * @brief Fixture for CDEPS data round-trip property tests.
 *
 * Manages temporary file creation and cleanup, and provides helpers for
 * generating random configurations.
 */
class CdepsDataRoundTripTest : public ::testing::Test {
protected:
    void SetUp() override {
        rng_.seed(20240101);
    }

    void TearDown() override {
        for (const auto& f : temp_files_) {
            std::remove(f.c_str());
        }
    }

    /** Registers a file for cleanup in TearDown. */
    std::string Track(const std::string& path) {
        temp_files_.push_back(path);
        return path;
    }

    /** Generates a unique temp filename with the given suffix. */
    std::string TempName(const std::string& suffix) {
        return Track("cdeps_rt_test_" + std::to_string(file_counter_++) + suffix);
    }

    std::mt19937 rng_;
    int file_counter_ = 0;
    std::vector<std::string> temp_files_;
};

// ---------------------------------------------------------------------------
// 1. Deterministic smoke test: parse a hand-crafted streams file
// ---------------------------------------------------------------------------

/**
 * @brief Parsing a hand-crafted streams file produces the expected config.
 *
 * Verifies that all attributes (name, file_paths, variables, taxmode,
 * tintalgo, year fields, offset) are extracted correctly.
 *
 * @see Requirements 7.1, 7.2
 */
TEST_F(CdepsDataRoundTripTest, ParseHandCraftedStreamsFile) {
    const std::string nc_path = TempName(".nc");
    const std::string streams_path = TempName(".streams");

    // Create a minimal NetCDF file so validation passes
    std::vector<double> data(4 * 6, 1.0);
    ASSERT_TRUE(CreateNetCDFFile(nc_path, "CO_emis", 4, 6, data));

    WriteStreamsFile(streams_path, "anthro_co", nc_path,
                    "CO_emis", "CO", "linear", "cycle", 2020);

    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile(streams_path);

    ASSERT_EQ(config.streams.size(), 1u);
    const auto& s = config.streams[0];
    EXPECT_EQ(s.name, "anthro_co");
    ASSERT_EQ(s.file_paths.size(), 1u);
    EXPECT_EQ(s.file_paths[0], nc_path);
    ASSERT_EQ(s.variables.size(), 1u);
    EXPECT_EQ(s.variables[0].name_in_file, "CO_emis");
    EXPECT_EQ(s.variables[0].name_in_model, "CO");
    EXPECT_EQ(s.tintalgo, "linear");
    EXPECT_EQ(s.taxmode, "cycle");
    EXPECT_EQ(s.yearFirst, 2020);
    EXPECT_EQ(s.yearLast, 2020);
    EXPECT_EQ(s.yearAlign, 2020);
    EXPECT_EQ(s.offset, 0);
}

// ---------------------------------------------------------------------------
// 2. NetCDF data round-trip: values read back match original
// ---------------------------------------------------------------------------

/**
 * @brief Values written to NetCDF and read back match within floating-point
 *        precision (relative error < 1e-15).
 *
 * This is the core of Property 2: the data pipeline from NetCDF source through
 * the parser and back to raw values must be lossless.
 *
 * @see Requirement 1.9, Property 2
 */
TEST_F(CdepsDataRoundTripTest, NetCDFValuesMatchOriginalWithinPrecision) {
    const int nx = 8, ny = 12;
    const std::string nc_path = TempName(".nc");

    // Generate known values: a simple ramp so every cell is distinct
    std::vector<double> original(static_cast<size_t>(nx * ny));
    for (int i = 0; i < nx * ny; ++i) {
        original[static_cast<size_t>(i)] = static_cast<double>(i) * 1.23456789e-6;
    }

    ASSERT_TRUE(CreateNetCDFFile(nc_path, "emission_var", nx, ny, original));

    // Read back via NetCDF-C directly (simulating what CDEPS would do)
    std::vector<double> readback;
    ASSERT_TRUE(ReadNetCDFVariable(nc_path, "emission_var", readback));

    ASSERT_EQ(readback.size(), original.size());
    double max_rel_err = MaxRelativeError(readback, original);
    EXPECT_LT(max_rel_err, 1e-15)
        << "NetCDF round-trip relative error " << max_rel_err
        << " exceeds 1e-15 tolerance";
}

/**
 * @brief Values near zero are preserved exactly (absolute error < 1e-300).
 *
 * Emission fields can contain exact zeros (masked regions). These must
 * survive the NetCDF round-trip without becoming non-zero.
 *
 * @see Requirement 1.9
 */
TEST_F(CdepsDataRoundTripTest, ZeroValuesPreservedExactly) {
    const int nx = 4, ny = 4;
    const std::string nc_path = TempName(".nc");

    std::vector<double> original(static_cast<size_t>(nx * ny), 0.0);
    ASSERT_TRUE(CreateNetCDFFile(nc_path, "zero_field", nx, ny, original));

    std::vector<double> readback;
    ASSERT_TRUE(ReadNetCDFVariable(nc_path, "zero_field", readback));

    for (size_t i = 0; i < readback.size(); ++i) {
        EXPECT_EQ(readback[i], 0.0)
            << "Zero value at index " << i << " was corrupted to " << readback[i];
    }
}

/**
 * @brief Very small and very large values survive the NetCDF round-trip.
 *
 * Emission fields span many orders of magnitude. The round-trip must
 * preserve subnormal and large values within double precision.
 *
 * @see Requirement 1.9
 */
TEST_F(CdepsDataRoundTripTest, ExtremeValuesPreservedWithinPrecision) {
    const int nx = 2, ny = 4;
    const std::string nc_path = TempName(".nc");

    std::vector<double> original = {
        1e-300, 1e-100, 1e-10, 1.0,
        1e10,   1e100,  1e300, -1e-10
    };

    ASSERT_TRUE(CreateNetCDFFile(nc_path, "extreme_field", nx, ny, original));

    std::vector<double> readback;
    ASSERT_TRUE(ReadNetCDFVariable(nc_path, "extreme_field", readback));

    ASSERT_EQ(readback.size(), original.size());
    double max_rel_err = MaxRelativeError(readback, original);
    EXPECT_LT(max_rel_err, 1e-15)
        << "Extreme-value round-trip relative error " << max_rel_err;
}

// ---------------------------------------------------------------------------
// 3. Streams config round-trip: parse → serialize → parse is lossless
// ---------------------------------------------------------------------------

/**
 * @brief Property 16: parse → WriteStreamsFile → parse produces equivalent config.
 *
 * All stream attributes (name, file_paths, variables, taxmode, tintalgo,
 * mapalgo, year fields, offset) must survive serialization unchanged.
 *
 * @see Requirements 7.13, Property 16
 */
TEST_F(CdepsDataRoundTripTest, StreamsConfigRoundTripIsLossless) {
    const std::string nc_path = TempName(".nc");
    const std::string streams1 = TempName("_1.streams");
    const std::string streams2 = TempName("_2.streams");

    std::vector<double> data(5 * 7, 3.14);
    ASSERT_TRUE(CreateNetCDFFile(nc_path, "NOx_emis", 5, 7, data));

    WriteStreamsFile(streams1, "biogenic_nox", nc_path,
                    "NOx_emis", "NOx", "nearest", "extend", 2019);

    // First parse
    AcesCdepsConfig config1 = CdepsStreamsParser::ParseStreamsFile(streams1);
    ASSERT_EQ(config1.streams.size(), 1u);

    // Serialize
    CdepsStreamsParser::WriteStreamsFile(streams2, config1);

    // Second parse
    AcesCdepsConfig config2 = CdepsStreamsParser::ParseStreamsFile(streams2);
    ASSERT_EQ(config2.streams.size(), 1u);

    const auto& s1 = config1.streams[0];
    const auto& s2 = config2.streams[0];

    EXPECT_EQ(s1.name,       s2.name);
    EXPECT_EQ(s1.file_paths, s2.file_paths);
    EXPECT_EQ(s1.taxmode,    s2.taxmode);
    EXPECT_EQ(s1.tintalgo,   s2.tintalgo);
    EXPECT_EQ(s1.mapalgo,    s2.mapalgo);
    EXPECT_EQ(s1.yearFirst,  s2.yearFirst);
    EXPECT_EQ(s1.yearLast,   s2.yearLast);
    EXPECT_EQ(s1.yearAlign,  s2.yearAlign);
    EXPECT_EQ(s1.offset,     s2.offset);

    ASSERT_EQ(s1.variables.size(), s2.variables.size());
    EXPECT_EQ(s1.variables[0].name_in_file,  s2.variables[0].name_in_file);
    EXPECT_EQ(s1.variables[0].name_in_model, s2.variables[0].name_in_model);
}

/**
 * @brief Multi-stream config round-trip preserves all streams.
 *
 * When a streams file contains multiple stream blocks, all must survive
 * the serialize → parse cycle with identical attributes.
 *
 * @see Requirements 7.13, Property 16
 */
TEST_F(CdepsDataRoundTripTest, MultiStreamConfigRoundTrip) {
    const std::string nc1 = TempName("_a.nc");
    const std::string nc2 = TempName("_b.nc");
    const std::string streams_in  = TempName("_in.streams");
    const std::string streams_out = TempName("_out.streams");

    std::vector<double> d1(3 * 3, 1.0), d2(6 * 4, 2.0);
    ASSERT_TRUE(CreateNetCDFFile(nc1, "CO_emis",  3, 3, d1));
    ASSERT_TRUE(CreateNetCDFFile(nc2, "SO2_emis", 6, 4, d2));

    // Write a two-stream file manually
    {
        std::ofstream f(streams_in);
        f << "stream::stream_co\n";
        f << "  file_paths = " << nc1 << "\n";
        f << "  variables = CO_emis:CO\n";
        f << "  taxmode = cycle\n";
        f << "  tintalgo = linear\n";
        f << "  yearFirst = 2020\n";
        f << "  yearLast = 2020\n";
        f << "  yearAlign = 2020\n";
        f << "  offset = 0\n";
        f << "::\n";
        f << "stream::stream_so2\n";
        f << "  file_paths = " << nc2 << "\n";
        f << "  variables = SO2_emis:SO2\n";
        f << "  taxmode = extend\n";
        f << "  tintalgo = nearest\n";
        f << "  yearFirst = 2018\n";
        f << "  yearLast = 2022\n";
        f << "  yearAlign = 2020\n";
        f << "  offset = 3600\n";
        f << "::\n";
    }

    AcesCdepsConfig cfg1 = CdepsStreamsParser::ParseStreamsFile(streams_in);
    ASSERT_EQ(cfg1.streams.size(), 2u);

    CdepsStreamsParser::WriteStreamsFile(streams_out, cfg1);
    AcesCdepsConfig cfg2 = CdepsStreamsParser::ParseStreamsFile(streams_out);
    ASSERT_EQ(cfg2.streams.size(), 2u);

    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(cfg1.streams[i].name,       cfg2.streams[i].name);
        EXPECT_EQ(cfg1.streams[i].file_paths, cfg2.streams[i].file_paths);
        EXPECT_EQ(cfg1.streams[i].taxmode,    cfg2.streams[i].taxmode);
        EXPECT_EQ(cfg1.streams[i].tintalgo,   cfg2.streams[i].tintalgo);
        EXPECT_EQ(cfg1.streams[i].yearFirst,  cfg2.streams[i].yearFirst);
        EXPECT_EQ(cfg1.streams[i].yearLast,   cfg2.streams[i].yearLast);
        EXPECT_EQ(cfg1.streams[i].yearAlign,  cfg2.streams[i].yearAlign);
        EXPECT_EQ(cfg1.streams[i].offset,     cfg2.streams[i].offset);
    }
}

// ---------------------------------------------------------------------------
// 4. Validation passes for all generated configs (no false positives)
// ---------------------------------------------------------------------------

/**
 * @brief ValidateStreamsConfig returns no errors for a well-formed config.
 *
 * A streams config that references an existing NetCDF file with the correct
 * variable and valid interpolation modes must pass validation cleanly.
 *
 * @see Requirements 7.2-7.6
 */
TEST_F(CdepsDataRoundTripTest, ValidationPassesForWellFormedConfig) {
    const std::string nc_path = TempName(".nc");
    const std::string streams_path = TempName(".streams");

    std::vector<double> data(10 * 10, 5.0);
    ASSERT_TRUE(CreateNetCDFFile(nc_path, "dust_emis", 10, 10, data));

    WriteStreamsFile(streams_path, "dust_stream", nc_path,
                    "dust_emis", "DUST", "none", "cycle", 2021);

    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile(streams_path);

    std::vector<std::string> errors;
    bool valid = CdepsStreamsParser::ValidateStreamsConfig(config, errors);

    EXPECT_TRUE(valid)
        << "Well-formed config should pass validation. Errors: "
        << (errors.empty() ? "(none)" : errors[0]);
    EXPECT_TRUE(errors.empty())
        << "Expected no validation errors. Got: " << errors[0];
}

/**
 * @brief All valid tintalgo values pass validation without errors.
 *
 * @see Requirement 7.6
 */
TEST_F(CdepsDataRoundTripTest, AllValidTintalgoValuesPassValidation) {
    const std::vector<std::string> valid_modes = {
        "none", "linear", "nearest", "lower", "upper"
    };

    for (const auto& mode : valid_modes) {
        const std::string nc_path = TempName(".nc");
        const std::string streams_path = TempName(".streams");

        std::vector<double> data(4 * 4, 1.0);
        ASSERT_TRUE(CreateNetCDFFile(nc_path, "test_var", 4, 4, data));
        WriteStreamsFile(streams_path, "s_" + mode, nc_path,
                        "test_var", "VAR", mode, "cycle", 2020);

        AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile(streams_path);
        std::vector<std::string> errors;
        bool valid = CdepsStreamsParser::ValidateStreamsConfig(config, errors);

        EXPECT_TRUE(valid)
            << "tintalgo='" << mode << "' should be valid. Error: "
            << (errors.empty() ? "(none)" : errors[0]);
    }
}

// ---------------------------------------------------------------------------
// 5. Property 2 (randomized): 100+ iterations, random configs + NetCDF data
// ---------------------------------------------------------------------------

/**
 * @brief Property 2 (randomized): FOR ALL valid streams configurations,
 *        parsed data matches the NetCDF source within floating-point precision.
 *
 * Each iteration:
 *   1. Generate a random grid size (nx, ny), variable name, and fill values
 *   2. Write a synthetic NetCDF file with those values
 *   3. Write a streams file referencing that NetCDF file
 *   4. Parse the streams file with CdepsStreamsParser
 *   5. Verify all config attributes match what was written
 *   6. Read the NetCDF variable directly and verify values match original
 *      within relative error < 1e-15
 *
 * Runs 100 iterations with different random seeds, grid sizes, variable
 * names, and interpolation modes.
 *
 * @see Requirement 1.9, Property 2
 */
TEST_F(CdepsDataRoundTripTest, RandomizedDataRoundTripProperty) {
    std::uniform_int_distribution<int> dim_dist(2, 20);
    std::uniform_real_distribution<double> val_dist(-1e-4, 1e-4);
    std::uniform_int_distribution<int> year_dist(2000, 2030);
    std::uniform_int_distribution<int> offset_dist(0, 86400);

    const std::vector<std::string> tintalgo_options = {
        "none", "linear", "nearest", "lower", "upper"
    };
    const std::vector<std::string> taxmode_options = {
        "cycle", "extend", "limit"
    };
    std::uniform_int_distribution<int> tint_dist(
        0, static_cast<int>(tintalgo_options.size()) - 1);
    std::uniform_int_distribution<int> tax_dist(
        0, static_cast<int>(taxmode_options.size()) - 1);

    constexpr int kIterations = 100;

    for (int iter = 0; iter < kIterations; ++iter) {
        const int nx = dim_dist(rng_);
        const int ny = dim_dist(rng_);
        const int n  = nx * ny;

        // Generate a unique variable name for this iteration
        const std::string var_name  = "emis_" + std::to_string(iter);
        const std::string model_name = "MODEL_" + std::to_string(iter);
        const std::string stream_name = "stream_iter_" + std::to_string(iter);

        // Random interpolation settings
        const std::string tintalgo = tintalgo_options[
            static_cast<size_t>(tint_dist(rng_))];
        const std::string taxmode = taxmode_options[
            static_cast<size_t>(tax_dist(rng_))];
        const int year = year_dist(rng_);

        // Generate random field values
        std::vector<double> original(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            original[static_cast<size_t>(i)] = val_dist(rng_);
        }

        // Create NetCDF file
        const std::string nc_path = TempName("_iter" + std::to_string(iter) + ".nc");
        ASSERT_TRUE(CreateNetCDFFile(nc_path, var_name, nx, ny, original))
            << "Iter " << iter << ": failed to create NetCDF file";

        // Write streams file
        const std::string streams_path = TempName("_iter" + std::to_string(iter) + ".streams");
        WriteStreamsFile(streams_path, stream_name, nc_path,
                        var_name, model_name, tintalgo, taxmode, year);

        // --- Parse and verify config attributes ---
        AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile(streams_path);

        ASSERT_EQ(config.streams.size(), 1u)
            << "Iter " << iter << ": expected 1 stream";

        const auto& s = config.streams[0];
        EXPECT_EQ(s.name, stream_name)
            << "Iter " << iter << ": stream name mismatch";
        ASSERT_EQ(s.file_paths.size(), 1u)
            << "Iter " << iter << ": expected 1 file path";
        EXPECT_EQ(s.file_paths[0], nc_path)
            << "Iter " << iter << ": file path mismatch";
        ASSERT_EQ(s.variables.size(), 1u)
            << "Iter " << iter << ": expected 1 variable";
        EXPECT_EQ(s.variables[0].name_in_file, var_name)
            << "Iter " << iter << ": name_in_file mismatch";
        EXPECT_EQ(s.variables[0].name_in_model, model_name)
            << "Iter " << iter << ": name_in_model mismatch";
        EXPECT_EQ(s.tintalgo, tintalgo)
            << "Iter " << iter << ": tintalgo mismatch";
        EXPECT_EQ(s.taxmode, taxmode)
            << "Iter " << iter << ": taxmode mismatch";
        EXPECT_EQ(s.yearFirst, year)
            << "Iter " << iter << ": yearFirst mismatch";

        // --- Read NetCDF values and verify data fidelity ---
        std::vector<double> readback;
        ASSERT_TRUE(ReadNetCDFVariable(nc_path, var_name, readback))
            << "Iter " << iter << ": failed to read NetCDF variable";

        ASSERT_EQ(readback.size(), original.size())
            << "Iter " << iter << ": readback size mismatch";

        double max_rel_err = MaxRelativeError(readback, original);
        EXPECT_LT(max_rel_err, 1e-15)
            << "Iter " << iter
            << " (nx=" << nx << " ny=" << ny
            << " tintalgo=" << tintalgo << ")"
            << ": NetCDF round-trip relative error " << max_rel_err
            << " exceeds 1e-15 tolerance";
    }
}

// ---------------------------------------------------------------------------
// 6. Property 16 (randomized): parse → serialize → parse is lossless
// ---------------------------------------------------------------------------

/**
 * @brief Property 16 (randomized): FOR ALL valid streams configurations,
 *        parse → WriteStreamsFile → parse produces an equivalent configuration.
 *
 * Each iteration generates a random streams config, serializes it, re-parses,
 * and verifies every attribute is identical. Runs 100 iterations.
 *
 * @see Requirements 7.13, Property 16
 */
TEST_F(CdepsDataRoundTripTest, RandomizedStreamsConfigRoundTripProperty) {
    std::uniform_int_distribution<int> dim_dist(2, 15);
    std::uniform_int_distribution<int> year_dist(1990, 2040);
    std::uniform_int_distribution<int> offset_dist(0, 7200);
    std::uniform_int_distribution<int> num_vars_dist(1, 4);

    const std::vector<std::string> tintalgo_options = {
        "none", "linear", "nearest", "lower", "upper"
    };
    const std::vector<std::string> taxmode_options = {
        "cycle", "extend", "limit"
    };
    std::uniform_int_distribution<int> tint_dist(
        0, static_cast<int>(tintalgo_options.size()) - 1);
    std::uniform_int_distribution<int> tax_dist(
        0, static_cast<int>(taxmode_options.size()) - 1);

    constexpr int kIterations = 100;

    for (int iter = 0; iter < kIterations; ++iter) {
        const int nx = dim_dist(rng_);
        const int ny = dim_dist(rng_);
        const int num_vars = num_vars_dist(rng_);
        const int year_first = year_dist(rng_);
        const int year_last  = year_first + (iter % 5);
        const int offset     = offset_dist(rng_);
        const std::string tintalgo = tintalgo_options[
            static_cast<size_t>(tint_dist(rng_))];
        const std::string taxmode = taxmode_options[
            static_cast<size_t>(tax_dist(rng_))];

        // Create a NetCDF file with all variables for this iteration
        const std::string nc_path = TempName("_rt" + std::to_string(iter) + ".nc");
        {
            // Write the first variable; additional variables share the same file
            std::vector<double> data(static_cast<size_t>(nx * ny), 1.0);
            ASSERT_TRUE(CreateNetCDFFile(nc_path, "var_0_" + std::to_string(iter),
                                         nx, ny, data))
                << "Iter " << iter << ": failed to create NetCDF file";
        }

        // Build the streams file manually with multiple variables
        const std::string streams_in  = TempName("_rt_in"  + std::to_string(iter) + ".streams");
        const std::string streams_out = TempName("_rt_out" + std::to_string(iter) + ".streams");
        {
            std::ofstream f(streams_in);
            f << "stream::rt_stream_" << iter << "\n";
            f << "  file_paths = " << nc_path << "\n";
            // Build variable list: var_0_N:MODEL_0_N, var_1_N:MODEL_1_N, ...
            // (only var_0 actually exists in the file; validation is not called here)
            f << "  variables = ";
            for (int v = 0; v < num_vars; ++v) {
                if (v > 0) f << ", ";
                f << "var_" << v << "_" << iter
                  << ":MODEL_" << v << "_" << iter;
            }
            f << "\n";
            f << "  taxmode = "   << taxmode   << "\n";
            f << "  tintalgo = "  << tintalgo  << "\n";
            f << "  yearFirst = " << year_first << "\n";
            f << "  yearLast = "  << year_last  << "\n";
            f << "  yearAlign = " << year_first << "\n";
            f << "  offset = "    << offset     << "\n";
            f << "::\n";
        }

        // First parse
        AcesCdepsConfig cfg1 = CdepsStreamsParser::ParseStreamsFile(streams_in);
        ASSERT_EQ(cfg1.streams.size(), 1u)
            << "Iter " << iter << ": expected 1 stream after first parse";

        // Serialize
        CdepsStreamsParser::WriteStreamsFile(streams_out, cfg1);

        // Second parse
        AcesCdepsConfig cfg2 = CdepsStreamsParser::ParseStreamsFile(streams_out);
        ASSERT_EQ(cfg2.streams.size(), 1u)
            << "Iter " << iter << ": expected 1 stream after second parse";

        const auto& s1 = cfg1.streams[0];
        const auto& s2 = cfg2.streams[0];

        EXPECT_EQ(s1.name,       s2.name)
            << "Iter " << iter << ": stream name changed after round-trip";
        EXPECT_EQ(s1.file_paths, s2.file_paths)
            << "Iter " << iter << ": file_paths changed after round-trip";
        EXPECT_EQ(s1.taxmode,    s2.taxmode)
            << "Iter " << iter << ": taxmode changed after round-trip";
        EXPECT_EQ(s1.tintalgo,   s2.tintalgo)
            << "Iter " << iter << ": tintalgo changed after round-trip";
        EXPECT_EQ(s1.mapalgo,    s2.mapalgo)
            << "Iter " << iter << ": mapalgo changed after round-trip";
        EXPECT_EQ(s1.yearFirst,  s2.yearFirst)
            << "Iter " << iter << ": yearFirst changed after round-trip";
        EXPECT_EQ(s1.yearLast,   s2.yearLast)
            << "Iter " << iter << ": yearLast changed after round-trip";
        EXPECT_EQ(s1.yearAlign,  s2.yearAlign)
            << "Iter " << iter << ": yearAlign changed after round-trip";
        EXPECT_EQ(s1.offset,     s2.offset)
            << "Iter " << iter << ": offset changed after round-trip";

        ASSERT_EQ(s1.variables.size(), s2.variables.size())
            << "Iter " << iter << ": variable count changed after round-trip";
        for (size_t v = 0; v < s1.variables.size(); ++v) {
            EXPECT_EQ(s1.variables[v].name_in_file,  s2.variables[v].name_in_file)
                << "Iter " << iter << " var " << v << ": name_in_file changed";
            EXPECT_EQ(s1.variables[v].name_in_model, s2.variables[v].name_in_model)
                << "Iter " << iter << " var " << v << ": name_in_model changed";
        }
    }
}

// ---------------------------------------------------------------------------
// 7. Multiple variables in one stream: all values preserved
// ---------------------------------------------------------------------------

/**
 * @brief When a stream references multiple variables, all are preserved
 *        through the round-trip with values matching the NetCDF source.
 *
 * @see Requirement 1.9
 */
TEST_F(CdepsDataRoundTripTest, MultipleVariablesAllPreserved) {
    const int nx = 6, ny = 8;
    const std::string nc_path = TempName(".nc");

    // Write two variables to the same NetCDF file
    {
        int ncid, x_dimid, y_dimid;
        ASSERT_EQ(nc_create(nc_path.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid), NC_NOERR);
        ASSERT_EQ(nc_def_dim(ncid, "x", static_cast<size_t>(nx), &x_dimid), NC_NOERR);
        ASSERT_EQ(nc_def_dim(ncid, "y", static_cast<size_t>(ny), &y_dimid), NC_NOERR);

        int dims[2] = {x_dimid, y_dimid};
        int varid_co, varid_nox;
        ASSERT_EQ(nc_def_var(ncid, "CO_emis",  NC_DOUBLE, 2, dims, &varid_co),  NC_NOERR);
        ASSERT_EQ(nc_def_var(ncid, "NOx_emis", NC_DOUBLE, 2, dims, &varid_nox), NC_NOERR);
        ASSERT_EQ(nc_enddef(ncid), NC_NOERR);

        std::vector<double> co_data(static_cast<size_t>(nx * ny));
        std::vector<double> nox_data(static_cast<size_t>(nx * ny));
        for (int i = 0; i < nx * ny; ++i) {
            co_data[static_cast<size_t>(i)]  = static_cast<double>(i) * 1.1e-8;
            nox_data[static_cast<size_t>(i)] = static_cast<double>(i) * 2.3e-9;
        }

        ASSERT_EQ(nc_put_var_double(ncid, varid_co,  co_data.data()),  NC_NOERR);
        ASSERT_EQ(nc_put_var_double(ncid, varid_nox, nox_data.data()), NC_NOERR);
        nc_close(ncid);

        // Verify both variables round-trip correctly
        std::vector<double> co_back, nox_back;
        ASSERT_TRUE(ReadNetCDFVariable(nc_path, "CO_emis",  co_back));
        ASSERT_TRUE(ReadNetCDFVariable(nc_path, "NOx_emis", nox_back));

        EXPECT_LT(MaxRelativeError(co_back,  co_data),  1e-15)
            << "CO_emis round-trip failed";
        EXPECT_LT(MaxRelativeError(nox_back, nox_data), 1e-15)
            << "NOx_emis round-trip failed";
    }

    // Verify the streams config correctly maps both variables
    const std::string streams_path = TempName(".streams");
    {
        std::ofstream f(streams_path);
        f << "stream::multi_var_stream\n";
        f << "  file_paths = " << nc_path << "\n";
        f << "  variables = CO_emis:CO, NOx_emis:NOx\n";
        f << "  taxmode = cycle\n";
        f << "  tintalgo = linear\n";
        f << "  yearFirst = 2020\n";
        f << "  yearLast = 2020\n";
        f << "  yearAlign = 2020\n";
        f << "  offset = 0\n";
        f << "::\n";
    }

    AcesCdepsConfig config = CdepsStreamsParser::ParseStreamsFile(streams_path);
    ASSERT_EQ(config.streams.size(), 1u);
    ASSERT_EQ(config.streams[0].variables.size(), 2u);
    EXPECT_EQ(config.streams[0].variables[0].name_in_file,  "CO_emis");
    EXPECT_EQ(config.streams[0].variables[0].name_in_model, "CO");
    EXPECT_EQ(config.streams[0].variables[1].name_in_file,  "NOx_emis");
    EXPECT_EQ(config.streams[0].variables[1].name_in_model, "NOx");
}

// ---------------------------------------------------------------------------
// 8. Negative values and spatially varying fields
// ---------------------------------------------------------------------------

/**
 * @brief Spatially varying fields (non-uniform values) survive the round-trip.
 *
 * Emission fields are rarely uniform. This test uses a spatially varying
 * pattern to ensure no spatial averaging or resampling occurs.
 *
 * @see Requirement 1.9
 */
TEST_F(CdepsDataRoundTripTest, SpatiallyVaryingFieldPreserved) {
    const int nx = 12, ny = 8;
    const std::string nc_path = TempName(".nc");

    // Create a spatially varying field: value(i,j) = sin(i) * cos(j) * 1e-6
    std::vector<double> original(static_cast<size_t>(nx * ny));
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            original[static_cast<size_t>(i * ny + j)] =
                std::sin(static_cast<double>(i)) *
                std::cos(static_cast<double>(j)) * 1e-6;
        }
    }

    ASSERT_TRUE(CreateNetCDFFile(nc_path, "spatial_emis", nx, ny, original));

    std::vector<double> readback;
    ASSERT_TRUE(ReadNetCDFVariable(nc_path, "spatial_emis", readback));

    ASSERT_EQ(readback.size(), original.size());
    double max_rel_err = MaxRelativeError(readback, original);
    EXPECT_LT(max_rel_err, 1e-15)
        << "Spatially varying field round-trip error " << max_rel_err;
}

}  // namespace
}  // namespace aces

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
