#ifndef ACES_MEGAN_HPP
#define ACES_MEGAN_HPP

#include "aces/physics_scheme.hpp"

namespace aces {

/**
 * @class MeganScheme
 * @brief Native C++ implementation of the MEGAN biogenics emission scheme.
 */
class MeganScheme : public BasePhysicsScheme {
   public:
    MeganScheme() = default;
    ~MeganScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override;
    void Run(AcesImportState& import_state, AcesExportState& export_state) override;

   private:
    double gamma_co2_ = 0.0;
    double beta_ = 0.13;
    double ct1_ = 95.0;
    double ceo_ = 2.0;
    double ldf_ = 1.0;

    // Configurable list of species and their emission factors (AEF)
    std::string species_name_ = "isoprene";
    std::string export_field_name_ = "isoprene_emissions";
    double aef_ = 1.0e-9;

    double lai_coeff_1_ = 0.49;
    double lai_coeff_2_ = 0.2;
    double standard_temp_ = 303.0;
    double gas_constant_ = 8.3144598e-3;
    double ct2_const_ = 200.0;
    double t_opt_coeff_1_ = 313.0;
    double t_opt_coeff_2_ = 0.6;
    double e_opt_coeff_ = 0.08;
    double wm2_to_umolm2s_ = 4.766;
    double ptoa_coeff_1_ = 3000.0;
    double ptoa_coeff_2_ = 99.0;
    double gamma_p_coeff_1_ = 1.0;
    double gamma_p_coeff_2_ = 0.0005;
    double gamma_p_coeff_3_ = 2.46;
    double gamma_p_coeff_4_ = 0.9;
    double gamma_co2_coeff_1_ = 8.9406;
    double gamma_co2_coeff_2_ = 0.0024;

    // Additional parameters for gamma_age
    double anew_ = 1.0;

    // Additional parameter for gamma_sm
    bool is_ald2_or_eoh_ = false;
    double agro_ = 1.0;
    double amat_ = 1.0;
    double aold_ = 1.0;
    bool is_bidirectional_ = false;
    bool use_wilkinson_ = false;
};

}  // namespace aces

#endif  // ACES_MEGAN_HPP
