// SPDX-License-Identifier: Apache-2.0
// CECE — Chemical Emissions Coupling Engine
// Feature 001 — T023 [US3]: all outputs stay UTC and the run is idempotent.
//
// FR-009: the local-time service is an internal computation input only. This
// test runs the stacking engine with per-cell local-time factors that vary
// across the band and asserts that (a) the provenance record still carries the
// UTC hour / day-of-week / month that were passed in — never a local value —
// and (b) the per-cell product never leaks into the scalar provenance scale
// (the layer's base scale is recorded). It also checks idempotence: identical
// inputs replay to identical outputs.

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cece/cece_config.hpp"
#include "cece/cece_local_time.hpp"
#include "cece/cece_provenance.hpp"
#include "cece/cece_stacking_engine.hpp"

namespace cece {
namespace {

constexpr std::int64_t kHour = 3600;
// 2026-01-15T06:00:00Z — Thursday (dow 4), month 0.
constexpr std::int64_t kStart0600 = 1768456800;

class GridResolver : public FieldResolver {
    std::map<std::string, DualView3D> fields_;

   public:
    void AddField(const std::string& name, int nx, int ny, int nz, double fill = 0.0) {
        fields_.try_emplace(name, "test_" + name, nx, ny, nz);
        if (fill != 0.0) SetAll(name, fill);
    }
    void SetAll(const std::string& name, double val) {
        auto h = fields_[name].view_host();
        Kokkos::deep_copy(h, val);
        fields_[name].modify_host();
        fields_[name].sync_device();
    }
    std::vector<double> Snapshot(const std::string& name, int nx) {
        fields_[name].sync_host();
        auto h = fields_[name].view_host();
        std::vector<double> v(static_cast<size_t>(nx));
        for (int i = 0; i < nx; ++i) v[i] = h(i, 0, 0);
        return v;
    }
    UnmanagedHostView3D ResolveImport(const std::string& n, int, int, int) override {
        return fields_[n].view_host();
    }
    UnmanagedHostView3D ResolveExport(const std::string& n, int, int, int) override {
        return fields_[n].view_host();
    }
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveImportDevice(const std::string& n, int, int,
                                                                                                         int) override {
        return fields_[n].view_device();
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> ResolveExportDevice(const std::string& n, int, int, int) override {
        return fields_[n].view_device();
    }
};

class FixedOffsetProvider final : public IUtcOffsetProvider {
   public:
    std::int32_t offsetSecondsAt(double, double, std::int64_t) const override {
        return 5 * kHour;
    }  // +5h everywhere
};

/// Longitude-tabled offset provider (the US4-style seam): offsets chosen per
/// cell by matching the cell's longitude against a small table.
class TableOffsetProvider final : public IUtcOffsetProvider {
   public:
    TableOffsetProvider(std::vector<double> lons, std::vector<std::int32_t> offsets_secs)
        : lons_(std::move(lons)), offsets_(std::move(offsets_secs)) {}
    std::int32_t offsetSecondsAt(double, double lon, std::int64_t) const override {
        for (size_t i = 0; i < lons_.size(); ++i) {
            if (std::fabs(lons_[i] - lon) < 1e-6) return offsets_[i];
        }
        return 0;
    }

