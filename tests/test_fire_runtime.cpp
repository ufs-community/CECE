// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <vector>

#include "cece/cece_fire_input.hpp"
#include "cece/cece_fire_runtime.hpp"
#include "cece/cece_internal.hpp"
#include "cece/cece_physics_factory.hpp"
#include "cece/physics/cece_fire.hpp"
#include "conf/config.hpp"
#include "support/cece_fire_test_fixtures.hpp"

extern "C" void cece_core_run(void* data_ptr, int hour, int day_of_week, int* rc);
extern "C" void cece_core_finalize(void* data_ptr, int* rc);

namespace {
using namespace cece::test::fire;

constexpr std::string_view kAbcSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

cece::FireBulkInventory ProviderInventory(std::string content_hash = std::string(kAbcSha256), std::string start = "2022-07-01T00:00:00Z",
                                          std::string end = "2022-07-01T01:00:00Z", std::string cutoff = "2022-07-02T00:00:00Z") {
    auto grid = std::make_shared<const cece::FireGrid>(cece::FireGridDefinition{"provider-grid",
                                                                                std::string(64, 'b'),
                                                                                1,
                                                                                1,
                                                                                {0.0},
                                                                                {0.0},
                                                                                {{{-1, 1, 1, -1}}},
                                                                                {{{-1, -1, 1, 1}}},
                                                                                {25.0},
                                                                                "EPSG:4326",
                                                                                cece::FireLongitudeConvention::Minus180To180});
    const cece::FireProfileIdentity profile{"synthetic", "1", std::string(64, 'c')};
    return cece::FireBulkInventory(
        {cece::FireInputIdentity({1, "source", "product-1", "algorithm-1", "delivery-1", std::move(content_hash), cece::FireUtcTime(start),
                                  cece::FireUtcTime(end), cece::FireUtcTime("2022-07-01T00:00:00Z"), cece::FireUtcTime("2022-07-01T00:00:00Z"),
                                  cece::FireUtcTime(cutoff), cece::FireCutoffMode::Fixed, grid}),
         {cece::FireBulkSpecies::CO},
         {"component"},
         {{"component", {profile, profile}}},
         {5.0},
         {cece::FireQualityState::Observed},
         {1.0},
         {std::string(64, 'd')},
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt,
         std::nullopt});
}

cece::FireCacheContext ProviderContext() {
    return {cece::FireInputMode::Product,
            {"quality", "1", std::string(64, 'e')},
            std::nullopt,
            std::nullopt,
            {"target", "1", std::string(64, 'f')},
            std::string(64, 'a'),
            std::nullopt,
            cece::FireTimeAllocation::Uniform,
            std::nullopt};
}

struct ProviderFile {
    std::string path;
    ProviderFile() {
        char name[] = "/tmp/cece-fire-provider-XXXXXX";
        const int descriptor = mkstemp(name);
        if (descriptor < 0) throw std::runtime_error("Cannot create provider test file");
        close(descriptor);
        path = name;
        Write("abc");
    }
    ~ProviderFile() {
        std::remove(path.c_str());
    }
    void Write(std::string_view bytes) const {
        std::ofstream output(path, std::ios::binary);
        output << bytes;
        if (!output) throw std::runtime_error("Cannot write provider test file");
    }
};

TEST(test_fire_runtime, ContentVerificationUsesKnownSha256AndReadsChangedFiles) {
    EXPECT_EQ(cece::FireContentSha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(cece::FireContentSha256("abc"), kAbcSha256);
    EXPECT_EQ(cece::FireContentSha256(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const std::string binary("a\0b", 3);
    EXPECT_NE(cece::FireContentSha256(binary), cece::FireContentSha256("a"));
    ProviderFile file;
    EXPECT_EQ(cece::FireFileSha256(file.path), kAbcSha256);
    file.Write(std::string(1000000, 'a'));
    EXPECT_EQ(cece::FireFileSha256(file.path), cece::FireContentSha256(std::string(1000000, 'a')));
    EXPECT_THROW(cece::FireFileSha256(file.path + ".missing"), std::runtime_error);
}

TEST(test_fire_runtime, RegisteredFireSchemeMapsBulkMassAndExportsSurfaceFlux) {
    cece::PhysicsSchemeConfig scheme_config;
    scheme_config.name = "fire";
    auto scheme = cece::PhysicsFactory::CreateScheme(scheme_config);
    ASSERT_NE(scheme, nullptr);
    auto* fire_scheme = dynamic_cast<cece::FireScheme*>(scheme.get());
    ASSERT_NE(fire_scheme, nullptr);

    const auto profile_path = std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml";
    auto options = conf::Config::from_string(
        "chemistry_state: bulk\n"
        "target_id: SYNTHETIC\n"
        "target_version: test-v1\n"
        "fire_mapping_file: " +
        profile_path + "\nrequested_species: [CO]\n");
    fire_scheme->Initialize(options.root(), nullptr);
    EXPECT_EQ(fire_scheme->ExportFieldNames(), std::vector<std::string>{"FIRE_CO"});
    const auto inventory = ProviderInventory();
    fire_scheme->StageBorrowedBulkInventory(inventory);

    cece::CeceImportState imports;
    cece::CeceExportState exports;
    exports.fields.emplace("FIRE_CO", cece::DualView3D("FIRE_CO", 1, 1, 1));
    fire_scheme->Run(imports, exports);
    auto output = exports.fields.at("FIRE_CO").view_host();
    EXPECT_NEAR(output(0, 0, 0), 5.0 / (25.0 * 3600.0), 1e-14);
    EXPECT_THROW(fire_scheme->Run(imports, exports), std::runtime_error);
}

TEST(test_fire_runtime, NativeDiagnosticsRemainSeparateFromMappedEmissions) {
    cece::FireScheme scheme;
    auto options = conf::Config::from_string(
        "chemistry_state: bulk\n"
        "target_id: SYNTHETIC\n"
        "target_version: test-v1\n"
        "fire_mapping_file: " +
        std::string(CECE_SOURCE_DIR) +
        "/tests/data/fire/synthetic_target_profile.yaml\nrequested_species: [CO]\nnative_diagnostic_species: [PM25, NOX, VOC]\n");
    scheme.Initialize(options.root(), nullptr);
    EXPECT_EQ(scheme.ExportFieldNames(), (std::vector<std::string>{"FIRE_CO", "FIRE_NATIVE_PM25", "FIRE_NATIVE_NOX", "FIRE_NATIVE_VOC"}));

    auto data = ProviderInventory().Data();
    data.species = {cece::FireBulkSpecies::CO, cece::FireBulkSpecies::PM25, cece::FireBulkSpecies::NOX, cece::FireBulkSpecies::VOC,
                    cece::FireBulkSpecies::BC};
    data.mass_kg = {5, 6, 7, 8, 2};
    data.native_diagnostic_species = {cece::FireBulkSpecies::PM25, cece::FireBulkSpecies::NOX, cece::FireBulkSpecies::VOC};
    cece::FireBulkInventory inventory(data);
    const auto profile = cece::LoadFireTargetProfile(std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml",
                                                     "SYNTHETIC", "test-v1");
    EXPECT_THROW(cece::MapFireBulkInventory(inventory, profile, {cece::FireBulkSpecies::NOX}), std::invalid_argument);
    data.native_diagnostic_species.clear();
    EXPECT_THROW((void)cece::FireBulkInventory{data}, std::invalid_argument);

    scheme.StageBorrowedBulkInventory(inventory);
    cece::CeceImportState imports;
    cece::CeceExportState exports;
    for (const auto& name : scheme.ExportFieldNames()) exports.fields.emplace(name, cece::DualView3D(name, 1, 1, 1));
    scheme.Run(imports, exports);
    for (const auto& [name, mass] : std::vector<std::pair<std::string, double>>{
             {"FIRE_CO", 5}, {"FIRE_NATIVE_PM25", 6}, {"FIRE_NATIVE_NOX", 7}, {"FIRE_NATIVE_VOC", 8}}) {
        EXPECT_NEAR(exports.fields.at(name).view_host()(0, 0, 0), mass / (25.0 * 3600.0), 1e-14);
    }
}

TEST(test_fire_runtime, TargetSpecificFireInventoryBypassesBulkMappingForExactTarget) {
    cece::FireScheme scheme;
    auto options = conf::Config::from_string(
        "chemistry_state: target_specific\n"
        "target_id: mechanism-a\n"
        "target_version: 3\n");
    scheme.Initialize(options.root(), nullptr);
    auto inventory = ProviderInventory();
    scheme.StageTargetSpecificInventory({inventory.Data().identity, "mechanism-a", "3", {{"CO", "kg"}}, {5.0}});

    cece::CeceImportState imports;
    cece::CeceExportState exports;
    exports.fields.emplace("FIRE_CO", cece::DualView3D("FIRE_CO", 1, 1, 1));
    scheme.Run(imports, exports);
    auto output = exports.fields.at("FIRE_CO").view_host();
    EXPECT_NEAR(output(0, 0, 0), 5.0 / (25.0 * 3600.0), 1e-14);
}

class MockFireRuntimeAdapter final : public cece::FireRuntimeAdapter {
   public:
    explicit MockFireRuntimeAdapter(bool* finalized_out = nullptr) : finalized_out(finalized_out) {}

    void Initialize(const cece::FireConfig&, int, int, int) override {
        initialized = true;
    }

    cece::FireRuntimeInventoryView PrepareInterval(const cece::FireRuntimeIntervalRequest&) override {
        prepared = true;
        if (fail_prepare) throw std::runtime_error("mock interval preparation failure");
        return std::cref(inventory);
    }

    void CommitInterval() override {
        committed = true;
        if (fail_commit) throw std::runtime_error("mock interval commit failure");
    }

    void AbortInterval() noexcept override {
        aborted = true;
    }

    void Finalize() override {
        finalized = true;
        if (finalized_out) *finalized_out = true;
        if (fail_finalize) throw std::runtime_error("mock adapter finalization failure");
    }

    cece::FireBulkInventory inventory{ProviderInventory()};
    bool initialized = false;
    bool prepared = false;
    bool committed = false;
    bool aborted = false;
    bool finalized = false;
    bool fail_finalize = false;
    bool fail_prepare = false;
    bool fail_commit = false;
    bool* finalized_out = nullptr;
};

std::string FireSchemeOptions() {
    return "chemistry_state: bulk\n"
           "target_id: SYNTHETIC\n"
           "target_version: test-v1\n"
           "fire_mapping_file: " +
           std::string(CECE_SOURCE_DIR) + "/tests/data/fire/synthetic_target_profile.yaml\nrequested_species: [CO]\n";
}

MockFireRuntimeAdapter* ConfigureCoreFireRuntime(cece::CeceInternalData& state, bool fail_prepare = false, bool fail_commit = false) {
    cece::PhysicsSchemeConfig config;
    config.name = "fire";
    config.refresh_interval_seconds = 3600;
    config.fire = cece::FireConfig{cece::FireInputMode::Product, cece::FireInputProvider::Prepared, cece::FireCutoffMode::Fixed};
    config.fire->availability_cutoff = cece::FireUtcTime("2022-07-02T00:00:00Z");
    config.options = conf::Config::from_string(FireSchemeOptions()).root();
    state.config.physics_schemes.push_back(config);

    auto scheme = std::make_unique<cece::FireScheme>();
    scheme->Initialize(config.options, nullptr);
    state.active_schemes.push_back(std::move(scheme));
    state.clock = std::make_unique<cece::CeceClock>("2022-07-01T00:00:00", "2022-07-01T01:00:00", 3600,
                                                    std::vector<cece::ClockComponent>{{cece::ComponentType::kPhysicsScheme, "fire", 3600}});
    state.nx = state.ny = state.ny_local = state.nz = 1;
    state.export_state.fields.emplace("FIRE_CO", cece::DualView3D("FIRE_CO", 1, 1, 1));

    auto adapter = std::make_unique<MockFireRuntimeAdapter>();
    adapter->fail_prepare = fail_prepare;
    adapter->fail_commit = fail_commit;
    auto* mock = adapter.get();
    adapter->Initialize(*config.fire, 1, 1, 1);
    state.fire_runtime_adapter = std::move(adapter);
    state.fire_runtime_initialized = true;
    return mock;
}

TEST(test_fire_runtime, PreparedRuntimeAdapterIsRegisteredAndDuplicateRegistrationFails) {
    auto adapter = cece::CreateFireRuntimeAdapter(cece::FireInputProvider::Prepared);
    ASSERT_NE(adapter, nullptr);
    EXPECT_THROW(cece::RegisterFireRuntimeAdapter(cece::FireInputProvider::Prepared, []() { return std::make_unique<MockFireRuntimeAdapter>(); }),
                 std::logic_error);
}

TEST(test_fire_runtime, UtcTimestampCanBeDerivedFromClockEpochSeconds) {
    const cece::FireUtcTime start("2022-07-01T00:00:00Z");
    const auto end = cece::FireUtcTime::FromEpochSeconds(start.EpochSeconds() + 3600);
    EXPECT_EQ(end.ToString(), "2022-07-01T01:00:00Z");
    EXPECT_THROW(cece::FireUtcTime::FromEpochSeconds(std::numeric_limits<std::int64_t>::max()), std::invalid_argument);
}

TEST(test_fire_runtime, FireExportDescriptorsRequireConfiguredFieldsAndFluxUnits) {
    const auto bulk_config = conf::Config::from_string(FireSchemeOptions());
    cece::FireScheme bulk_scheme;
    bulk_scheme.Initialize(bulk_config.root(), nullptr);

    cece::CeceConfig output_config;
    output_config.output_config.enabled = true;
    output_config.output_config.fields.push_back({"FIRE_CO", {{"units", "kg m-2 s-1"}}});
    EXPECT_NO_THROW(cece::ValidateFireExportDescriptors(output_config, bulk_scheme));

    auto invalid_units = output_config;
    invalid_units.output_config.fields = cece::CeceOutputFieldCollection{};
    invalid_units.output_config.fields.push_back({"FIRE_CO", {{"units", "kg"}}});
    EXPECT_THROW(cece::ValidateFireExportDescriptors(invalid_units, bulk_scheme), std::invalid_argument);

    cece::CeceConfig nuopc_config;
    nuopc_config.nuopc.export_fields.emplace_back("FIRE_CO", cece::NuopcFieldSpec{"fire carbon monoxide", "kg m-2 s-1", ""});
    EXPECT_NO_THROW(cece::ValidateFireExportDescriptors(nuopc_config, bulk_scheme));
    nuopc_config.nuopc.export_fields.front().second.units.clear();
    EXPECT_THROW(cece::ValidateFireExportDescriptors(nuopc_config, bulk_scheme), std::invalid_argument);

    const auto target_options = conf::Config::from_string("chemistry_state: target_specific\ntarget_id: MECHANISM\ntarget_version: v1\n");
    cece::FireScheme target_scheme;
    target_scheme.Initialize(target_options.root(), nullptr);
    cece::CeceConfig target_config;
    target_config.output_config.enabled = true;
    target_config.output_config.fields.push_back({"FIRE_CO", {{"units", "kg m-2 s-1"}}});
    EXPECT_NO_THROW(cece::ValidateFireExportDescriptors(target_config, target_scheme));
}

TEST(test_fire_runtime, CoreCommitsPreparedFireOnlyAfterExportSyncSucceeds) {
    cece::CeceInternalData state;
    auto* adapter = ConfigureCoreFireRuntime(state);
    int rc = -1;
    cece_core_run(&state, 0, 0, &rc);
    EXPECT_EQ(rc, 1);
    EXPECT_TRUE(adapter->prepared);
    EXPECT_TRUE(adapter->committed);
    EXPECT_FALSE(adapter->aborted);
    EXPECT_FALSE(state.fire_interval_pending);
    EXPECT_EQ(state.fire_last_committed_elapsed_seconds, 3600);
    EXPECT_NEAR(state.export_state.fields.at("FIRE_CO").view_host()(0, 0, 0), 5.0 / (25.0 * 3600.0), 1e-14);
}

TEST(test_fire_runtime, CoreAbortsPreparedFireWhenCommitFails) {
    cece::CeceInternalData state;
    auto* adapter = ConfigureCoreFireRuntime(state, false, true);
    int rc = 0;
    cece_core_run(&state, 0, 0, &rc);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(adapter->prepared);
    EXPECT_TRUE(adapter->committed);
    EXPECT_TRUE(adapter->aborted);
    EXPECT_FALSE(state.fire_interval_pending);
    EXPECT_EQ(state.fire_last_committed_elapsed_seconds, 0);
}

TEST(test_fire_runtime, CorePropagatesPreparationFailureAndAbortsAdapter) {
    cece::CeceInternalData state;
    auto* adapter = ConfigureCoreFireRuntime(state, true);
    int rc = 0;
    cece_core_run(&state, 0, 0, &rc);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(adapter->prepared);
    EXPECT_FALSE(adapter->committed);
    EXPECT_TRUE(adapter->aborted);
    EXPECT_FALSE(state.fire_interval_pending);
}

TEST(test_fire_runtime, CoreRunWithoutFireKeepsLegacyUnconditionalPath) {
    cece::CeceInternalData state;
    int rc = -1;
    cece_core_run(&state, 0, 0, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(state.fire_runtime_adapter, nullptr);
}

TEST(test_fire_runtime, CoreFinalizesInitializedFireAdapterAndReportsCleanupFailure) {
    bool finalized = false;
    auto* state = new cece::CeceInternalData();
    auto adapter = std::make_unique<MockFireRuntimeAdapter>(&finalized);
    state->fire_runtime_adapter = std::move(adapter);
    state->fire_runtime_initialized = true;
    int rc = -1;
    cece_core_finalize(state, &rc);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(finalized);

    finalized = false;
    state = new cece::CeceInternalData();
    auto failing_adapter = std::make_unique<MockFireRuntimeAdapter>(&finalized);
    failing_adapter->fail_finalize = true;
    state->fire_runtime_adapter = std::move(failing_adapter);
    state->fire_runtime_initialized = true;
    rc = 0;
    cece_core_finalize(state, &rc);
    EXPECT_EQ(rc, -2);
    EXPECT_TRUE(finalized);
}

TEST(test_fire_runtime, PreparedAndCoupledProvidersShareCacheAndLifecycle) {
    ProviderFile file;
    cece::FireIntervalProvider prepared(cece::FireInputProvider::Prepared, {1, 1, 1, 8192}, {"run", "revision"});
    cece::FireIntervalProvider coupled(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "revision"});
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Evicted);
    prepared.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path);
    coupled.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc");
    const auto key = prepared.Status(0).cache_identity;
    EXPECT_EQ(key, coupled.Status(0).cache_identity);
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Ready);
    EXPECT_DOUBLE_EQ(prepared.Ready(0).SpeciesBudgetKg(cece::FireBulkSpecies::CO), 5.0);
    EXPECT_THROW(prepared.Evict(0), std::logic_error);
    EXPECT_THROW(prepared.Consume(0, "stale-key"), std::invalid_argument);
    prepared.Consume(0, key);
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Consumed);
    EXPECT_THROW(prepared.Ready(0), std::logic_error);
    EXPECT_THROW(prepared.Consume(0, key), std::logic_error);
    prepared.Evict(0);
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Evicted);
    EXPECT_THROW(prepared.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path), std::invalid_argument);
    EXPECT_EQ(prepared.Status(0).state, cece::FireIntervalState::Rejected);
    EXPECT_FALSE(prepared.Status(0).rejection_reason.empty());
}

