/**
 * @file test_stream_config_resolution.cpp
 * @brief Tests that StreamConfig resolution matches the legacy inline parse.
 *
 * Feature: driver-io-regrid-perf, Property 4: StreamConfig resolution matches
 * the legacy inline parse
 *
 * CeceDriverOrchestrator::ResolveStreamConfigs() (delegating to the private
 * static ResolveStreamConfigsFromFile) must populate stream_configs_ with
 * EXACTLY the same per-stream fields the legacy inline AdvanceTime parse
 * produced for every model variable:
 *   input_file_path, input_var_name, mapalgo, cadence, tintalgo, data_model,
 *   data_model_explicit, amio_worker_threads, amio_staging_buffer_count.
 *
 * Two parts:
 *   1. Table-driven example tests over the checked-in example configs under
 *      examples/ (located via the CECE_SOURCE_DIR compile definition), plus a
 *      hand-written fixture written to a temp dir, comparing every resolved
 *      StreamConfig field against a local reference parser that reproduces the
 *      legacy inline field resolution from the same YAML.
 *   2. A RapidCheck property (>= 100 iters) over randomized stream
 *      configurations, asserting every resolved field equals the local
 *      reference (legacy) parser output.
 *
 * The local reference parser (LegacyResolve, below) reproduces, field for
 * field, the inline parse that AdvanceTime performed before task 8.1 factored
 * it into ResolveStreamConfigsFromFile:
 *   - Walk config["cece_data"]["streams"]; for each stream walk its
 *     "variables"; a scalar variable => model_name == file_name; a map with a
 *     "model" key => model_name from "model", file_name from "file" (defaulting
 *     to model_name). First stream/variable matching a model name wins.
 *   - input_file_path = stream["file"] if present else "" (missing => empty).
 *   - input_var_name = file_name.
 *   - mapalgo default "consd"; cadence default ""; tintalgo default "nearest".
 *   - data_model: lower-cased. "classic"/"enhanced" => explicit true;
 *     "auto" => "enhanced" non-explicit; anything else => "enhanced"
 *     non-explicit (with a warning, not asserted here).
 *   - driver.amio_worker_threads default 1 (validated >= 1);
 *     driver.amio_staging_buffer_count default 8 (validated >= 1).
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim.
//
// ResolveStreamConfigsFromFile is a private static member of
// CeceDriverOrchestrator. This struct is declared a friend inside the class
// (see include/cece/cece_driver_facade.hpp, Task 8.2) so the tests below can
// invoke the real production resolution helper directly against a config file
// path WITHOUT constructing a full orchestrator (which pulls in MPI/DAGR/
// CeceIO). This exercises the production code path, not a copy. It does not
// touch any production runtime behavior.
// ============================================================================
struct StreamConfigTestAccess {
    static void Resolve(const std::string& config_file, std::unordered_map<std::string, StreamConfig>& out_configs, std::string& out_gridspec_file) {
        CeceDriverOrchestrator::ResolveStreamConfigsFromFile(config_file, out_configs, out_gridspec_file);
    }
};

namespace {

// ----------------------------------------------------------------------------
// Local reference oracle: reproduces the legacy inline AdvanceTime parse.
// ----------------------------------------------------------------------------

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Resolve driver-level amio_worker_threads / amio_staging_buffer_count exactly
// as the legacy parse did (defaults 1 / 8, both validated >= 1). Returns false
// via `ok` if the config declares an out-of-range value (the production helper
// throws in that case, so callers can skip the comparison).
void LegacyDriverValues(const YAML::Node& config, int& worker_threads, int& staging_buffer_count, bool& ok) {
    worker_threads = 1;
    staging_buffer_count = 8;
    ok = true;
    if (config["driver"]) {
        if (config["driver"]["amio_worker_threads"]) {
            worker_threads = config["driver"]["amio_worker_threads"].as<int>();
            if (worker_threads < 1) ok = false;
        }
        if (config["driver"]["amio_staging_buffer_count"]) {
            staging_buffer_count = config["driver"]["amio_staging_buffer_count"].as<int>();
            if (staging_buffer_count < 1) ok = false;
        }
    }
}

// Build the reference map of model-variable-name -> StreamConfig by replaying
// the legacy inline field resolution over the same YAML. First-match-wins per
// model name (mirrors the legacy `break` and the helper's `emplace`).
std::unordered_map<std::string, StreamConfig> LegacyResolve(const YAML::Node& config) {
    std::unordered_map<std::string, StreamConfig> out;

    int worker_threads = 1;
    int staging_buffer_count = 8;
    bool driver_ok = true;
    LegacyDriverValues(config, worker_threads, staging_buffer_count, driver_ok);

    if (!config["cece_data"] || !config["cece_data"]["streams"]) return out;

    for (const auto& stream : config["cece_data"]["streams"]) {
        for (const auto& var : stream["variables"]) {
            std::string model_name;
            std::string file_name;
            if (var.IsScalar()) {
                model_name = var.as<std::string>();
                file_name = model_name;
            } else if (var.IsMap() && var["model"]) {
                model_name = var["model"].as<std::string>();
                file_name = var["file"] ? var["file"].as<std::string>() : model_name;
            } else {
                continue;
            }

            StreamConfig cfg;
            if (stream["file"]) {
                cfg.input_file_path = stream["file"].as<std::string>();
            }
            cfg.input_var_name = file_name;
            if (stream["mapalgo"]) {
                cfg.mapalgo = stream["mapalgo"].as<std::string>();
            }
            if (stream["cadence"]) {
                cfg.cadence = stream["cadence"].as<std::string>();
            }
            if (stream["tintalgo"]) {
                cfg.tintalgo = stream["tintalgo"].as<std::string>();
            }
            if (stream["data_model"]) {
                std::string requested = ToLower(stream["data_model"].as<std::string>());
                if (requested == "classic" || requested == "enhanced") {
                    cfg.data_model = requested;
                    cfg.data_model_explicit = true;
                } else if (requested == "auto") {
                    cfg.data_model = "enhanced";
                    cfg.data_model_explicit = false;
                } else {
                    cfg.data_model = "enhanced";
                    cfg.data_model_explicit = false;
                }
            }
            cfg.amio_worker_threads = worker_threads;
            cfg.amio_staging_buffer_count = staging_buffer_count;

            // First-match-wins per model name.
            out.emplace(model_name, std::move(cfg));
        }
    }

    return out;
}

// Compare every field the property covers, with descriptive messages.
void ExpectStreamConfigEq(const std::string& var_name, const StreamConfig& actual, const StreamConfig& expected) {
    EXPECT_EQ(actual.input_file_path, expected.input_file_path) << "input_file_path for '" << var_name << "'";
    EXPECT_EQ(actual.input_var_name, expected.input_var_name) << "input_var_name for '" << var_name << "'";
    EXPECT_EQ(actual.mapalgo, expected.mapalgo) << "mapalgo for '" << var_name << "'";
    EXPECT_EQ(actual.cadence, expected.cadence) << "cadence for '" << var_name << "'";
    EXPECT_EQ(actual.tintalgo, expected.tintalgo) << "tintalgo for '" << var_name << "'";
    EXPECT_EQ(actual.data_model, expected.data_model) << "data_model for '" << var_name << "'";
    EXPECT_EQ(actual.data_model_explicit, expected.data_model_explicit) << "data_model_explicit for '" << var_name << "'";
    EXPECT_EQ(actual.amio_worker_threads, expected.amio_worker_threads) << "amio_worker_threads for '" << var_name << "'";
    EXPECT_EQ(actual.amio_staging_buffer_count, expected.amio_staging_buffer_count) << "amio_staging_buffer_count for '" << var_name << "'";
}

// RapidCheck flavour of the field comparison (RC_ASSERT so failures shrink).
void RcAssertStreamConfigEq(const StreamConfig& actual, const StreamConfig& expected) {
    RC_ASSERT(actual.input_file_path == expected.input_file_path);
    RC_ASSERT(actual.input_var_name == expected.input_var_name);
    RC_ASSERT(actual.mapalgo == expected.mapalgo);
    RC_ASSERT(actual.cadence == expected.cadence);
    RC_ASSERT(actual.tintalgo == expected.tintalgo);
    RC_ASSERT(actual.data_model == expected.data_model);
    RC_ASSERT(actual.data_model_explicit == expected.data_model_explicit);
    RC_ASSERT(actual.amio_worker_threads == expected.amio_worker_threads);
    RC_ASSERT(actual.amio_staging_buffer_count == expected.amio_staging_buffer_count);
}

// RAII temp file for fixtures / generated configs.
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& content) {
        path = "/tmp/cece_stream_cfg_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".yaml";
        std::ofstream ofs(path);
        ofs << content;
        ofs.close();
    }
    ~TempFile() {
        std::remove(path.c_str());
    }
};

// Drive both the production helper and the legacy oracle from the same file and
// assert exact field-for-field agreement over the union of resolved variables.
void CompareResolutionAgainstLegacy(const std::string& config_path) {
    std::unordered_map<std::string, StreamConfig> actual;
    std::string actual_gridspec;
    StreamConfigTestAccess::Resolve(config_path, actual, actual_gridspec);

    YAML::Node config = YAML::LoadFile(config_path);
    std::unordered_map<std::string, StreamConfig> expected = LegacyResolve(config);

    // Same set of resolved model variables.
    ASSERT_EQ(actual.size(), expected.size()) << "resolved variable count differs for " << config_path;

    for (const auto& [var_name, exp_cfg] : expected) {
        auto it = actual.find(var_name);
        ASSERT_NE(it, actual.end()) << "production resolution missing variable '" << var_name << "' from " << config_path;
        ExpectStreamConfigEq(var_name, it->second, exp_cfg);
    }
}

// The example configs shipped in examples/ (all contain a cece_data.streams
// block, per the checked-in files).
const std::vector<std::string>& ExampleConfigNames() {
    static const std::vector<std::string> kNames = {
        "cece_config_ex1.yaml", "cece_config_ex2.yaml", "cece_config_ex3.yaml", "cece_config_ex4.yaml", "cece_config_ex5.yaml",
        "cece_config_ex6.yaml", "cece_config_ex7.yaml", "cece_config_ex8.yaml", "cece_config_ex9.yaml",
    };
    return kNames;
}

}  // namespace

// ============================================================================
// Part 1a: Table-driven example tests over examples/cece_config_ex*.yaml.
// Feature: driver-io-regrid-perf, Property 4: StreamConfig resolution matches
// the legacy inline parse
// **Validates: Requirements 1.1, 1.5, 4.3**
// ============================================================================
TEST(StreamConfigResolution, ExampleConfigsMatchLegacyParse) {
    const std::string src_dir = CECE_SOURCE_DIR;
    int examples_with_streams = 0;

    for (const auto& name : ExampleConfigNames()) {
        const std::string path = src_dir + "/examples/" + name;
        std::ifstream probe(path);
        if (!probe.good()) {
            // Example file not present in this checkout; skip rather than fail.
            continue;
        }
        probe.close();

        YAML::Node config = YAML::LoadFile(path);
        if (config["cece_data"] && config["cece_data"]["streams"]) {
            ++examples_with_streams;
        }

        SCOPED_TRACE("example config: " + name);
        CompareResolutionAgainstLegacy(path);
    }

    // At least one example config with a streams block must have been checked,
    // otherwise the hand-written fixture (below) is the only real coverage.
    EXPECT_GT(examples_with_streams, 0) << "no example configs with a cece_data.streams block were found";
}

// ============================================================================
// Part 1b: Hand-written fixture exercising defaults, explicit/auto/invalid
// data models, scalar vs map variables, present/absent file, and non-default
// driver-level worker/staging counts.
// Feature: driver-io-regrid-perf, Property 4: StreamConfig resolution matches
// the legacy inline parse
// **Validates: Requirements 1.1, 1.5, 4.3**
// ============================================================================
TEST(StreamConfigResolution, HandWrittenFixtureMatchesLegacyParse) {
    const std::string yaml =
        "driver:\n"
        "  gridspec_file: \"/tmp/grid.nc\"\n"
        "  amio_worker_threads: 4\n"
        "  amio_staging_buffer_count: 16\n"
        "cece_data:\n"
        "  streams:\n"
        // Stream 1: explicit data_model classic, map variable with file.
        "    - name: \"S_CLASSIC\"\n"
        "      file: \"/tmp/s_classic.nc\"\n"
        "      cadence: \"monthly\"\n"
        "      tintalgo: \"linear\"\n"
        "      mapalgo: \"bilinear\"\n"
        "      data_model: \"classic\"\n"
        "      variables:\n"
        "        - file: \"emi_no\"\n"
        "          model: \"VAR_CLASSIC\"\n"
        // Stream 2: data_model enhanced (explicit), scalar variable => model==file.
        "    - name: \"S_ENH\"\n"
        "      file: \"/tmp/s_enh.nc\"\n"
        "      data_model: \"ENHANCED\"\n"  // upper-case => lower-cased, explicit
        "      variables:\n"
        "        - \"VAR_SCALAR\"\n"
        // Stream 3: data_model auto => enhanced non-explicit; all other defaults.
        "    - name: \"S_AUTO\"\n"
        "      file: \"/tmp/s_auto.nc\"\n"
        "      data_model: \"auto\"\n"
        "      variables:\n"
        "        - file: \"in_auto\"\n"
        "          model: \"VAR_AUTO\"\n"
        // Stream 4: invalid data_model => enhanced non-explicit (with warning).
        "    - name: \"S_BAD\"\n"
        "      file: \"/tmp/s_bad.nc\"\n"
        "      data_model: \"garbage\"\n"
        "      variables:\n"
        "        - file: \"in_bad\"\n"
        "          model: \"VAR_BAD\"\n"
        // Stream 5: no file key => input_file_path empty; map variable no file
        //           key => file_name defaults to model name.
        "    - name: \"S_NOFILE\"\n"
        "      variables:\n"
        "        - model: \"VAR_NOFILE\"\n";

    TempFile fixture(yaml);
    CompareResolutionAgainstLegacy(fixture.path);

    // Also spot-check a couple of resolved fields directly so the fixture is
    // meaningful even if the oracle had a matching bug.
    std::unordered_map<std::string, StreamConfig> actual;
    std::string gridspec;
    StreamConfigTestAccess::Resolve(fixture.path, actual, gridspec);

    ASSERT_TRUE(actual.count("VAR_CLASSIC"));
    EXPECT_EQ(actual["VAR_CLASSIC"].data_model, "classic");
    EXPECT_TRUE(actual["VAR_CLASSIC"].data_model_explicit);
    EXPECT_EQ(actual["VAR_CLASSIC"].input_var_name, "emi_no");
    EXPECT_EQ(actual["VAR_CLASSIC"].mapalgo, "bilinear");
    EXPECT_EQ(actual["VAR_CLASSIC"].cadence, "monthly");
    EXPECT_EQ(actual["VAR_CLASSIC"].tintalgo, "linear");
    EXPECT_EQ(actual["VAR_CLASSIC"].amio_worker_threads, 4);
    EXPECT_EQ(actual["VAR_CLASSIC"].amio_staging_buffer_count, 16);

    ASSERT_TRUE(actual.count("VAR_SCALAR"));
    EXPECT_EQ(actual["VAR_SCALAR"].data_model, "enhanced");
    EXPECT_TRUE(actual["VAR_SCALAR"].data_model_explicit);
    EXPECT_EQ(actual["VAR_SCALAR"].input_var_name, "VAR_SCALAR");  // scalar => file==model
    EXPECT_EQ(actual["VAR_SCALAR"].mapalgo, "consd");              // default
    EXPECT_EQ(actual["VAR_SCALAR"].tintalgo, "nearest");           // default
    EXPECT_EQ(actual["VAR_SCALAR"].cadence, "");                   // default

    ASSERT_TRUE(actual.count("VAR_AUTO"));
    EXPECT_EQ(actual["VAR_AUTO"].data_model, "enhanced");
    EXPECT_FALSE(actual["VAR_AUTO"].data_model_explicit);

    ASSERT_TRUE(actual.count("VAR_BAD"));
    EXPECT_EQ(actual["VAR_BAD"].data_model, "enhanced");
    EXPECT_FALSE(actual["VAR_BAD"].data_model_explicit);

    ASSERT_TRUE(actual.count("VAR_NOFILE"));
    EXPECT_EQ(actual["VAR_NOFILE"].input_file_path, "");           // missing file => empty
    EXPECT_EQ(actual["VAR_NOFILE"].input_var_name, "VAR_NOFILE");  // map w/o file => model
}

// ============================================================================
// Part 2: RapidCheck property over generated stream configurations.
// Feature: driver-io-regrid-perf, Property 4: StreamConfig resolution matches
// the legacy inline parse
// **Validates: Requirements 1.1, 1.5, 4.3**
//
// For any randomly generated CECE stream configuration, ResolveStreamConfigs
// (via the production ResolveStreamConfigsFromFile) resolves every field
// exactly as the legacy inline parse (reproduced by LegacyResolve).
// ============================================================================
RC_GTEST_PROP(StreamConfigResolutionProperty, Property4_ResolutionMatchesLegacy, ()) {
    // Generate 1-6 streams. Each stream has one variable with a unique model
    // name (unique so first-match-wins is deterministic and both parsers agree).
    const int num_streams = 1 + *rc::gen::inRange(0, 6);

    // Driver-level values: default (absent), or an explicit in-range value.
    const bool set_workers = *rc::gen::arbitrary<bool>();
    const int workers = set_workers ? (1 + *rc::gen::inRange(0, 32)) : 1;  // always >= 1
    const bool set_staging = *rc::gen::arbitrary<bool>();
    const int staging = set_staging ? (1 + *rc::gen::inRange(0, 64)) : 8;  // always >= 1

    static const std::vector<std::string> kMapAlgos = {"consd", "bilinear", "patch", "nearestdtos"};
    static const std::vector<std::string> kCadences = {"", "hourly", "weekly", "monthly"};
    static const std::vector<std::string> kTintAlgos = {"nearest", "linear"};
    // Mix of explicit, auto, invalid, and mixed-case models.
    static const std::vector<std::string> kDataModels = {"classic", "enhanced", "CLASSIC", "Enhanced", "auto", "garbage", "xyz", ""};

    std::string yaml;
    yaml += "driver:\n";
    if (set_workers) yaml += "  amio_worker_threads: " + std::to_string(workers) + "\n";
    if (set_staging) yaml += "  amio_staging_buffer_count: " + std::to_string(staging) + "\n";
    // Ensure the driver block is non-empty even when neither is set.
    if (!set_workers && !set_staging) yaml += "  gridspec_file: \"/tmp/g.nc\"\n";

    yaml += "cece_data:\n";
    yaml += "  streams:\n";

    for (int s = 0; s < num_streams; ++s) {
        const bool has_file = *rc::gen::arbitrary<bool>();
        const bool set_mapalgo = *rc::gen::arbitrary<bool>();
        const bool set_cadence = *rc::gen::arbitrary<bool>();
        const bool set_tintalgo = *rc::gen::arbitrary<bool>();
        const bool set_data_model = *rc::gen::arbitrary<bool>();
        const bool scalar_var = *rc::gen::arbitrary<bool>();

        const std::string map_algo = *rc::gen::elementOf(kMapAlgos);
        const std::string cadence = *rc::gen::elementOf(kCadences);
        const std::string tint = *rc::gen::elementOf(kTintAlgos);
        const std::string data_model = *rc::gen::elementOf(kDataModels);

        const std::string model_name = "MODEL_" + std::to_string(s);
        const std::string file_var = "FILEVAR_" + std::to_string(s);

        yaml += "    - name: \"STREAM_" + std::to_string(s) + "\"\n";
        if (has_file) yaml += "      file: \"/tmp/stream_" + std::to_string(s) + ".nc\"\n";
        if (set_mapalgo) yaml += "      mapalgo: \"" + map_algo + "\"\n";
        if (set_cadence && !cadence.empty()) yaml += "      cadence: \"" + cadence + "\"\n";
        if (set_tintalgo) yaml += "      tintalgo: \"" + tint + "\"\n";
        if (set_data_model && !data_model.empty()) yaml += "      data_model: \"" + data_model + "\"\n";
        yaml += "      variables:\n";
        if (scalar_var) {
            yaml += "        - \"" + model_name + "\"\n";
        } else {
            // Map variable, with a distinct file var name so we can verify the
            // file-var-name fallback path.
            yaml += "        - file: \"" + file_var + "\"\n";
            yaml += "          model: \"" + model_name + "\"\n";
        }
    }

    TempFile fixture(yaml);

    std::unordered_map<std::string, StreamConfig> actual;
    std::string gridspec;
    StreamConfigTestAccess::Resolve(fixture.path, actual, gridspec);

    YAML::Node config = YAML::LoadFile(fixture.path);
    std::unordered_map<std::string, StreamConfig> expected = LegacyResolve(config);

    RC_ASSERT(actual.size() == expected.size());
    for (const auto& [var_name, exp_cfg] : expected) {
        auto it = actual.find(var_name);
        RC_ASSERT(it != actual.end());
        RcAssertStreamConfigEq(it->second, exp_cfg);
    }
}

}  // namespace cece
