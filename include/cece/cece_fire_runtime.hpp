// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "cece/cece_config.hpp"
#include "cece/physics/cece_fire.hpp"

namespace cece {

struct FireRuntimeIntervalRequest {
    FireUtcTime interval_start;
    FireUtcTime interval_end;
    std::optional<FireUtcTime> fixed_cutoff;
    std::int64_t elapsed_end_seconds;
};

using FireRuntimeInventoryView =
    std::variant<std::reference_wrapper<const FireBulkInventory>, std::reference_wrapper<const FireTargetSpecificInventory>>;

/**
 * Provider-specific I/O and metadata handling for the shared fire runtime.
 *
 * Implementations own their bounded provider window and return borrowed
 * inventories that remain valid until CommitInterval() or AbortInterval().
 * PrepareInterval() runs in the core run phase before FireScheme::Run; file,
 * network and host-field access must not be placed in FireScheme::Run.
 */
class FireRuntimeAdapter {
   public:
    virtual ~FireRuntimeAdapter() = default;

    virtual void SetTargetGrid(std::shared_ptr<const FireGrid> target_grid) {
        (void)target_grid;
    }
    virtual void Initialize(const FireConfig& config, int nx, int ny, int nz) = 0;
    virtual FireRuntimeInventoryView PrepareInterval(const FireRuntimeIntervalRequest& request) = 0;
    virtual void CommitInterval() = 0;
    virtual void AbortInterval() noexcept = 0;
    virtual void Finalize() = 0;
};

using FireRuntimeAdapterFactory = std::function<std::unique_ptr<FireRuntimeAdapter>()>;

void RegisterFireRuntimeAdapter(FireInputProvider provider, FireRuntimeAdapterFactory factory);
std::unique_ptr<FireRuntimeAdapter> CreateFireRuntimeAdapter(FireInputProvider provider);
void ValidateFireExportDescriptors(const CeceConfig& config, const FireScheme& scheme);

}  // namespace cece