TEST(test_fire_runtime, FailedIntegrityCannotLeaveAReadyOrZeroFilledInterval) {
    ProviderFile file;
    cece::FireIntervalProvider provider(cece::FireInputProvider::Prepared, {1, 1, 1, 8192}, {"run", "revision"});
    provider.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path);
    file.Write("changed");
    EXPECT_THROW(provider.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path), std::runtime_error);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Rejected);
    EXPECT_FALSE(provider.Status(0).rejection_reason.empty());
    EXPECT_THROW(provider.Ready(0), std::logic_error);
    provider.Evict(0);
    EXPECT_THROW(provider.StagePrepared(0, ProviderInventory(), ProviderContext(), file.path + ".missing"), std::runtime_error);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Rejected);
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc"), std::invalid_argument);
}

TEST(test_fire_runtime, ProviderBoundsAndUnconsumedRevisionsAreExplicit) {
    EXPECT_THROW((cece::FireIntervalProvider(cece::FireInputProvider::Coupled, {0, 1, 1, 8192}, {"run", "r"})), std::invalid_argument);
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {2, 1, 1, 8192}, {"run", "r"});
    EXPECT_THROW(provider.Status(2), std::out_of_range);
    provider.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc");
    const auto old_key = provider.Status(0).cache_identity;
    auto context = ProviderContext();
    context.quality_rules.version = "2";
    provider.StageCoupled(0, ProviderInventory(), context, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, old_key);
    EXPECT_THROW(provider.Consume(0, old_key), std::invalid_argument);
    EXPECT_THROW(provider.StageCoupled(1, ProviderInventory(), context, "abc"), std::invalid_argument);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Ready);
    provider.StageCoupled(1, ProviderInventory(std::string(kAbcSha256), "2022-07-01T01:00:00Z", "2022-07-01T02:00:00Z"), context, "abc");
    EXPECT_THROW(provider.Consume(1, provider.Status(1).cache_identity), std::invalid_argument);
    provider.Consume(0, provider.Status(0).cache_identity);
    provider.Consume(1, provider.Status(1).cache_identity);
    EXPECT_EQ(provider.SlotCount(), 2);
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::logic_error);
    cece::FireIntervalProvider tiny(cece::FireInputProvider::Coupled, {1, 1, 1, 1}, {"r", "r"});
    EXPECT_THROW(tiny.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    EXPECT_EQ(tiny.Status(0).state, cece::FireIntervalState::Rejected);
}

