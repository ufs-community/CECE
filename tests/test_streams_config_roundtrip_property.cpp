/**
 * @file test_streams_config_roundtrip_property.cpp
 * @brief Property 16: Streams Configuration Round-Trip
 *
 * Validates: Requirements 7.13
 *
 * FOR ALL valid Streams_File configurations, parsing → serializing → parsing
 * SHALL produce an equivalent configuration with identical stream names,
 * file paths, variables, and interpolation settings.
 *
 * Runs 100+ iterations with randomly generated valid configurations.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "aces/aces_cdeps_parser.hpp"

namespace aces {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

static bool ConfigsEquivalent(const AcesCdepsConfig& a, const AcesCdepsConfig& b) {
    if (a.streams.size() != b.streams.size()) return false;
    for (size_t i = 0; i < a.streams.size(); ++i) {
        const auto& sa = a.streams[i];
        const auto& sb = b.streams[i];
        if (sa.name != sb.name) return false;
        if (sa.file_paths != sb.file_paths) return false;
        if (sa.taxmode != sb.taxmode) return false;
        if (sa.tintalgo != sb.tintalgo) return false;
        if (sa.mapalgo != sb.mapalgo) return false;
        if (sa.yearFirst != sb.yearFirst) return false;
        if (sa.yearLast != sb.yearLast) return false;
        if (sa.yearAlign != sb.yearAlign) return false;
        if (sa.offset != sb.offset) return false;
        if (sa.variables.size() != sb.variables.size()) return false;
        for (size_t j = 0; j < sa.variables.size(); ++j) {
            if (sa.variables[j].name_in_file != sb.variables[j].name_in_file) return false;
            if (sa.variables[j].name_in_model != sb.variables[j].name_in_model) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class StreamsConfigRoundTripPropertyTest : public ::testing::Test {
   protected:
    std::mt19937 rng_{42};

    void TearDown() override {
        for (const auto& f : tmp_files_) std::remove(f.c_str());
    }

    std::string TmpFile(const std::string& suffix) {
        std::string name = "streams_rt_" + suffix + ".txt";
        tmp_files_.push_back(name);
        return name;
    }

    /** Pick a random element from a vector. */
    template <typename T>
    const T& Pick(const std::vector<T>& v) {
        std::uniform_int_distribution<int> d(0, v.size() - 1);
        return v[d(rng_)];
    }

    /** Generate a random valid streams file and return its path. */
    std::string GenerateValidStreamsFile(const std::string& tag, int n_streams) {
        const std::vector<std::string> taxmodes = {"cycle", "extend", "limit"};
        const std::vector<std::string> tintalgo = {"none", "linear", "nearest"};
        const std::vector<std::string> mapalgo  = {"bilinear", "patch", "nearestdtos"};

        std::string content;
        for (int s = 0; s < n_streams; ++s) {
            std::string sname = "stream_" + tag + "_" + std::to_string(s);
            content += "stream::" + sname + "\n";
            content += "  file_paths = /data/" + sname + ".nc\n";

            // 1-3 variables
            std::uniform_int_distribution<int> nv(1, 3);
            int nvars = nv(rng_);
            std::string vars;
            for (int v = 0; v < nvars; ++v) {
                if (v > 0) vars += ", ";
                vars += "VAR" + std::to_string(v) + ":model_var" + std::to_string(v);
            }
            content += "  variables = " + vars + "\n";
            content += "  taxmode = " + Pick(taxmodes) + "\n";
            content += "  tintalgo = " + Pick(tintalgo) + "\n";
            content += "  mapalgo = " + Pick(mapalgo) + "\n";

            std::uniform_int_distribution<int> year(2000, 2023);
            int y1 = year(rng_);
            int y2 = year(rng_);
            if (y1 > y2) std::swap(y1, y2);
            content += "  yearFirst = " + std::to_string(y1) + "\n";
            content += "  yearLast = " + std::to_string(y2) + "\n";
            content += "  yearAlign = " + std::to_string(y1) + "\n";

            std::uniform_int_distribution<int> off(0, 7200);
            content += "  offset = " + std::to_string(off(rng_)) + "\n";
            content += "::\n\n";
        }

        std::string path = TmpFile(tag);
        WriteFile(path, content);
        return path;
    }

   private:
    std::vector<std::string> tmp_files_;
};

// ---------------------------------------------------------------------------
// Property 16 tests
// ---------------------------------------------------------------------------

