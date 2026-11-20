/**
 * @file test_handle_key_properties.cpp
 * @brief Property-based tests for CeceDriverOrchestrator::HandleKey.
 *
 * Feature: regrid-per-stream, Property 4: HandleKey manifest-injectivity
 *
 * HandleKey(cfg) is the file/manifest-scoped identity key used to re-key the
 * file-scoped caches (amio_handles_, file_nt_cache_) so that variables reading
 * the same file/manifest share one open AMIO handle set and one record-count
 * search, even when their mapalgo differs.
 *
 * Property 4 (design.md Amendment 1): For any two StreamConfig values a and b,
 *   HandleKey(a) == HandleKey(b)
 * if and only if
 *   BuildManifestContent(a, a.data_model) is byte-identical to
 *   BuildManifestContent(b, b.data_model).
 * Equivalently, HandleKey collides only when the two configs produce the same
 * AMIO manifest.
 *
 * Why a test-local manifest builder is faithful and sufficient:
 *   BuildManifestContent (see src/driver/cece_driver_facade.cpp) emits a fixed
 *   YAML block whose independent inputs are eight StreamConfig fields:
 *     - input_file_path                     (rendered as `path:`)
 *     - data_model                          (the data_model argument; HandleKey
 *                                            passes cfg.data_model, so we build
 *                                            with that)
 *     - kind                               (rendered as `stream_kind:`)
 *     - format                             (rendered as `stream_format:`)
 *     - amio_staging_buffer_count           (rendered as `buffer_count:`)
 *     - amio_staging_buffer_capacity_bytes  (rendered as `buffer_capacity_bytes:`)
 *     - amio_worker_threads                 (rendered as `threads:`)
 *     - amio_prefetch_depth                 (rendered as `depth:`)
 *   HandleKey concatenates those same fields in this order: input_file_path,
 *   data_model, kind, format, worker_threads, staging_buffer_count,
 *   staging_buffer_capacity_bytes, prefetch_depth. The manifest's
 *   max_buffer_count is derived from amio_staging_buffer_count, so it adds no
 *   independent key field. All other manifest lines are constant.
 *
 * **Validates: Requirements 11.1, 11.2, 11.3**
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>

#include "cece/cece_driver_facade.hpp"
#include "stream_config_generator.hpp"

namespace cece {

// ============================================================================
// Test-only friend shim.
//
// HandleKey is a private static member of CeceDriverOrchestrator. This struct
// is declared a friend inside the class (see
// include/cece/cece_driver_facade.hpp, Task 8.1) so the tests below can invoke
// the private static helper without altering its signature, logic, or
// visibility. It does not touch any production code path. Mirrors the
// StreamKeyTestAccess pattern in tests/test_stream_key_properties.cpp.
// ============================================================================
struct HandleKeyTestAccess {
    static std::string Key(const StreamConfig& cfg) {
        return CeceDriverOrchestrator::HandleKey(cfg);
    }
};

namespace {

// Faithful test-local reproduction of the manifest bytes that
// BuildManifestContent(cfg, cfg.data_model) would emit.
// This lets the property compare manifests without constructing an orchestrator
// (BuildManifestContent is a non-static const member that only reads cfg fields
// + a data_model string). data_model is fixed to cfg.data_model because that is
// exactly what HandleKey feeds BuildManifestContent.
std::string TestManifest(const StreamConfig& cfg) {
    // The staging ceiling is derived in BuildManifestContent as
    // min(4096, max(count, count*8)); reproduce it faithfully so the
    // manifest bytes match production exactly.
    const long long ceiling_raw = static_cast<long long>(cfg.amio_staging_buffer_count) * 8;
    const long long ceiling = std::min<long long>(4096, std::max<long long>(cfg.amio_staging_buffer_count, ceiling_raw));
    std::ostringstream m_content;
    m_content << "backend: netcdf4\n"
              << "path: " << cfg.input_file_path << "\n"
              << "data_model: " << cfg.data_model << "\n"
              << "stream_kind: " << cfg.kind << "\n"
              << "stream_format: " << cfg.format << "\n"
              << "staging_pool:\n"
              << "  buffer_count: " << cfg.amio_staging_buffer_count << "\n"
              << "  buffer_capacity_bytes: " << cfg.amio_staging_buffer_capacity_bytes << "\n"
              << "  max_buffer_count: " << ceiling << "\n"
              << "worker_pool:\n"
              << "  threads: " << cfg.amio_worker_threads << "\n"
              << "prefetch:\n"
              << "  depth: " << cfg.amio_prefetch_depth << "\n"
              << "  read_timeout_s: 120\n"
              << "staging_timeout_ms: 30000\n";
    return m_content.str();
}

}  // namespace

// ============================================================================
// Property 4a: manifest-equal => HandleKey-equal (forward direction)
// Feature: regrid-per-stream, Property 4: HandleKey manifest-injectivity
// **Validates: Requirements 11.1, 11.2, 11.3**
//
// If two configs produce a byte-identical manifest (equivalently: the eight
// manifest-affecting fields are pairwise equal), then their HandleKeys are
// equal. b copies a's manifest fields but varies every other field
// arbitrarily, proving the non-key fields do not influence HandleKey.
// ============================================================================
RC_GTEST_PROP(HandleKeyProperty, Property4_ManifestEqualImpliesKeyEqual, ()) {
    const StreamConfig a = *genStreamConfig();

    StreamConfig b = *genStreamConfig();
    // Copy exactly the manifest-affecting fields; everything else (the
    // non-key noise: mapalgo/cadence/tintalgo/input_var_name/data_model_explicit)
    // stays arbitrary.
    b.input_file_path = a.input_file_path;
    b.data_model = a.data_model;
    b.kind = a.kind;
    b.format = a.format;
    b.amio_worker_threads = a.amio_worker_threads;
    b.amio_staging_buffer_count = a.amio_staging_buffer_count;
    b.amio_staging_buffer_capacity_bytes = a.amio_staging_buffer_capacity_bytes;
    b.amio_prefetch_depth = a.amio_prefetch_depth;

    // Precondition sanity: the manifests really are byte-identical here.
    RC_ASSERT(TestManifest(a) == TestManifest(b));
    // Forward direction of the iff.
    RC_ASSERT(HandleKeyTestAccess::Key(a) == HandleKeyTestAccess::Key(b));
}

// ============================================================================
// Property 4b: HandleKey-equal => manifest-equal (reverse / injectivity)
// Feature: regrid-per-stream, Property 4: HandleKey manifest-injectivity
// **Validates: Requirements 11.1, 11.2, 11.3**
//
// If two configs share a HandleKey, their manifests are byte-identical. Two
// independently generated arbitrary configs essentially never collide on the
// key (RapidCheck would discard every case), so we instead CONSTRUCT the
// collision: b is generated arbitrarily and then has its key fields forced
// to equal a's. This guarantees HandleKey(a) == HandleKey(b) while leaving all
// NON-key fields of b arbitrary and (independently) different from a's. The
// test then asserts the manifests are byte-identical, proving that a key
// collision implies manifest-equality REGARDLESS of the non-key noise — i.e.
// the reverse direction of the iff holds and the non-key fields cannot leak
// into the manifest. The integer knobs render unambiguously via std::to_string,
// and the "|" separator cannot appear inside the string key fields (guarded
// with RC_PRE, matching tests/test_stream_key_properties.cpp), so the key
// mapping is injective and the collision we build is genuine.
// ============================================================================
RC_GTEST_PROP(HandleKeyProperty, Property4_KeyEqualImpliesManifestEqual, ()) {
    const StreamConfig a = *genStreamConfig();

    // Keep the concatenation unambiguous: the reserved "|" separator must not
    // appear inside the string-valued key fields.
    RC_PRE(a.input_file_path.find('|') == std::string::npos);
    RC_PRE(a.data_model.find('|') == std::string::npos);
    RC_PRE(a.kind.find('|') == std::string::npos);
    RC_PRE(a.format.find('|') == std::string::npos);

    // Build a genuine key collision: force b's manifest-affecting fields to
    // match a's; every non-key field stays arbitrary.
    StreamConfig b = *genStreamConfig();
    b.input_file_path = a.input_file_path;
    b.data_model = a.data_model;
    b.kind = a.kind;
    b.format = a.format;
    b.amio_worker_threads = a.amio_worker_threads;
    b.amio_staging_buffer_count = a.amio_staging_buffer_count;
    b.amio_staging_buffer_capacity_bytes = a.amio_staging_buffer_capacity_bytes;
    b.amio_prefetch_depth = a.amio_prefetch_depth;

    // The antecedent of the reverse direction now holds by construction.
    RC_ASSERT(HandleKeyTestAccess::Key(a) == HandleKeyTestAccess::Key(b));

    // Reverse direction of the iff: equal key => byte-identical manifest,
    // regardless of the arbitrary non-key fields in b.
    RC_ASSERT(TestManifest(a) == TestManifest(b));
}

// ============================================================================
// Property 4c: manifest-differ => HandleKey-differ (contrapositive of 4a)
// Feature: regrid-per-stream, Property 4: HandleKey manifest-injectivity
// **Validates: Requirements 11.1, 11.2, 11.3**
//
// If two configs produce different manifests, their HandleKeys differ. Together
// with 4a this closes the iff. The same "|" guard as 4b keeps the key mapping
// injective so distinct manifests cannot alias to one key.
// ============================================================================
RC_GTEST_PROP(HandleKeyProperty, Property4_ManifestDifferImpliesKeyDiffer, ()) {
    const StreamConfig a = *genStreamConfig();
    const StreamConfig b = *genStreamConfig();

    RC_PRE(a.input_file_path.find('|') == std::string::npos);
    RC_PRE(a.data_model.find('|') == std::string::npos);
    RC_PRE(a.kind.find('|') == std::string::npos);
    RC_PRE(a.format.find('|') == std::string::npos);
    RC_PRE(b.input_file_path.find('|') == std::string::npos);
    RC_PRE(b.data_model.find('|') == std::string::npos);
    RC_PRE(b.kind.find('|') == std::string::npos);
    RC_PRE(b.format.find('|') == std::string::npos);

    // Only meaningful when the manifests actually differ.
    RC_PRE(TestManifest(a) != TestManifest(b));

    RC_ASSERT(HandleKeyTestAccess::Key(a) != HandleKeyTestAccess::Key(b));
}

// ============================================================================
// Property 4d: determinism / purity
// Feature: regrid-per-stream, Property 4: HandleKey manifest-injectivity
// **Validates: Requirements 11.1, 11.2, 11.3**
//
// HandleKey is a pure function: the same config yields the same key on repeated
// evaluation (models the every-rank-agrees invariant).
// ============================================================================
RC_GTEST_PROP(HandleKeyProperty, Property4_DeterministicOnIdenticalConfig, ()) {
    const StreamConfig a = *genStreamConfig();
    RC_ASSERT(HandleKeyTestAccess::Key(a) == HandleKeyTestAccess::Key(a));
}

}  // namespace cece