TEST(test_fire_runtime, CacheIdentityIncludesPoliciesProfilesSourcesAndRevision) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    const auto original = ProviderContext();
    provider.StageCoupled(0, ProviderInventory(), original, "abc");
    const auto key = provider.Status(0).cache_identity;
    auto changed = original;
    changed.target_profile.content_sha256 = std::string(64, '1');
    provider.StageCoupled(0, ProviderInventory(), changed, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    changed = original;
    changed.source_manifest_sha256 = std::string(64, '2');
    provider.StageCoupled(0, ProviderInventory(), changed, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    changed = original;
    changed.product_time_allocation = cece::FireTimeAllocation::SourceResolved;
    provider.StageCoupled(0, ProviderInventory(), changed, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    changed = original;
    changed.scaling_profile = cece::FireProfileIdentity{"scaling", "1", std::string(64, '3')};
    provider.StageCoupled(0, ProviderInventory(), changed, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    provider.StageCoupled(0, ProviderInventory(std::string(kAbcSha256), "2022-07-01T00:00:00Z", "2022-07-01T01:00:00Z", "2022-07-03T00:00:00Z"),
                          original, "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    cece::FireIntervalProvider revision(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r2"});
    revision.StageCoupled(0, ProviderInventory(), original, "abc");
    EXPECT_NE(revision.Status(0).cache_identity, key);
}

TEST(test_fire_runtime, ProviderEnforcesCutoffMonotonicityAndRestoredCommitWatermark) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    provider.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc");
    provider.Consume(0, provider.Status(0).cache_identity);
    const auto checkpoint = provider.Checkpoint();
    provider.Evict(0);
    EXPECT_THROW(
        provider.StageCoupled(0, ProviderInventory(std::string(kAbcSha256), "2022-07-01T01:00:00Z", "2022-07-01T02:00:00Z", "2022-07-01T12:00:00Z"),
                              ProviderContext(), "abc"),
        std::invalid_argument);
    cece::FireIntervalProvider restarted(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"}, checkpoint);
    EXPECT_THROW(restarted.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc"), std::invalid_argument);
    EXPECT_THROW((cece::FireIntervalProvider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"different", "r"}, checkpoint)),
                 std::invalid_argument);
}

TEST(test_fire_runtime, CacheIdentityRecordsAbsentCompositionAndDerivationRequiresIt) {
    auto inventory_data = ProviderInventory().Data();
    inventory_data.component_profiles.clear();
    inventory_data.unprofiled_component_ids = {"component"};
    cece::FireBulkInventory inventory(std::move(inventory_data));

    cece::FireIntervalProvider product_provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    product_provider.StageCoupled(0, inventory, ProviderContext(), "abc");
    const auto unprofiled_key = product_provider.Status(0).cache_identity;
    cece::FireIntervalProvider profiled_provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    profiled_provider.StageCoupled(0, ProviderInventory(), ProviderContext(), "abc");
    EXPECT_NE(unprofiled_key, profiled_provider.Status(0).cache_identity);

    auto derive_context = ProviderContext();
    derive_context.mode = cece::FireInputMode::DeriveFrp;
    derive_context.product_time_allocation.reset();
    derive_context.scientific_profile = cece::FireProfileIdentity{"science", "1", std::string(64, 'e')};
    cece::FireIntervalProvider derive_provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    EXPECT_THROW(derive_provider.StageCoupled(0, inventory, derive_context, "abc"), std::invalid_argument);
    EXPECT_EQ(derive_provider.Status(0).state, cece::FireIntervalState::Rejected);
    EXPECT_NE(derive_provider.Status(0).rejection_reason.find("biome/composition identities"), std::string::npos);
}

TEST(test_fire_runtime, ProviderAcceptsOutOfOrderUnconsumedDeliveriesAndCanonicalizesSourceSets) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {2, 1, 1, 8192}, {"run", "r"});
    provider.StageCoupled(1, ProviderInventory(std::string(kAbcSha256), "2022-07-01T01:00:00Z", "2022-07-01T02:00:00Z"), ProviderContext(), "abc");
    auto data = ProviderInventory().Data();
    data.accepted_source_sha256.push_back(std::string(64, 'e'));
    provider.StageCoupled(0, cece::FireBulkInventory(data), ProviderContext(), "abc");
    const auto key = provider.Status(0).cache_identity;
    std::reverse(data.accepted_source_sha256.begin(), data.accepted_source_sha256.end());
    provider.StageCoupled(0, cece::FireBulkInventory(data), ProviderContext(), "abc");
    EXPECT_EQ(provider.Status(0).cache_identity, key);
    data.accepted_source_sha256[0] = std::string(64, 'f');
    provider.StageCoupled(0, cece::FireBulkInventory(data), ProviderContext(), "abc");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    data = ProviderInventory(cece::FireContentSha256("revised")).Data();
    provider.StageCoupled(0, cece::FireBulkInventory(data), ProviderContext(), "revised");
    EXPECT_NE(provider.Status(0).cache_identity, key);
    provider.Consume(0, provider.Status(0).cache_identity);
    provider.Consume(1, provider.Status(1).cache_identity);
}

