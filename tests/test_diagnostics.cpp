/**
 * @file test_diagnostics.cpp
 * @brief Unit tests for the diagnostic registry semantics (feature 003).
 *
 * These tests are framework-free: they exercise only CeceDiagnosticManager and
 * BasePhysicsScheme (both in cece_core), never ESMF or AMIO. That is deliberate
 * (research R8 tier-1) — the registry contract must be verifiable without a
 * coupled framework present.
 *
 * Coverage (tasks.md T003 a-f):
 *   (a) BasePhysicsScheme with a null manager records pending declarations
 *       (name/units/long_name) instead of silently dropping them.
 *   (b) CeceDiagnosticManager::SetGrid allocates pending + new fields at the
 *       grid extent regardless of the requested placeholder dims (FR-008).
 *   (c) Duplicate RegisterDiagnostic returns the shared existing field (one
 *       map entry) (FR-003).
 *   (d) RegisterDiagnostic throws naming the offender for coordinate names and
 *       for species names supplied via SetReservedNames (FR-009).
 *   (e) units/long_name are first-non-empty-wins (later registration cannot
 *       clobber a set value; an empty value adopts the later non-empty one).
 *   (f) Fields are zero-initialized on allocation (edge case: unfilled regions
 *       are well-defined).
 */
#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <conf/config.hpp>
#include <conf/value.hpp>
#include <functional>
#include <string>
#include <vector>

#include "cece/cece_diagnostics.hpp"
#include "cece/physics_scheme.hpp"

namespace cece {
namespace {

// A scheme that declares diagnostics through ResolveDiagnostic (the pattern the
// real schemes use), so we can observe pending declarations with metadata even
// when no manager is bound.
class DeclaringScheme : public BasePhysicsScheme {
   public:
    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override {
        BasePhysicsScheme::Initialize(config, diag_manager);
        temperature_anomaly_ = ResolveDiagnostic("temperature_anomaly", 1, 1, 1, "K", "Temperature deviation from reference");
        emission_efficiency_ = ResolveDiagnostic("emission_efficiency", 1, 1, 1, "kg m-2 s-1 K-1", "Emission per unit temperature anomaly");
    }
    void Run(CeceImportState&, CeceExportState&) override {}

   private:
    DualView3D temperature_anomaly_;
    DualView3D emission_efficiency_;
};

// Assert a thrown exception's message contains `needle`.
void ExpectThrowNaming(const std::function<void()>& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find(needle), std::string::npos) << "message was: " << e.what();
        return;
    }
    ADD_FAILURE() << "expected an exception naming '" << needle << "', but none was thrown";
}

}  // namespace

// (a) Null manager records pending declarations instead of dropping them.
TEST(DiagnosticsRegistryTest, NullManagerRecordsPendingDeclarations) {
    DeclaringScheme scheme;
    conf::Config cfg = conf::Config::from_string("reference_temperature: 298.15\n");

    scheme.Initialize(cfg.root(), /*diag_manager=*/nullptr);

    const auto& pending = scheme.PendingDeclarations();
    // Both diagnostics the scheme resolved must be recorded, not dropped.
    EXPECT_EQ(pending.size(), 2u);
    bool found_anomaly = false;
    for (const auto& decl : pending) {
        if (decl.name == "temperature_anomaly") {
            found_anomaly = true;
            EXPECT_EQ(decl.units, "K");
            EXPECT_EQ(decl.long_name, "Temperature deviation from reference");
        }
    }
    EXPECT_TRUE(found_anomaly) << "temperature_anomaly pending declaration missing";
}

