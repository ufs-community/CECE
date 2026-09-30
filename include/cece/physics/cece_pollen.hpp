#ifndef CECE_POLLEN_HPP
#define CECE_POLLEN_HPP

#include <string>

#include "cece/physics_scheme.hpp"

namespace cece {

enum class AutumnPhenologyMethod { Rs1, Rs2, Rssig };

class PollenScheme : public BasePhysicsScheme {
   public:
    PollenScheme() = default;
    ~PollenScheme() override = default;

    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    AutumnPhenologyMethod phenology_method_ = AutumnPhenologyMethod::Rssig;
    double season_start_doy_ = 215.0;
    double season_end_doy_ = 280.0;
    double vegetation_fraction_default_ = 1.0;
    double gaussian_width_ = 4.0;
    double precipitation_low_mm_interval_ = 1.0e-5;
    double precipitation_high_mm_interval_ = 1.0;
    double rh_low_percent_ = 50.0;
    double rh_high_percent_ = 90.0;
    double temperature_threshold_c_ = 10.0;
    double temperature_slope_ = 0.35;
    double particle_diameter_m_ = 30.0e-6;
    double particle_density_kg_m3_ = 1000.0;
    double t_base_c_ = 20.0;
    double sunshine_base_hours_ = 12.0;
    double temperature_exponent_ = 1.0;
    double sunshine_exponent_ = 1.0;
    double sigmoid_a_ = 0.01;
    double sigmoid_b_ = 1.0;
    double phenology_threshold_ = 1.0;
    bool use_autumn_trigger_ = false;
};

}  // namespace cece

#endif  // CECE_POLLEN_HPP
