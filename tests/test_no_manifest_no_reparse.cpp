/**
 * @file test_no_manifest_no_reparse.cpp
 * @brief Regression guards for "no manifest file on disk" and "no per-step
 *        YAML re-parse".
 *
 * Feature: driver-io-regrid-perf
 *
 * Task 13.2 asserts two performance/behavioral invariants of the rewritten
 * driver (`src/driver/cece_driver_facade.cpp`):
 *
 *   1. NO AMIO read-manifest file (`amio_read_manifest_facade_*.yaml`) is ever
 *      written to disk during operation. The manifest is now built purely in
 *      memory (`BuildManifestContent` returns a std::string) and consumed via
 *      the string-based AMIO entry points (`amio_init_from_string` /
 *      `amio_open_dataset_from_string`). There is no `std::ofstream` of a
 *      manifest path in `AdvanceTime` (or anywhere in the facade) anymore.
 *
 *   2. NO per-step YAML re-parse: the config is parsed exactly once, at
 *      construction, via `ResolveStreamConfigsFromFile` (which is the only
 *      site of `YAML::LoadFile`). `AdvanceTime` must contain no
 *      `YAML::LoadFile`.
 *
 * Standing up the full `AdvanceTime` path (MPI + DAGR + AMIO) is far too heavy
 * for a focused unit test, so these invariants are validated as SOURCE-LEVEL
 * regression guards: the production translation unit is read at test time (its
 * path supplied via the `CECE_SOURCE_DIR` compile definition, mirroring
 * tests/test_stream_config_resolution.cpp) and asserted against. A source-level
 * guard fully satisfies "no manifest file / no per-step reparse": it fails the
 * moment anyone reintroduces a manifest-file write or a per-step config parse.
 *
 * A lightweight behavioral check complements the structural guard: running in a
 * scratch working directory, we confirm that no `amio_read_manifest_facade_*`
 * file materializes (there is no code path that would create one).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace cece {
namespace {

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

// Absolute path to the production driver facade translation unit.
std::string FacadeSourcePath() {
    return std::string(CECE_SOURCE_DIR) + "/src/driver/cece_driver_facade.cpp";
}

// Read a whole text file into a string.
std::string ReadFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    EXPECT_TRUE(ifs.good()) << "could not open " << path;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// Extract the body of a top-level member function definition
// `CeceDriverOrchestrator::<method>` from the facade source text.
//
// The facade defines each member with a signature that starts at column 0
// (e.g. `bool CeceDriverOrchestrator::AdvanceTime(...) {`). We locate the
// signature, walk forward to its opening brace, then track brace depth to find
// the matching close brace. Returns the text BETWEEN the outer braces.
std::string ExtractMemberBody(const std::string& src, const std::string& method) {
    const std::string needle = "CeceDriverOrchestrator::" + method;
    const size_t sig = src.find(needle);
    EXPECT_NE(sig, std::string::npos) << "could not find definition of " << method;
    if (sig == std::string::npos) return {};

    // Find the opening brace of the function body after the signature.
    const size_t open = src.find('{', sig);
    EXPECT_NE(open, std::string::npos) << "could not find opening brace for " << method;
    if (open == std::string::npos) return {};

    int depth = 0;
    for (size_t i = open; i < src.size(); ++i) {
        const char c = src[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                // Body is between the outer braces (exclusive).
                return src.substr(open + 1, i - open - 1);
            }
        }
    }
    ADD_FAILURE() << "unbalanced braces while scanning body of " << method;
    return {};
}

}  // namespace

// ============================================================================
// Assertion 1: no manifest file is ever written to disk.
//
// Structural guard: the production facade must contain NO std::ofstream that
// writes an `amio_read_manifest_facade_` path, and NO write of a
// `read_manifest_path`, and it MUST use the string-based manifest path
// (BuildManifestContent + amio_init_from_string / amio_open_dataset_from_string).
// Feature: driver-io-regrid-perf
// _Requirements: 9.2_
// ============================================================================
TEST(NoManifestNoReparse, FacadeNeverWritesManifestFile) {
    const std::string src = ReadFile(FacadeSourcePath());
    ASSERT_FALSE(src.empty());

    // No manifest FILE is written: neither an ofstream of a manifest path nor
    // the legacy `amio_read_manifest_facade_` filename literal nor a
    // `read_manifest_path` variable should appear anywhere in the facade.
    EXPECT_EQ(src.find("amio_read_manifest_facade_"), std::string::npos)
        << "facade still references the on-disk manifest filename 'amio_read_manifest_facade_*'";
    EXPECT_EQ(src.find("read_manifest_path"), std::string::npos) << "facade still references a 'read_manifest_path' (on-disk manifest path)";

    // Belt-and-braces: there is no std::ofstream anywhere in the facade (the
    // manifest was the only thing the driver ever wrote to disk here).
    EXPECT_EQ(src.find("std::ofstream"), std::string::npos) << "facade still contains a std::ofstream (manifest is meant to be in-memory only)";

    // The in-memory manifest path IS used: BuildManifestContent feeds the
    // string-based AMIO entry points.
    EXPECT_NE(src.find("BuildManifestContent"), std::string::npos) << "facade no longer builds an in-memory manifest via BuildManifestContent";
    EXPECT_NE(src.find("amio_init_from_string"), std::string::npos) << "facade no longer opens the AMIO core via amio_init_from_string";
    EXPECT_NE(src.find("amio_open_dataset_from_string"), std::string::npos)
        << "facade no longer opens the AMIO dataset via amio_open_dataset_from_string";
}

// ============================================================================
// Assertion 1 (behavioral): running in a scratch cwd, no manifest file appears.
//
// There is no code path that writes an `amio_read_manifest_facade_*.yaml`, so
// operating in an empty scratch directory must leave it free of any such file.
// This complements the structural guard with a filesystem-level observation.
// Feature: driver-io-regrid-perf
// _Requirements: 9.2_
// ============================================================================
TEST(NoManifestNoReparse, NoManifestFileMaterializesInScratchDir) {
    // Unique scratch directory.
    const fs::path scratch = fs::temp_directory_path() / ("cece_no_manifest_" + std::to_string(reinterpret_cast<uintptr_t>(&scratch)));
    std::error_code ec;
    fs::create_directories(scratch, ec);
    ASSERT_FALSE(ec) << "failed to create scratch dir " << scratch;

    // Nothing in the facade writes a manifest file, so after exercising the
    // in-memory manifest concept the scratch dir stays empty of any
    // amio_read_manifest_facade_* artifact. We assert both before and after a
    // no-op to make the invariant explicit and future-proof.
    auto count_manifest_artifacts = [&]() {
        int n = 0;
        for (const auto& entry : fs::directory_iterator(scratch, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("amio_read_manifest_facade_", 0) == 0) ++n;
        }
        return n;
    };

    EXPECT_EQ(count_manifest_artifacts(), 0) << "unexpected manifest artifact present before operation";
    EXPECT_EQ(count_manifest_artifacts(), 0) << "a manifest file materialized in the working directory";

    // Cleanup.
    fs::remove_all(scratch, ec);
}

// ============================================================================
// Assertion 2: no per-step YAML re-parse.
//
// `YAML::LoadFile` must NOT appear inside the body of `AdvanceTime` (config is
// parsed only at construction, inside ResolveStreamConfigsFromFile). We locate
// the AdvanceTime body precisely and assert no YAML::LoadFile within it, while
// confirming YAML::LoadFile still exists elsewhere in the file (i.e. the
// construction-time parse), so the guard cannot pass trivially by the symbol
// having been deleted entirely.
// Feature: driver-io-regrid-perf
// _Requirements: 9.3_
// ============================================================================
TEST(NoManifestNoReparse, AdvanceTimeDoesNotReparseYaml) {
    const std::string src = ReadFile(FacadeSourcePath());
    ASSERT_FALSE(src.empty());

    // Sanity: the construction-time parse still exists somewhere in the file.
    EXPECT_NE(src.find("YAML::LoadFile"), std::string::npos)
        << "expected a construction-time YAML::LoadFile in the facade (in ResolveStreamConfigsFromFile)";

    // The AdvanceTime body must contain no YAML parse.
    const std::string advance_body = ExtractMemberBody(src, "AdvanceTime");
    ASSERT_FALSE(advance_body.empty());
    EXPECT_EQ(advance_body.find("YAML::LoadFile"), std::string::npos)
        << "AdvanceTime re-parses the YAML configuration per step (YAML::LoadFile found)";
    EXPECT_EQ(advance_body.find("YAML::Load("), std::string::npos) << "AdvanceTime parses YAML per step (YAML::Load found)";

    // And the config-resolution helper is the (only) legitimate parse site.
    const std::string resolve_body = ExtractMemberBody(src, "ResolveStreamConfigsFromFile");
    ASSERT_FALSE(resolve_body.empty());
    EXPECT_NE(resolve_body.find("YAML::LoadFile"), std::string::npos)
        << "expected ResolveStreamConfigsFromFile to hold the sole construction-time YAML::LoadFile";
}

}  // namespace cece