// (b) SetGrid allocates pending + new fields at grid extent, ignoring placeholder dims.
TEST(DiagnosticsRegistryTest, SetGridAllocatesAtGridExtent) {
    CeceDiagnosticManager manager;

    // Register before the grid is known (pending path).
    manager.RegisterDiagnostic("pending_diag", 1, 1, 1, "K", "declared early");
    manager.SetGrid(3, 4, 5);
    // Register after the grid is known (immediate allocation).
    manager.RegisterDiagnostic("late_diag", 1, 1, 1, "m", "declared late");

    const auto& fields = manager.Fields();
    ASSERT_EQ(fields.count("pending_diag"), 1u);
    ASSERT_EQ(fields.count("late_diag"), 1u);

    for (const std::string& name : {std::string("pending_diag"), std::string("late_diag")}) {
        const auto host = fields.at(name).data.view_host();
        EXPECT_EQ(host.extent(0), 3u) << name;
        EXPECT_EQ(host.extent(1), 4u) << name;
        EXPECT_EQ(host.extent(2), 5u) << name;
    }
}

// (c) Duplicate registration returns the shared field (one map entry).
TEST(DiagnosticsRegistryTest, DuplicateReturnsSharedField) {
    CeceDiagnosticManager manager;
    manager.SetGrid(2, 2, 2);

    DualView3D first = manager.RegisterDiagnostic("shared", 2, 2, 2, "K", "shared diag");
    manager.RegisterDiagnostic("shared", 2, 2, 2, "K", "ignored duplicate");

    EXPECT_EQ(manager.Fields().count("shared"), 1u);

    // The two handles alias the same storage: a device write through the first
    // is visible through the registry after a host sync.
    auto device = first.view_device();
    Kokkos::deep_copy(device, 42.0);
    first.modify_device();
    first.sync<Kokkos::HostSpace>();
    EXPECT_DOUBLE_EQ(first.view_host()(1, 1, 1), 42.0);
    EXPECT_DOUBLE_EQ(manager.Fields().at("shared").data.view_host()(1, 1, 1), 42.0);
}

// (d) Coordinate and reserved species names are rejected, naming the offender.
TEST(DiagnosticsRegistryTest, RejectsCoordinateAndSpeciesNames) {
    CeceDiagnosticManager manager;
    manager.SetGrid(2, 2, 1);
    manager.SetReservedNames({"co", "nox"});

    for (const std::string& coord : {"lon", "lat", "lev", "time", "lon_bnds", "lat_bnds", "mesh"}) {
        ExpectThrowNaming([&] { manager.RegisterDiagnostic(coord, 2, 2, 1, "x", "y"); }, coord);
    }
    for (const std::string& species : {"co", "nox"}) {
        ExpectThrowNaming([&] { manager.RegisterDiagnostic(species, 2, 2, 1, "x", "y"); }, species);
    }
}

// (e) units/long_name are first-non-empty-wins.
TEST(DiagnosticsRegistryTest, MetadataFirstNonEmptyWins) {
    CeceDiagnosticManager manager;
    manager.SetGrid(2, 2, 1);

    // Register with empty metadata first, then supply it: the non-empty wins.
    manager.RegisterDiagnostic("meta", 2, 2, 1, "", "");
    manager.RegisterDiagnostic("meta", 2, 2, 1, "K", "temperature anomaly");
    EXPECT_EQ(manager.Fields().at("meta").units, "K");
    EXPECT_EQ(manager.Fields().at("meta").long_name, "temperature anomaly");

    // A later conflicting value must NOT clobber the established one.
    manager.RegisterDiagnostic("meta", 2, 2, 1, "C", "conflicting long name");
    EXPECT_EQ(manager.Fields().at("meta").units, "K");
    EXPECT_EQ(manager.Fields().at("meta").long_name, "temperature anomaly");
}

// (f) Fields are zero-initialized on allocation.
TEST(DiagnosticsRegistryTest, FieldsZeroInitialized) {
    CeceDiagnosticManager manager;
    manager.SetGrid(2, 3, 4);
    manager.RegisterDiagnostic("fresh", 2, 3, 4, "K", "brand new");

    const auto host = manager.Fields().at("fresh").data.view_host();
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 4; ++k) EXPECT_DOUBLE_EQ(host(i, j, k), 0.0);
}

}  // namespace cece

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    Kokkos::initialize(argc, argv);
    int result = RUN_ALL_TESTS();
    Kokkos::finalize();
    return result;
}
