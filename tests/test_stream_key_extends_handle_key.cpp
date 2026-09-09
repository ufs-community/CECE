/**
 * @file test_stream_key_extends_handle_key.cpp
 * @brief Property-based tests for the StreamKey/HandleKey two-level keying.
 *
 * Feature: regrid-per-stream, Property 5: StreamKey extends HandleKey
 * (shared-handle, split-plan)
 *
 * Amendment 1 introduces a coarser HandleKey for the file-scoped caches
 * (amio_handles_, file_nt_cache_), while StreamKey is redefined as
 * HandleKey(cfg) + "|" + cfg.mapalgo and keys the regrid plan cache. The
 * consequence is that two variables reading the same file/manifest share one
 * open handle set and one record count even when they request a different
 * mapalgo, but they still get separate regrid plans. HandleKey tracks every
 * field BuildManifestContent consumes (with the AMIO lazy-allocation fix that
 * is input_file_path + data_model + worker_threads + staging_buffer_count +
 * staging_buffer_capacity_bytes + prefetch_depth).
 *
 * Properties tested:
 *   1. Vary ONLY mapalgo: HandleKey is shared (-> shared handle + record count)
 *      but StreamKey differs (-> separate plans).
 *   2. Vary a manifest field (input_file_path / data_model /
 *      amio_worker_threads / amio_staging_buffer_count /
 *      amio_staging_buffer_capacity_bytes / amio_prefetch_depth) so the
 *      manifest tuple differs: BOTH HandleKey and StreamKey differ.
 *   3. Structural: StreamKey(cfg) == HandleKey(cfg) + "|" + cfg.mapalgo.
 *
 * **Validates: Requirements 11.4, 11.5, 11.6**
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <string>

#include "cece/cece_driver_facade.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim (self-contained to this translation unit).
//
// Both StreamKey and HandleKey are PRIVATE static members of
// CeceDriverOrchestrator. The class declares `friend struct
// StreamKeyExtendsAccess;` so this Property 5 test can invoke both private
// statics without altering their signature, logic, or visibility. It touches
// no production code path.
//
// This shim is deliberately given a name DISTINCT from the shims used by the
// sibling Amendment 1 tests (StreamKeyTestAccess in
// tests/test_stream_key_properties.cpp, HandleKeyTestAccess in the parallel
// task 11.1 test tests/test_handle_key_properties.cpp). Each test is compiled
// into its own executable, so this unique name guarantees the translation
// units never collide (no ODR / duplicate-symbol issue) even if they were
// ever linked together.
// ============================================================================
struct StreamKeyExtendsAccess {
    static std::string HKey(const cece::StreamConfig& cfg) { return CeceDriverOrchestrator::HandleKey(cfg); }
    static std::string SKey(const cece::StreamConfig& cfg) { return CeceDriverOrchestrator::StreamKey(cfg); }
};

namespace {

// Generate an arbitrary StreamConfig. Every field is populated with arbitrary
// (but constrained-to-sane) values so the tests exercise the key derivation
// against noise in the non-key fields. The manifest-relevant fields
// (input_file_path, data_model, amio_worker_threads, amio_staging_buffer_count,
// amio_staging_buffer_capacity_bytes, amio_prefetch_depth) and mapalgo are
// drawn from a broad space to stress the concatenation formulas. Mirrors the
// generator in tests/test_stream_key_properties.cpp.
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
        rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(),
        rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(),
        rc::gen::arbitrary<bool>(), rc::gen::inRange(1, 65), rc::gen::inRange(1, 65), rc::gen::inRange(1, 65),
        rc::gen::inRange(1, 65));
}

}  // namespace

// ============================================================================
// Property 5a: Vary ONLY mapalgo -> shared HandleKey, distinct StreamKey.
// Feature: regrid-per-stream, Property 5: StreamKey extends HandleKey
// (shared-handle, split-plan)
// **Validates: Requirements 11.4, 11.5**
//
// Two configs identical in every manifest field but differing in mapalgo
// produce the SAME HandleKey (so they share one open handle set and one file
// record count) and DIFFERENT StreamKeys (so their regrid plans split). The
// "|" separator ban keeps the StreamKey concatenation unambiguous.
// ============================================================================
RC_GTEST_PROP(StreamKeyExtends, Property5_MapalgoOnlySharesHandleSplitsStream, ()) {
    StreamConfig a = *genStreamConfig();
    StreamConfig b = a;  // identical in every field, including mapalgo

    // Give b a different mapalgo than a.
    b.mapalgo = *rc::gen::arbitrary<std::string>();

    // Guard: neither mapalgo may contain the reserved separator, so the
    // StreamKey concatenation (HandleKey + "|" + mapalgo) stays unambiguous.
    RC_PRE(a.mapalgo.find('|') == std::string::npos);
    RC_PRE(b.mapalgo.find('|') == std::string::npos);

    // Only meaningful when the two mapalgo values actually differ.
    RC_PRE(a.mapalgo != b.mapalgo);

    // Same manifest tuple -> same HandleKey (shared handle + record count).
    RC_ASSERT(StreamKeyExtendsAccess::HKey(a) == StreamKeyExtendsAccess::HKey(b));

    // Different mapalgo -> different StreamKey (separate regrid plans).
    RC_ASSERT(StreamKeyExtendsAccess::SKey(a) != StreamKeyExtendsAccess::SKey(b));
}

// ============================================================================
// Property 5b: Vary a manifest field -> BOTH HandleKey and StreamKey differ.
// Feature: regrid-per-stream, Property 5: StreamKey extends HandleKey
// (shared-handle, split-plan)
// **Validates: Requirements 11.6**
//
// When the manifest tuple (input_file_path, data_model, amio_worker_threads,
// amio_staging_buffer_count, amio_staging_buffer_capacity_bytes,
// amio_prefetch_depth) differs, both the coarser HandleKey and the finer
// StreamKey differ. The "|" separator ban on the string manifest fields keeps
// both concatenations unambiguous.
// ============================================================================
RC_GTEST_PROP(StreamKeyExtends, Property5_ManifestFieldDiffersBothKeysDiffer, ()) {
    StreamConfig a = *genStreamConfig();
    StreamConfig b = *genStreamConfig();

    // Keep mapalgo identical so any key difference is attributable to the
    // manifest tuple, not mapalgo.
    b.mapalgo = a.mapalgo;

    // Ban the reserved separator from the string manifest fields and mapalgo so
    // the six-field HandleKey concatenation is injective on the tuple.
    RC_PRE(a.input_file_path.find('|') == std::string::npos);
    RC_PRE(a.data_model.find('|') == std::string::npos);
    RC_PRE(a.mapalgo.find('|') == std::string::npos);
    RC_PRE(b.input_file_path.find('|') == std::string::npos);
    RC_PRE(b.data_model.find('|') == std::string::npos);

    // Only meaningful when the manifest tuple actually differs in some field.
    RC_PRE(a.input_file_path != b.input_file_path || a.data_model != b.data_model ||
           a.amio_worker_threads != b.amio_worker_threads ||
           a.amio_staging_buffer_count != b.amio_staging_buffer_count ||
           a.amio_staging_buffer_capacity_bytes != b.amio_staging_buffer_capacity_bytes ||
           a.amio_prefetch_depth != b.amio_prefetch_depth);

    // Manifest tuple differs -> HandleKey differs (separate handle + count).
    RC_ASSERT(StreamKeyExtendsAccess::HKey(a) != StreamKeyExtendsAccess::HKey(b));

    // ... and therefore StreamKey (HandleKey + "|" + mapalgo) differs too.
    RC_ASSERT(StreamKeyExtendsAccess::SKey(a) != StreamKeyExtendsAccess::SKey(b));
}

// ============================================================================
// Property 5c: Structural relationship StreamKey = HandleKey + "|" + mapalgo.
// Feature: regrid-per-stream, Property 5: StreamKey extends HandleKey
// (shared-handle, split-plan)
// **Validates: Requirements 11.4, 11.5**
//
// For any config, StreamKey is exactly HandleKey extended by "|" + mapalgo.
// This is the invariant that makes the shared-handle / split-plan behavior
// hold: everything is shared at the HandleKey level, and the plan splits only
// when mapalgo differs.
// ============================================================================
RC_GTEST_PROP(StreamKeyExtends, Property5_StreamKeyIsHandleKeyPlusMapalgo, ()) {
    const StreamConfig cfg = *genStreamConfig();
    const std::string expected = StreamKeyExtendsAccess::HKey(cfg) + "|" + cfg.mapalgo;
    RC_ASSERT(StreamKeyExtendsAccess::SKey(cfg) == expected);
}

}  // namespace cece
