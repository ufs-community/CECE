#include "aces/aces_compute.hpp"
#include <Kokkos_Core.hpp>
#include <iostream>

namespace aces {

void ComputeEmissions(
    const AcesConfig& config,
    FieldResolver& resolver,
    int nx, int ny, int nz
) {
    // Create a 1.0 mask view for layers without an explicit mask.
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::HostSpace> default_mask("default_mask", nx, ny, nz);
    Kokkos::deep_copy(default_mask, 1.0);

    for (auto const& [species, layers] : config.species_layers) {
        std::string export_name = "total_" + species + "_emissions";
        auto total_view = resolver.ResolveExport(export_name, nx, ny, nz);

        if (total_view.data() == nullptr) {
            std::cerr << "ACES_Compute: Warning - Could not resolve export field " << export_name << std::endl;
            continue;
        }

        // Initialize export field to 0 before accumulating layers.
        Kokkos::deep_copy(total_view, 0.0);

        for (auto const& layer : layers) {
            auto field_view = resolver.ResolveImport(layer.field_name, nx, ny, nz);
            if (field_view.data() == nullptr) {
                std::cerr << "ACES_Compute: Warning - Could not resolve input field " << layer.field_name << std::endl;
                continue;
            }

            UnmanagedHostView3D mask_view;
            if (!layer.mask_name.empty()) {
                mask_view = resolver.ResolveImport(layer.mask_name, nx, ny, nz);
                if (mask_view.data() == nullptr) {
                    std::cerr << "ACES_Compute: Warning - Could not resolve mask " << layer.mask_name << ", using default 1.0" << std::endl;
                    mask_view = default_mask;
                }
            } else {
                mask_view = default_mask;
            }

            double replace_flag = (layer.operation == "replace") ? 1.0 : 0.0;
            double scale = layer.scale;

            Kokkos::parallel_for("EmissionLayerKernel",
                Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {nx, ny, nz}),
                KOKKOS_LAMBDA(int i, int j, int k) {
                    total_view(i, j, k) = total_view(i, j, k) * (1.0 - replace_flag * mask_view(i, j, k)) +
                                          field_view(i, j, k) * scale * mask_view(i, j, k);
                }
            );
        }
    }
    Kokkos::fence();
}

} // namespace aces