TEST(test_fire_runtime, ProviderRejectsInvalidProfilesSchedulesAndLogicalPayloadBounds) {
    cece::FireIntervalProvider provider(cece::FireInputProvider::Coupled, {1, 1, 1, 8192}, {"run", "r"});
    auto context = ProviderContext();
    context.target_profile.content_sha256 = "not-a-hash";
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    context = ProviderContext();
    context.cutoff_schedule_sha256 = std::string(64, 'a');
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    context = ProviderContext();
    context.product_time_allocation = cece::FireTimeAllocation::Profile;
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    context.product_time_profile = cece::FireProfileIdentity{"temporal", "1", std::string(64, 'e')};
    provider.StageCoupled(0, ProviderInventory(), context, "abc");
    const auto profile_key = provider.Status(0).cache_identity;
    context.product_time_profile->version = "2";
    provider.StageCoupled(0, ProviderInventory(), context, "abc");
    EXPECT_NE(profile_key, provider.Status(0).cache_identity);
    context = ProviderContext();
    context.mode = cece::FireInputMode::DeriveFrp;
    context.product_time_allocation.reset();
    EXPECT_THROW(provider.StageCoupled(0, ProviderInventory(), context, "abc"), std::invalid_argument);
    context.scientific_profile = cece::FireProfileIdentity{"science", "1", std::string(64, 'e')};
    provider.StageCoupled(0, ProviderInventory(), context, "abc");
    auto data = ProviderInventory().Data();
    data.component_ids.push_back("extra");
    data.component_profiles.emplace("extra", data.component_profiles.at("component"));
    data.mass_kg.push_back(1.0);
    EXPECT_THROW(provider.StageCoupled(0, cece::FireBulkInventory(data), context, "abc"), std::invalid_argument);
    data = ProviderInventory().Data();
    auto grid = data.identity.Metadata().grid_definition->Definition();
    grid.nx = 2;
    grid.longitude_deg.push_back(0);
    grid.latitude_deg.push_back(0);
    grid.longitude_bounds_deg.push_back(grid.longitude_bounds_deg.front());
    grid.latitude_bounds_deg.push_back(grid.latitude_bounds_deg.front());
    grid.area_m2.push_back(25);
    auto identity = data.identity.Metadata();
    identity.grid_definition = std::make_shared<const cece::FireGrid>(std::move(grid));
    data.identity = cece::FireInputIdentity(std::move(identity));
    data.mass_kg.push_back(1);
    data.coverage_fraction.push_back(1);
    data.quality_state.push_back(cece::FireQualityState::Observed);
    EXPECT_THROW(provider.StageCoupled(0, cece::FireBulkInventory(data), context, "abc"), std::invalid_argument);
    EXPECT_EQ(provider.Status(0).state, cece::FireIntervalState::Rejected);
}