/**
 * @test Property 16.1: Single-stream round-trip (50 iterations).
 *
 * FOR ALL single-stream configurations, parse → write → parse SHALL produce
 * an equivalent configuration.
 */
TEST_F(StreamsConfigRoundTripPropertyTest, SingleStreamRoundTrip) {
    for (int i = 0; i < 50; ++i) {
        std::string src = GenerateValidStreamsFile("s1_" + std::to_string(i), 1);
        std::string out = TmpFile("s1_out_" + std::to_string(i));

        AcesCdepsConfig config1 = CdepsStreamsParser::ParseStreamsFile(src);
        CdepsStreamsParser::WriteStreamsFile(out, config1);
        AcesCdepsConfig config2 = CdepsStreamsParser::ParseStreamsFile(out);

        EXPECT_TRUE(ConfigsEquivalent(config1, config2))
            << "Round-trip failed for single-stream iteration " << i
            << "\n  Stream name: " << config1.streams[0].name;
    }
}

/**
 * @test Property 16.2: Multi-stream round-trip (30 iterations).
 *
 * FOR ALL multi-stream configurations (2-5 streams), parse → write → parse
 * SHALL produce an equivalent configuration.
 */
TEST_F(StreamsConfigRoundTripPropertyTest, MultiStreamRoundTrip) {
    std::uniform_int_distribution<int> ns(2, 5);
    for (int i = 0; i < 30; ++i) {
        int n = ns(rng_);
        std::string src = GenerateValidStreamsFile("ms_" + std::to_string(i), n);
        std::string out = TmpFile("ms_out_" + std::to_string(i));

        AcesCdepsConfig config1 = CdepsStreamsParser::ParseStreamsFile(src);
        CdepsStreamsParser::WriteStreamsFile(out, config1);
        AcesCdepsConfig config2 = CdepsStreamsParser::ParseStreamsFile(out);

        EXPECT_EQ(config1.streams.size(), config2.streams.size())
            << "Stream count mismatch after round-trip, iteration " << i;
        EXPECT_TRUE(ConfigsEquivalent(config1, config2))
            << "Round-trip failed for multi-stream iteration " << i
            << " (" << n << " streams)";
    }
}

/**
 * @test Property 16.3: Stream names are preserved exactly.
 *
 * FOR ALL stream names, the round-trip SHALL preserve the exact name string.
 *
 * Iterations: 30
 */
TEST_F(StreamsConfigRoundTripPropertyTest, StreamNamesPreserved) {
    for (int i = 0; i < 30; ++i) {
        std::string src = GenerateValidStreamsFile("nm_" + std::to_string(i), 2);
        std::string out = TmpFile("nm_out_" + std::to_string(i));

        AcesCdepsConfig c1 = CdepsStreamsParser::ParseStreamsFile(src);
        CdepsStreamsParser::WriteStreamsFile(out, c1);
        AcesCdepsConfig c2 = CdepsStreamsParser::ParseStreamsFile(out);

        for (size_t s = 0; s < c1.streams.size(); ++s) {
            EXPECT_EQ(c1.streams[s].name, c2.streams[s].name)
                << "Stream name mismatch at index " << s << ", iteration " << i;
        }
    }
}

/**
 * @test Property 16.4: File paths are preserved exactly.
 *
 * FOR ALL file path lists, the round-trip SHALL preserve every path.
 *
 * Iterations: 30
 */
TEST_F(StreamsConfigRoundTripPropertyTest, FilePathsPreserved) {
    for (int i = 0; i < 30; ++i) {
        std::string src = GenerateValidStreamsFile("fp_" + std::to_string(i), 1);
        std::string out = TmpFile("fp_out_" + std::to_string(i));

        AcesCdepsConfig c1 = CdepsStreamsParser::ParseStreamsFile(src);
        CdepsStreamsParser::WriteStreamsFile(out, c1);
        AcesCdepsConfig c2 = CdepsStreamsParser::ParseStreamsFile(out);

        for (size_t s = 0; s < c1.streams.size(); ++s) {
            EXPECT_EQ(c1.streams[s].file_paths, c2.streams[s].file_paths)
                << "File paths mismatch at stream " << s << ", iteration " << i;
        }
    }
}

/**
 * @test Property 16.5: Variable mappings are preserved exactly.
 *
 * FOR ALL variable name mappings, the round-trip SHALL preserve both
 * name_in_file and name_in_model for every variable.
 *
 * Iterations: 30
 */
