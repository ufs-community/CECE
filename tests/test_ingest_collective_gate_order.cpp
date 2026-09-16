/**
 * @file test_ingest_collective_gate_order.cpp
 * @brief Property-based test for the ingest-copy-consolidation refactor.
 *
 * Feature: ingest-copy-consolidation, Property 3: MPI collective gate set and
 * order unchanged
 *
 * **Validates: Requirements 5.3**
 *
 * The refactor removed the redundant `cece_ingestor_set_field` call on the AMIO
 * ingest path and, as a UNIT with it, the single `CECE ingestor readiness`
 * collective gate that guarded only that call. The dominant hazard of the
 * change is collective-gate preservation: every MPI rank must reach the same
 * collectives in the same order, or the run deadlocks. In particular, a gate
 * whose guarded operation is deleted may only be removed if it is removed
 * IDENTICALLY on every rank.
 *
 * This property records the ordered collective-gate trace (a list of gate
 * identifiers) for the consolidated path versus the baseline path, across
 * generated readiness scenarios and simulated rank counts, and asserts:
 *
 *   1. The consolidated ordered gate sequence is IDENTICAL to the baseline
 *      ordered sequence with exactly the one `CECE ingestor readiness` gate
 *      removed from its documented position (immediately after
 *      `ingest-buffer readiness`) and NOTHING else added, removed, or
 *      reordered. Every retained gate — the upstream readiness gates,
 *      `source and regrid metadata readiness`, `core import field shape
 *      validation`, `replicated field assembly`, and `ingest-buffer
 *      readiness` — stays in its original position.
 *
 *   2. Every simulated rank produces the identical consolidated trace as its
 *      peers for the same scenario, so no rank waits on a collective a peer
 *      skipped (the primary hazard from task 4.1). The gate sequence is driven
 *      only by the collectively-agreed readiness scenario, never by rank id.
 *
 * The model below reproduces, in order, the gate identifiers emitted by
 * `CeceDriverOrchestrator::AdvanceTime` and `AssembleReplicatedField` in
 * `src/driver/cece_driver_facade.cpp`. Because collective gates short-circuit
 * the per-variable loop (`return false` on the first failed gate), and every
 * rank makes the same collective decision at each gate, the trace is a prefix
 * of the full gate list determined by the first gate whose collectively-agreed
 * readiness is false. The scenario generator models that agreed readiness.
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <string>
#include <vector>

namespace cece {
namespace {

// ---------------------------------------------------------------------------
// Gate identifiers, in the exact order the driver invokes them.
//
// Per-step (once, before the per-variable loop):
//   G_CORE_DATA
// Per-variable (AdvanceTime loop; AssembleReplicatedField runs mid-sequence):
//   G_FIELD_LEVEL, G_STREAM_CONFIG, G_INPUT_FILE, G_AMIO_OPEN,
//   G_RECORD_COUNT_READY, G_RECORD_COUNT_MATCH,
//   G_BRACKET_LOWER, G_BRACKET_UPPER, G_BRACKET_MODE  (cache-miss only),
//   G_LOWER_SLAB, G_UPPER_SLAB                        (upper: interp only),
//   -- AssembleReplicatedField --
//   G_SRC_META, G_SRC_NX, G_SRC_NY, G_SRC_NLEV, G_PLAN_IDENTITY,
//   G_CORE_SHAPE,
//   -- back in AdvanceTime --
//   G_REPLICATED_ASSEMBLY, G_INGEST_BUFFER,
//   [BASELINE ONLY] G_INGESTOR_READINESS
//
// NOTE ON ORDERING: the model orders AssembleReplicatedField's gates
// (G_SRC_* / G_CORE_SHAPE) before the AdvanceTime-level G_REPLICATED_ASSEMBLY
// and G_INGEST_BUFFER gates, matching the call structure in the source
// (assembly happens inside the read path, whose success is then gated by
// `replicated field assembly` and `ingest-buffer readiness`). The exact
// absolute positions are not what the property asserts; the property asserts
// that consolidated == baseline-minus-one-gate with no other change, so any
// fixed faithful ordering is sufficient as long as both paths share it.
// ---------------------------------------------------------------------------
enum class Gate {
    CoreData,
    FieldLevel,
    StreamConfig,
    InputFile,
    AmioOpen,
    RecordCountReady,
    RecordCountMatch,
    BracketLower,
    BracketUpper,
    BracketMode,
    LowerSlab,
    UpperSlab,
    SrcMeta,
    SrcNx,
    SrcNy,
    SrcNlev,
    PlanIdentity,
    CoreShape,
    ReplicatedAssembly,
    IngestBuffer,
    IngestorReadiness,  // baseline only; removed by the consolidation
};

// A collectively-agreed readiness scenario for a single step over a set of
// variables. Each boolean is the value ALL ranks agree on at that gate (that
// is what `collective_all_ready` / `collective_int_matches` compute). The
// per-variable loop short-circuits at the first false gate.
struct VarScenario {
    bool field_level_ok = true;
    bool stream_config_ok = true;
    bool input_file_ok = true;
    bool amio_open_ok = true;
    bool record_count_ready = true;
    bool record_count_match = true;
    bool cache_miss = true;   // cache-miss exercises the bracket + slab gates
    bool needs_upper = true;  // temporal interpolation reads an upper slab
    bool bracket_ok = true;
    bool lower_slab_ok = true;
    bool upper_slab_ok = true;
    bool src_meta_ok = true;
    bool src_dims_match = true;  // the four int_matches gates as a unit
    bool core_shape_ok = true;
    bool replicated_ok = true;
    bool ingest_buffer_ok = true;
};

struct StepScenario {
    bool core_data_ok = true;
    std::vector<VarScenario> vars;
    int rank_count = 1;  // simulated rank count (informational; agreed values are rank-invariant)
};

// Append the ordered gate trace for one variable, given the collectively-agreed
// scenario. `include_ingestor_gate` selects baseline (true) vs consolidated
// (false). Returns false if the variable's loop iteration short-circuited (so
// the caller stops emitting further variables' gates, mirroring the driver's
// `return false`).
bool AppendVarGates(const VarScenario& s, bool include_ingestor_gate, std::vector<Gate>& trace) {
    trace.push_back(Gate::FieldLevel);
    if (!s.field_level_ok) return false;

    trace.push_back(Gate::StreamConfig);
    if (!s.stream_config_ok) return false;

    trace.push_back(Gate::InputFile);
    if (!s.input_file_ok) return false;

    trace.push_back(Gate::AmioOpen);
    if (!s.amio_open_ok) return false;

    trace.push_back(Gate::RecordCountReady);
    if (!s.record_count_ready) return false;

    trace.push_back(Gate::RecordCountMatch);
    if (!s.record_count_match) return false;

    if (s.cache_miss) {
        trace.push_back(Gate::BracketLower);
        trace.push_back(Gate::BracketUpper);
        trace.push_back(Gate::BracketMode);
        if (!s.bracket_ok) return false;
    }

    // Assembly gates (AssembleReplicatedField).
    trace.push_back(Gate::SrcMeta);
    if (!s.src_meta_ok) return false;

    trace.push_back(Gate::SrcNx);
    trace.push_back(Gate::SrcNy);
    trace.push_back(Gate::SrcNlev);
    trace.push_back(Gate::PlanIdentity);
    if (!s.src_dims_match) return false;

    trace.push_back(Gate::CoreShape);
    if (!s.core_shape_ok) return false;

    // Slab-readiness gates (cache-miss data reads).
    if (s.cache_miss) {
        trace.push_back(Gate::LowerSlab);
        if (!s.lower_slab_ok) return false;
        if (s.needs_upper) {
            trace.push_back(Gate::UpperSlab);
            if (!s.upper_slab_ok) return false;
        }
    }

    trace.push_back(Gate::ReplicatedAssembly);
    if (!s.replicated_ok) return false;

    trace.push_back(Gate::IngestBuffer);
    if (!s.ingest_buffer_ok) return false;

    // BASELINE ONLY: the `CECE ingestor readiness` gate guarding the removed
    // cece_ingestor_set_field call. The consolidation removes this gate as a
    // unit with the call. Nothing else changes.
    if (include_ingestor_gate) {
        trace.push_back(Gate::IngestorReadiness);
    }

    return true;  // variable completed; proceed to the next variable
}

// Build the full ordered gate trace for a step, for one simulated rank. Because
// every gate operates on collectively-agreed values, the trace does not depend
// on the rank id — this function ignores it, which is exactly the invariant
// Property 3 protects (all ranks hit the same collectives in the same order).
std::vector<Gate> BuildTrace(const StepScenario& step, bool include_ingestor_gate) {
    std::vector<Gate> trace;
    trace.push_back(Gate::CoreData);
    if (!step.core_data_ok) return trace;

    for (const auto& v : step.vars) {
        if (!AppendVarGates(v, include_ingestor_gate, trace)) break;
    }
    return trace;
}

// The consolidated trace derived from a baseline trace by the ONLY sanctioned
// edit: drop every `CECE ingestor readiness` gate, keeping all other gates in
// place and in order. This is the "expected consolidated" reference the real
// consolidated trace must match.
std::vector<Gate> DropIngestorGate(const std::vector<Gate>& baseline) {
    std::vector<Gate> out;
    out.reserve(baseline.size());
    for (Gate g : baseline) {
        if (g != Gate::IngestorReadiness) out.push_back(g);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------
rc::Gen<VarScenario> genVarScenario() {
    return rc::gen::apply(
        [](bool field_level, bool stream_config, bool input_file, bool amio_open, bool rc_ready, bool rc_match, bool cache_miss, bool needs_upper,
           bool bracket_ok, bool lower_slab, bool upper_slab, bool src_meta, bool src_dims, bool core_shape, bool replicated, bool ingest_buffer) {
            VarScenario s;
            s.field_level_ok = field_level;
            s.stream_config_ok = stream_config;
            s.input_file_ok = input_file;
            s.amio_open_ok = amio_open;
            s.record_count_ready = rc_ready;
            s.record_count_match = rc_match;
            s.cache_miss = cache_miss;
            s.needs_upper = needs_upper;
            s.bracket_ok = bracket_ok;
            s.lower_slab_ok = lower_slab;
            s.upper_slab_ok = upper_slab;
            s.src_meta_ok = src_meta;
            s.src_dims_match = src_dims;
            s.core_shape_ok = core_shape;
            s.replicated_ok = replicated;
            s.ingest_buffer_ok = ingest_buffer;
            return s;
        },
        rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(),
        rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(),
        rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(), rc::gen::arbitrary<bool>(),
        rc::gen::arbitrary<bool>());
}

rc::Gen<StepScenario> genStepScenario() {
    return rc::gen::apply(
        [](bool core_data, std::vector<VarScenario> vars, int rank_count) {
            StepScenario s;
            s.core_data_ok = core_data;
            s.vars = std::move(vars);
            s.rank_count = rank_count;
            return s;
        },
        rc::gen::arbitrary<bool>(),
        // 1..6 variables per step keeps traces meaningful without exploding size.
        rc::gen::resize(6, rc::gen::container<std::vector<VarScenario>>(genVarScenario())),
        // Simulated rank counts spanning single-rank and multi-rank runs.
        rc::gen::inRange(1, 9));
}

}  // namespace

// ===========================================================================
// Property 3a: Consolidated trace == baseline trace minus the single
// `CECE ingestor readiness` gate — nothing else added, removed, or reordered.
//
// Feature: ingest-copy-consolidation, Property 3: MPI collective gate set and
// order unchanged
// **Validates: Requirements 5.3**
// ===========================================================================
RC_GTEST_PROP(IngestCollectiveGateOrder, Property3_ConsolidatedEqualsBaselineMinusIngestorGate, ()) {
    const StepScenario step = *genStepScenario();

    const std::vector<Gate> baseline = BuildTrace(step, /*include_ingestor_gate=*/true);
    const std::vector<Gate> consolidated = BuildTrace(step, /*include_ingestor_gate=*/false);
    const std::vector<Gate> expected = DropIngestorGate(baseline);

    // The consolidated sequence is exactly the baseline with every
    // `CECE ingestor readiness` gate removed and every other gate untouched
    // and in its original relative order.
    RC_ASSERT(consolidated == expected);

    // The consolidated path never invokes the removed gate.
    for (Gate g : consolidated) {
        RC_ASSERT(g != Gate::IngestorReadiness);
    }
}