TEST(test_fire_runtime, PreparedHostCutoffsRequireScheduleIdentityAndCheckpointPairs) {
    ProviderFile file;
    cece::FireIntervalProvider provider(cece::FireInputProvider::Prepared, {1, 1, 1, 8192}, {"run", "r"});
    auto data = ProviderInventory().Data();
    auto identity = data.identity.Metadata();
    identity.cutoff_mode = cece::FireCutoffMode::HostSupplied;
    data.identity = cece::FireInputIdentity(identity);
    EXPECT_THROW(provider.StagePrepared(0, cece::FireBulkInventory(data), ProviderContext(), file.path), std::invalid_argument);
    auto context = ProviderContext();
    context.cutoff_schedule_sha256 = std::string(64, 'a');
    provider.StagePrepared(0, cece::FireBulkInventory(data), context, file.path);
    const auto key = provider.Status(0).cache_identity;
    context.cutoff_schedule_sha256 = std::string(64, 'b');
    provider.StagePrepared(0, cece::FireBulkInventory(data), context, file.path);
    EXPECT_NE(key, provider.Status(0).cache_identity);
    auto checkpoint = provider.Checkpoint();
    checkpoint.committed_end = cece::FireUtcTime("2022-07-01T01:00:00Z");
    EXPECT_THROW((cece::FireIntervalProvider(cece::FireInputProvider::Prepared, {1, 1, 1, 8192}, {"run", "r"}, checkpoint)), std::invalid_argument);
}