   private:
    std::vector<double> lons_;
    std::vector<std::int32_t> offsets_;
};

CeceConfig MakeConfig() {
    CeceConfig config;
    TemporalCycle cyc;
    for (int i = 0; i < 24; ++i) cyc.factors.push_back(static_cast<double>(i));
    config.temporal_profiles["hour_id"] = cyc;
    EmissionLayer layer;
    layer.operation = "add";
    layer.field_name = "f1";
    layer.scale = 3.0;  // non-trivial base scale to check it is what provenance records
    layer.diurnal_cycle = "hour_id";
    layer.use_local_time = true;
    config.species_layers["SP"] = {layer};
    return config;
}

class LocalTimeUtcOutputsTest : public ::testing::Test {
   protected:
    void SetUp() override {
        if (!Kokkos::is_initialized()) Kokkos::initialize();
    }
};

// ---------------------------------------------------------------------------
// FR-009 — provenance timestamps remain UTC despite local-time scaling
// ---------------------------------------------------------------------------

TEST_F(LocalTimeUtcOutputsTest, ProvenanceRecordsUtcTimeNotLocal) {
    auto config = MakeConfig();
    GridResolver r;
    r.AddField("f1", 2, 1, 1, 10.0);
    r.AddField("SP", 2, 1, 1, 0.0);

    auto svc = LocalTimeService::Create(std::make_unique<FixedOffsetProvider>(), {0.0, 1.0}, {40.0}, 2, 0, 1, kStart0600);
    StackingEngine engine(config);
    // Pass UTC hour=6, dow=4, month=0. Local hour at +5h is 11; provenance
    // must record the UTC 6, never 11.
    engine.Execute(r, 2, 1, 1, {}, 6, 4, 0, nullptr, svc.get(), 0);

    const auto* prov = engine.GetProvenance().GetProvenance("SP");
    ASSERT_NE(prov, nullptr);
    EXPECT_EQ(prov->last_hour, 6);         // UTC, not local (11)
    EXPECT_EQ(prov->last_day_of_week, 4);  // UTC
    EXPECT_EQ(prov->last_month, 0);        // UTC
    // The scalar effective scale recorded is the layer base (3.0): the per-cell
    // diurnal product (D[11]=11) lives only in the factor field, never provenance.
    ASSERT_EQ(prov->contributions.size(), 1u);
    EXPECT_DOUBLE_EQ(prov->contributions[0].effective_scale, 3.0);

    // Meanwhile the EXPORT field genuinely used local time: 10 * 3 * D[11] = 330.
    auto out = r.Snapshot("SP", 2);
    EXPECT_NEAR(out[0], 330.0, 1e-9);
    EXPECT_NEAR(out[1], 330.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Idempotence — identical inputs replay to identical outputs
// ---------------------------------------------------------------------------

TEST_F(LocalTimeUtcOutputsTest, LocalTimeRunIsIdempotent) {
    auto run_once = []() {
        auto config = MakeConfig();
        GridResolver r;
        r.AddField("f1", 3, 1, 1, 2.0);
        r.AddField("SP", 3, 1, 1, 0.0);
        std::vector<double> lons = {-74.0, 0.0, 139.7};
        std::vector<std::int32_t> offs = {-8 * kHour, 0, 12 * kHour};
        auto svc = LocalTimeService::Create(std::make_unique<TableOffsetProvider>(lons, offs), lons, {40.0}, 3, 0, 1, kStart0600);
        StackingEngine engine(config);
        engine.Execute(r, 3, 1, 1, {}, 6, 4, 0, nullptr, svc.get(), 0);
        return r.Snapshot("SP", 3);
    };

    const auto a = run_once();
    const auto b = run_once();
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) EXPECT_DOUBLE_EQ(a[i], b[i]) << "cell " << i;
    // Sanity: the values are genuinely local-time dependent — 06:00Z at -8h =>
    // local 22, at 0 => 6, at +12h => 18 (identity profile, base 3, field 2).
    EXPECT_NEAR(a[0], 2.0 * 3.0 * 22.0, 1e-9);
    EXPECT_NEAR(a[1], 2.0 * 3.0 * 6.0, 1e-9);
    EXPECT_NEAR(a[2], 2.0 * 3.0 * 18.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Disabled => byte-identical to the pure UTC scalar path (SC-001)
// ---------------------------------------------------------------------------

TEST_F(LocalTimeUtcOutputsTest, DisabledOutputBitIdenticalToUtcBaseline) {
    auto config = MakeConfig();

    GridResolver on;
    on.AddField("f1", 2, 1, 1, 10.0);
    on.AddField("SP", 2, 1, 1, 0.0);
    StackingEngine engine_on(config);
    engine_on.Execute(on, 2, 1, 1, {}, 6, 4, 0, nullptr, nullptr, 0);  // no service => UTC scalar

    GridResolver off;
    off.AddField("f1", 2, 1, 1, 10.0);
    off.AddField("SP", 2, 1, 1, 0.0);
    auto utc_config = config;
    utc_config.species_layers["SP"][0].use_local_time = false;  // fully disabled
    StackingEngine engine_off(utc_config);
    engine_off.Execute(off, 2, 1, 1, {}, 6, 4, 0, nullptr, nullptr, 0);

    const auto a = on.Snapshot("SP", 2);
    const auto b = off.Snapshot("SP", 2);
    for (size_t i = 0; i < a.size(); ++i) EXPECT_DOUBLE_EQ(a[i], b[i]) << "cell " << i;
    EXPECT_NEAR(a[0], 10.0 * 3.0 * 6.0, 1e-12);  // D[6] UTC scalar
}

}  // namespace
}  // namespace cece