TEST_F(StreamsConfigRoundTripPropertyTest, VariableMappingsPreserved) {
    for (int i = 0; i < 30; ++i) {
        std::string src = GenerateValidStreamsFile("vm_" + std::to_string(i), 1);
        std::string out = TmpFile("vm_out_" + std::to_string(i));

        AcesCdepsConfig c1 = CdepsStreamsParser::ParseStreamsFile(src);
        CdepsStreamsParser::WriteStreamsFile(out, c1);
        AcesCdepsConfig c2 = CdepsStreamsParser::ParseStreamsFile(out);

        for (size_t s = 0; s < c1.streams.size(); ++s) {
            ASSERT_EQ(c1.streams[s].variables.size(), c2.streams[s].variables.size())
                << "Variable count mismatch at stream " << s;
            for (size_t v = 0; v < c1.streams[s].variables.size(); ++v) {
                EXPECT_EQ(c1.streams[s].variables[v].name_in_file,
                          c2.streams[s].variables[v].name_in_file)
                    << "name_in_file mismatch at stream " << s << " var " << v;
                EXPECT_EQ(c1.streams[s].variables[v].name_in_model,
                          c2.streams[s].variables[v].name_in_model)
                    << "name_in_model mismatch at stream " << s << " var " << v;
            }
        }
    }
}

/**
 * @test Property 16.6: Temporal settings are preserved exactly.
 *
 * FOR ALL taxmode/tintalgo/year settings, the round-trip SHALL preserve them.
 *
 * Iterations: 30
 */
TEST_F(StreamsConfigRoundTripPropertyTest, TemporalSettingsPreserved) {
    for (int i = 0; i < 30; ++i) {
        std::string src = GenerateValidStreamsFile("ts_" + std::to_string(i), 1);
        std::string out = TmpFile("ts_out_" + std::to_string(i));

        AcesCdepsConfig c1 = CdepsStreamsParser::ParseStreamsFile(src);
        CdepsStreamsParser::WriteStreamsFile(out, c1);
        AcesCdepsConfig c2 = CdepsStreamsParser::ParseStreamsFile(out);

        for (size_t s = 0; s < c1.streams.size(); ++s) {
            EXPECT_EQ(c1.streams[s].taxmode,    c2.streams[s].taxmode)    << "taxmode mismatch";
            EXPECT_EQ(c1.streams[s].tintalgo,   c2.streams[s].tintalgo)   << "tintalgo mismatch";
            EXPECT_EQ(c1.streams[s].mapalgo,    c2.streams[s].mapalgo)    << "mapalgo mismatch";
            EXPECT_EQ(c1.streams[s].yearFirst,  c2.streams[s].yearFirst)  << "yearFirst mismatch";
            EXPECT_EQ(c1.streams[s].yearLast,   c2.streams[s].yearLast)   << "yearLast mismatch";
            EXPECT_EQ(c1.streams[s].yearAlign,  c2.streams[s].yearAlign)  << "yearAlign mismatch";
            EXPECT_EQ(c1.streams[s].offset,     c2.streams[s].offset)     << "offset mismatch";
        }
    }
}

/**
 * @test Property 16.7: Triple round-trip is stable (parse→write→parse→write→parse).
 *
 * FOR ALL configurations, applying the round-trip twice SHALL produce the
 * same result as applying it once (idempotence of serialization).
 *
 * Iterations: 20
 */
TEST_F(StreamsConfigRoundTripPropertyTest, TripleRoundTripStable) {
    for (int i = 0; i < 20; ++i) {
        std::string src  = GenerateValidStreamsFile("tr_" + std::to_string(i), 2);
        std::string out1 = TmpFile("tr_out1_" + std::to_string(i));
        std::string out2 = TmpFile("tr_out2_" + std::to_string(i));

        AcesCdepsConfig c1 = CdepsStreamsParser::ParseStreamsFile(src);
        CdepsStreamsParser::WriteStreamsFile(out1, c1);
        AcesCdepsConfig c2 = CdepsStreamsParser::ParseStreamsFile(out1);
        CdepsStreamsParser::WriteStreamsFile(out2, c2);
        AcesCdepsConfig c3 = CdepsStreamsParser::ParseStreamsFile(out2);

        EXPECT_TRUE(ConfigsEquivalent(c2, c3))
            << "Triple round-trip not stable at iteration " << i;
    }
}

}  // namespace
}  // namespace aces