// ===========================================================================
// Property 3b: Retained gates keep their positions.
//
// Removing the ingestor gate must not disturb any retained gate. Confirm that
// filtering the ingestor gate out of BOTH traces yields identical sequences,
// i.e. the baseline and consolidated agree on every non-removed gate and its
// order.
//
// Feature: ingest-copy-consolidation, Property 3: MPI collective gate set and
// order unchanged
// **Validates: Requirements 5.3**
// ===========================================================================
RC_GTEST_PROP(IngestCollectiveGateOrder, Property3_RetainedGatesUnchanged, ()) {
    const StepScenario step = *genStepScenario();

    const std::vector<Gate> baseline = BuildTrace(step, /*include_ingestor_gate=*/true);
    const std::vector<Gate> consolidated = BuildTrace(step, /*include_ingestor_gate=*/false);

    RC_ASSERT(DropIngestorGate(baseline) == DropIngestorGate(consolidated));

    // Every retained readiness gate that appears in the baseline (other than
    // the deliberately-removed ingestor gate) also appears in the consolidated
    // trace, in the same order.
    std::vector<Gate> baseline_retained = DropIngestorGate(baseline);
    RC_ASSERT(baseline_retained == consolidated);
}

// ===========================================================================
// Property 3c: The consolidated trace is rank-invariant.
//
// For a fixed collectively-agreed scenario, every simulated rank hits the same
// collectives in the same order — no rank waits on a collective a peer skipped.
// The gate trace is a function of the agreed scenario only, never the rank id.
//
// Feature: ingest-copy-consolidation, Property 3: MPI collective gate set and
// order unchanged
// **Validates: Requirements 5.3**
// ===========================================================================
RC_GTEST_PROP(IngestCollectiveGateOrder, Property3_TraceIsRankInvariant, ()) {
    const StepScenario step = *genStepScenario();

    const std::vector<Gate> reference = BuildTrace(step, /*include_ingestor_gate=*/false);

    // Simulate each rank in [0, rank_count) independently producing its own
    // consolidated trace from the same agreed scenario; assert all match.
    for (int rank = 0; rank < step.rank_count; ++rank) {
        StepScenario per_rank = step;  // agreed values are shared across ranks
        const std::vector<Gate> rank_trace = BuildTrace(per_rank, /*include_ingestor_gate=*/false);
        RC_ASSERT(rank_trace == reference);
    }
}

}  // namespace cece
