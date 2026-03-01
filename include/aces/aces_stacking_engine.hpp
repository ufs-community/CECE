#ifndef ACES_STACKING_ENGINE_HPP
#define ACES_STACKING_ENGINE_HPP

#include <Kokkos_Core.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aces/aces_compute.hpp"
#include "aces/aces_config.hpp"

namespace aces {

/**
 * @brief Alias for an unmanaged 3D device View, safe for use in POD-like structures
 * that are deep-copied between host and device.
 */
using UnmanagedDeviceView3D =
    Kokkos::View<const double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace,
                 Kokkos::MemoryTraits<Kokkos::Unmanaged>>;

/**
 * @struct DeviceLayer
 * @brief POD-like structure containing unmanaged device-side View handles.
 *
 * @details By using Unmanaged views, this struct becomes trivially copyable,
 * which is a requirement for safe transfer to the device via Kokkos::deep_copy.
 * Ownership of the underlying memory must be maintained by the StackingEngine
 * or the FieldResolver during the kernel execution.
 */
struct DeviceLayer {
    /// Primary emission field View.
    UnmanagedDeviceView3D field;
    /// Constant scaling factor (combined with temporal cycles).
    double scale;
    /// 1.0 for "replace", 0.0 for "add".
    double replace_flag;

    /// Capacity matches the original requirements.
    static constexpr int MAX_SCALES = 16;
    Kokkos::Array<UnmanagedDeviceView3D, MAX_SCALES> scales;
    int num_scales;

    /// Capacity matches the original requirements.
    static constexpr int MAX_MASKS = 8;
    Kokkos::Array<UnmanagedDeviceView3D, MAX_MASKS> masks;
    int num_masks;
};

/**
 * @class StackingEngine
 * @brief Modernized, high-performance engine for stacking emission layers using fused Kokkos
 * kernels.
 */
class StackingEngine {
   public:
    explicit StackingEngine(const AcesConfig& config);

    /**
     * @brief Executes the emission stacking for all species.
     */
    void Execute(
        FieldResolver& resolver, int nx, int ny, int nz,
        Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> default_mask,
        int hour, int day_of_week);

   private:
    struct CompiledLayer {
        std::string field_name;
        std::string operation;
        double base_scale;
        int hierarchy;
        std::vector<std::string> masks;
        std::vector<std::string> scale_fields;
        std::string diurnal_cycle;
        std::string weekly_cycle;
    };

    struct CompiledSpecies {
        std::string name;
        std::vector<CompiledLayer> layers;
        /// Device-side storage for layer handles.
        Kokkos::View<DeviceLayer*, Kokkos::DefaultExecutionSpace> device_layers;
        /// Persistent host-side mirror to avoid redundant allocations.
        typename Kokkos::View<DeviceLayer*, Kokkos::DefaultExecutionSpace>::HostMirror host_layers;
    };

    AcesConfig m_config;
    std::vector<CompiledSpecies> m_compiled;

    void PreCompile();
    void BindSpecies(CompiledSpecies& spec, FieldResolver& resolver, int nx, int ny, int nz,
                     int hour, int day_of_week);
};

}  // namespace aces

#endif  // ACES_STACKING_ENGINE_HPP