TEST(test_fire_runtime, FixtureDeviceRoundTripRetainsEachSpeciesBudget) {
    const auto grid = MakeGrid(2, 2, {25.0, 50.0, 75.0, 100.0});
    const auto areas = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace{}, grid.area_m2);
    EXPECT_DOUBLE_EQ(areas(1, 0), 75.0);
    std::vector<double> masses;
    for (double total : kExpectedMassKg) {
        for (double fraction : {0.1, 0.2, 0.3, 0.4}) {
            masses.push_back(total * fraction);
        }
    }
    const auto field = MakeMassField(grid, masses);
    for (std::size_t s = 0; s < kSpecies.size(); ++s) {
        EXPECT_NEAR(SpeciesBudgetKg(field, s), kExpectedMassKg[s], kRelativeTolerance * kExpectedMassKg[s]);
    }
}

TEST(test_fire_runtime, FixtureRejectsInvalidGridAndMassRatherThanFillingZeros) {
    EXPECT_THROW(MakeGrid(0, 1, {}), std::invalid_argument);
    EXPECT_THROW(MakeGrid(2, 2, {1.0}), std::invalid_argument);
    EXPECT_THROW(MakeGrid(1, 1, {0.0}), std::invalid_argument);
    EXPECT_THROW(MakeGrid(1, 1, {std::numeric_limits<double>::quiet_NaN()}), std::invalid_argument);
    const auto grid = MakeGrid(1, 1, {1.0});
    EXPECT_THROW(MakeMassField(grid, {}), std::invalid_argument);
    std::vector<double> masses(kSpecies.size(), 0.0);
    const auto zero = MakeMassField(grid, masses);
    EXPECT_NEAR(SpeciesBudgetKg(zero, 0), 0.0, kZeroAbsoluteTolerance);
    EXPECT_THROW(SpeciesBudgetKg(zero, kSpecies.size()), std::invalid_argument);
    masses[0] = -1.0;
    EXPECT_THROW(MakeMassField(grid, masses), std::invalid_argument);
    masses[0] = std::numeric_limits<double>::infinity();
    EXPECT_THROW(MakeMassField(grid, masses), std::invalid_argument);
}
}  // namespace
