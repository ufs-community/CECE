/**
 * @file test_stream_key_properties.cpp
 * @brief Property-based tests for CeceDriverOrchestrator::StreamKey.
 *
 * Feature: regrid-per-stream, Property 1: StreamKey concatenation correctness
 *
 * NOTE (Amendment 1, task 8.1): StreamKey was REDEFINED. It no longer returns
 * cfg.input_file_path + "|" + cfg.mapalgo. It now returns
 * HandleKey(cfg) + "|" + cfg.mapalgo, where
 * HandleKey(cfg) = input_file_path + "|" + data_model + "|" + worker_threads +
 * "|" + staging_buffer_count + "|" + staging_buffer_capacity_bytes + "|" +
 * prefetch_depth (the capacity/depth fields were added with the AMIO
 * lazy-allocation staging-pool fix so the key tracks every field
 * BuildManifestContent consumes). So the full StreamKey concatenation is:
 *   input_file_path + "|" + data_model + "|" +
 *   to_string(amio_worker_threads) + "|" +
 *   to_string(amio_staging_buffer_count) + "|" +
 *   to_string(amio_staging_buffer_capacity_bytes) + "|" +
 *   to_string(amio_prefetch_depth) + "|" + mapalgo.
 * StreamKey remains the Stream_Identity_Key that keys the regrid_plans_ cache;
 * under Amendment 1 amio_handles_ / file_nt_cache_ are keyed by the coarser
 * HandleKey instead. These tests are updated to the extended-key contract.
 *
 * Properties tested (under the extended key):
 *   - Concatenation: StreamKey(cfg) == input_file_path + "|" + data_model +
 *     "|" + worker_threads + "|" + staging_buffer_count + "|" +
 *     staging_buffer_capacity_bytes + "|" + prefetch_depth + "|" + mapalgo.
 *   - Determinism / equality: two configs matching on the full seven-field key
 *     tuple {input_file_path, data_model, amio_worker_threads,
 *     amio_staging_buffer_count, amio_staging_buffer_capacity_bytes,
 *     amio_prefetch_depth, mapalgo} produce the same key regardless of
 *     any other field.
 *   - Injectivity on the key tuple: two configs differing in any of those seven
 *     fields produce different keys.
 *
 * **Validates: Requirements 1.1, 1.2, 1.3, 8.1, 9.4** (under the Amendment 1
 * extended key redefinition)
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <string>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim.
//
// StreamKey is a private static member of CeceDriverOrchestrator. This struct
// is declared a friend inside the class (see
// include/cece/cece_driver_facade.hpp, Task 6.1) so the tests below can invoke
// the private static helper without altering its signature, logic, or
// visibility. It does not touch any production code path.
// ============================================================================
struct StreamKeyTestAccess {
    static std::string Key(const StreamConfig& cfg) {
        return CeceDriverOrchestrator::StreamKey(cfg);
    }
};

namespace {

// Generate an arbitrary StreamConfig. Every field is populated with arbitrary
// (but constrained-to-sane) values so the tests exercise the key derivation
// against noise in the non-key fields. input_file_path and mapalgo are drawn
// from a broad space (including empty strings and characters such as '|' and
// path separators) to stress the concatenation formula.
rc::Gen<StreamConfig> genStreamConfig() {
    return rc::gen::apply(
        [](std::string input_file_path, std::string input_var_name, std::string mapalgo, std::string cadence, std::string tintalgo,
           std::string data_model, bool data_model_explicit, int amio_worker_threads, int amio_staging_buffer_count,
           int amio_staging_buffer_capacity_bytes, int amio_prefetch_depth) {
            StreamConfig cfg;
            cfg.input_file_path = std::move(input_file_path);
            cfg.input_var_name = std::move(input_var_name);
            cfg.mapalgo = std::move(mapalgo);
            cfg.cadence = std::move(cadence);
            cfg.tintalgo = std::move(tintalgo);
            cfg.data_model = std::move(data_model);
            cfg.data_model_explicit = data_model_explicit;
            cfg.amio_worker_threads = amio_worker_threads;
            cfg.amio_staging_buffer_count = amio_staging_buffer_count;
            cfg.amio_staging_buffer_capacity_bytes = amio_staging_buffer_capacity_bytes;
            cfg.amio_prefetch_depth = amio_prefetch_depth;
            return cfg;
        },
        rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(),
        rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<bool>(), rc::gen::inRange(1, 65),
        rc::gen::inRange(1, 65), rc::gen::inRange(1, 65), rc::gen::inRange(1, 65));
}

}  // namespace

// ============================================================================
// Property 1a: Concatenation correctness (Amendment 1 extended key)
// Feature: regrid-per-stream, Property 1: StreamKey concatenation correctness
// **Validates: Requirements 1.1, 9.4**
//
// For any StreamConfig, StreamKey(cfg) equals exactly the seven-field
// concatenation HandleKey(cfg) + "|" + cfg.mapalgo, i.e.
//   input_file_path + "|" + data_model + "|" + to_string(worker_threads) +
//   "|" + to_string(staging_buffer_count) + "|"
//   + to_string(staging_buffer_capacity_bytes) + "|"
//   + to_string(prefetch_depth) + "|" + mapalgo.
// The full concatenation is inlined here to keep this file self-contained.
// ============================================================================
RC_GTEST_PROP(StreamKeyProperty, Property1_Concatenation, ()) {
    const StreamConfig cfg = *genStreamConfig();
    const std::string expected = cfg.input_file_path + "|" + cfg.data_model + "|" + std::to_string(cfg.amio_worker_threads) + "|" +
                                 std::to_string(cfg.amio_staging_buffer_count) + "|" + std::to_string(cfg.amio_staging_buffer_capacity_bytes) + "|" +
                                 std::to_string(cfg.amio_prefetch_depth) + "|" + cfg.mapalgo;
    RC_ASSERT(StreamKeyTestAccess::Key(cfg) == expected);
}

// ============================================================================
// Property 1b: Determinism / equality when the full key tuple matches
// Feature: regrid-per-stream, Property 1: StreamKey concatenation correctness
// **Validates: Requirements 1.2, 8.1**
//
// Under the Amendment 1 extended key, StreamKey depends on the seven-field
// tuple {input_file_path, data_model, amio_worker_threads,
// amio_staging_buffer_count, amio_staging_buffer_capacity_bytes,
// amio_prefetch_depth, mapalgo}. Two configs matching on ALL seven
// produce the same key, regardless of any other field.
// ============================================================================
RC_GTEST_PROP(StreamKeyProperty, Property1_EqualWhenPathAndAlgoMatch, ()) {
    const StreamConfig a = *genStreamConfig();

    // b copies a's full key tuple but varies everything else arbitrarily.
    StreamConfig b = *genStreamConfig();
    b.input_file_path = a.input_file_path;
    b.data_model = a.data_model;
    b.amio_worker_threads = a.amio_worker_threads;
    b.amio_staging_buffer_count = a.amio_staging_buffer_count;
    b.amio_staging_buffer_capacity_bytes = a.amio_staging_buffer_capacity_bytes;
    b.amio_prefetch_depth = a.amio_prefetch_depth;
    b.mapalgo = a.mapalgo;

    RC_ASSERT(StreamKeyTestAccess::Key(a) == StreamKeyTestAccess::Key(b));
}

// ============================================================================
// Property 1c: Determinism on identical configs
// Feature: regrid-per-stream, Property 1: StreamKey concatenation correctness
// **Validates: Requirements 8.1**
//
// StreamKey is a pure function: the same config produces the same key on
// repeated evaluation (models the every-rank-agrees invariant).
// ============================================================================
RC_GTEST_PROP(StreamKeyProperty, Property1_DeterministicOnIdenticalConfig, ()) {
    const StreamConfig a = *genStreamConfig();
    RC_ASSERT(StreamKeyTestAccess::Key(a) == StreamKeyTestAccess::Key(a));
}

// ============================================================================
// Property 1d: Inequality when the five-field key tuple differs
// Feature: regrid-per-stream, Property 1: StreamKey concatenation correctness
// **Validates: Requirements 1.3, 9.4**
//
// Under the Amendment 1 extended key, two configs differing in ANY of the seven
// key-tuple fields {input_file_path, data_model, amio_worker_threads,
// amio_staging_buffer_count, amio_staging_buffer_capacity_bytes,
// amio_prefetch_depth, mapalgo} produce different keys. Because "|"
// cannot appear in the string key fields here (we guard with RC_PRE that none
// of input_file_path, data_model, or mapalgo contains "|"; the integer fields
// stringify without "|"), the concatenation is unambiguous and the mapping
// (key tuple) -> key is injective.
// ============================================================================
RC_GTEST_PROP(StreamKeyProperty, Property1_UnequalWhenPathOrAlgoDiffers, ()) {
    StreamConfig a = *genStreamConfig();
    StreamConfig b = *genStreamConfig();

    // The separator ban only needs to hold to keep concatenation unambiguous.
    // Discard cases where a string key field contains the reserved separator.
    RC_PRE(a.input_file_path.find('|') == std::string::npos);
    RC_PRE(a.data_model.find('|') == std::string::npos);
    RC_PRE(a.mapalgo.find('|') == std::string::npos);
    RC_PRE(b.input_file_path.find('|') == std::string::npos);
    RC_PRE(b.data_model.find('|') == std::string::npos);
    RC_PRE(b.mapalgo.find('|') == std::string::npos);

    // Only meaningful when the seven-field key tuple actually differs.
    RC_PRE(a.input_file_path != b.input_file_path || a.data_model != b.data_model || a.amio_worker_threads != b.amio_worker_threads ||
           a.amio_staging_buffer_count != b.amio_staging_buffer_count ||
           a.amio_staging_buffer_capacity_bytes != b.amio_staging_buffer_capacity_bytes || a.amio_prefetch_depth != b.amio_prefetch_depth ||
           a.mapalgo != b.mapalgo);

    RC_ASSERT(StreamKeyTestAccess::Key(a) != StreamKeyTestAccess::Key(b));
}

}  // namespace cece
