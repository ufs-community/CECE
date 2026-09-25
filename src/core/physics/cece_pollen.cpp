#include "cece/physics/cece_pollen.hpp"

#include <Kokkos_Core.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "cece/cece_physics_factory.hpp"

namespace cece {
namespace {

KOKKOS_INLINE_FUNCTION
double linear_suppression(double value, double low, double high) {
    if (value <= low) return 1.0;
    if (value >= high) return 0.0;
    return (high - value) / (high - low);
}

KOKKOS_INLINE_FUNCTION
double autumn_forcing(AutumnPhenologyMethod method, double temperature_c, double sunshine_hours, double t_base_c, double sunshine_base_hours,
                      double temperature_exponent, double sunshine_exponent, double sigmoid_a, double sigmoid_b) {
    if (method == AutumnPhenologyMethod::Rssig) {
        return 1.0 / (1.0 + std::exp(sigmoid_a * temperature_c * sunshine_hours - sigmoid_b));
    }
    if (temperature_c >= t_base_c || sunshine_hours >= sunshine_base_hours) return 0.0;

    const double temperature_term = std::pow(t_base_c - temperature_c, temperature_exponent);
    if (method == AutumnPhenologyMethod::Rs1) {
        return temperature_term * std::pow(sunshine_hours / sunshine_base_hours, sunshine_exponent);
    }
    return temperature_term * std::pow(1.0 - sunshine_hours / sunshine_base_hours, sunshine_exponent);
}

}  // namespace

static PhysicsRegistration<PollenScheme> register_scheme("pollen");
static PhysicsRegistration<PollenScheme> register_artemisia_scheme("pollen_artemisia");
static PhysicsRegistration<PollenScheme> register_mugwort_scheme("pollen_mugwort");
static PhysicsRegistration<PollenScheme> register_chenopod_scheme("pollen_chenopod");
static PhysicsRegistration<PollenScheme> register_ragweed_scheme("pollen_ragweed");
static PhysicsRegistration<PollenScheme> register_grass_scheme("pollen_grass");
static PhysicsRegistration<PollenScheme> register_alder_scheme("pollen_alder");
static PhysicsRegistration<PollenScheme> register_ash_scheme("pollen_ash");
static PhysicsRegistration<PollenScheme> register_birch_scheme("pollen_birch");
static PhysicsRegistration<PollenScheme> register_cottonwood_scheme("pollen_cottonwood");
static PhysicsRegistration<PollenScheme> register_cypress_scheme("pollen_cypress");
static PhysicsRegistration<PollenScheme> register_elm_scheme("pollen_elm");
static PhysicsRegistration<PollenScheme> register_hazel_scheme("pollen_hazel");
static PhysicsRegistration<PollenScheme> register_juniper_scheme("pollen_juniper");
static PhysicsRegistration<PollenScheme> register_maple_scheme("pollen_maple");
static PhysicsRegistration<PollenScheme> register_oak_scheme("pollen_oak");
static PhysicsRegistration<PollenScheme> register_olive_scheme("pollen_olive");
static PhysicsRegistration<PollenScheme> register_pine_scheme("pollen_pine");
static PhysicsRegistration<PollenScheme> register_plane_scheme("pollen_plane");
static PhysicsRegistration<PollenScheme> register_nettle_scheme("pollen_nettle");
static PhysicsRegistration<PollenScheme> register_total_scheme("pollen_total");

void PollenScheme::Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) {
    BasePhysicsScheme::Initialize(config, diag_manager);

    const std::string method = config["phenology_method"].string_or("rssig");
    if (method == "rs1") {
        phenology_method_ = AutumnPhenologyMethod::Rs1;
    } else if (method == "rs2") {
        phenology_method_ = AutumnPhenologyMethod::Rs2;
    } else if (method == "rssig") {
        phenology_method_ = AutumnPhenologyMethod::Rssig;
    } else {
        throw std::invalid_argument("PollenScheme: phenology_method must be rs1, rs2, or rssig");
    }

    season_start_doy_ = config["season_start_doy"].double_or(season_start_doy_);
    season_end_doy_ = config["season_end_doy"].double_or(season_end_doy_);
    vegetation_fraction_default_ = config["vegetation_fraction_default"].double_or(vegetation_fraction_default_);
    gaussian_width_ = config["gaussian_width"].double_or(gaussian_width_);
    precipitation_low_mm_interval_ = config["precipitation_low_mm_interval"].double_or(precipitation_low_mm_interval_);
    precipitation_high_mm_interval_ = config["precipitation_high_mm_interval"].double_or(precipitation_high_mm_interval_);
    rh_low_percent_ = config["rh_low_percent"].double_or(rh_low_percent_);
    rh_high_percent_ = config["rh_high_percent"].double_or(rh_high_percent_);
    temperature_threshold_c_ = config["temperature_threshold_c"].double_or(temperature_threshold_c_);
    temperature_slope_ = config["temperature_slope"].double_or(temperature_slope_);
    particle_diameter_m_ = config["particle_diameter_um"].double_or(30.0) * 1.0e-6;
    particle_density_kg_m3_ = config["particle_density_kg_m3"].double_or(particle_density_kg_m3_);
    t_base_c_ = config["autumn_temperature_base_c"].double_or(t_base_c_);
    sunshine_base_hours_ = config["autumn_sunshine_base_hours"].double_or(sunshine_base_hours_);
    temperature_exponent_ = config["autumn_temperature_exponent"].double_or(temperature_exponent_);
    sunshine_exponent_ = config["autumn_sunshine_exponent"].double_or(sunshine_exponent_);
    sigmoid_a_ = config["autumn_sigmoid_a"].double_or(sigmoid_a_);
    sigmoid_b_ = config["autumn_sigmoid_b"].double_or(sigmoid_b_);
    phenology_threshold_ = config["autumn_threshold"].double_or(phenology_threshold_);
    use_autumn_trigger_ = config["use_autumn_trigger"].bool_or(use_autumn_trigger_);

    if (season_end_doy_ <= season_start_doy_ || gaussian_width_ <= 0.0) {
        throw std::invalid_argument("PollenScheme: season_end_doy must exceed season_start_doy and gaussian_width must be positive");
    }
    if (vegetation_fraction_default_ < 0.0 || vegetation_fraction_default_ > 1.0) {
        throw std::invalid_argument("PollenScheme: vegetation_fraction_default must be in [0, 1]");
    }
    if (precipitation_high_mm_interval_ <= precipitation_low_mm_interval_ || rh_high_percent_ <= rh_low_percent_) {
        throw std::invalid_argument("PollenScheme: meteorological high thresholds must exceed low thresholds");
    }
    if (particle_diameter_m_ <= 0.0 || particle_density_kg_m3_ <= 0.0 || sunshine_base_hours_ <= 0.0) {
        throw std::invalid_argument("PollenScheme: particle properties and autumn_sunshine_base_hours must be positive");
    }
}

void PollenScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    auto day_of_year = ResolveImport("day_of_year", import_state);
    auto vegetation_fraction = ResolveImport("vegetation_fraction", import_state);
    auto annual_production = ResolveImport("annual_pollen_production", import_state);
    auto wind_speed = ResolveImport("wind_speed", import_state);
    auto convective_velocity = ResolveImport("convective_velocity", import_state);
    auto precipitation = ResolveImport("precipitation", import_state);
    auto relative_humidity = ResolveImport("relative_humidity", import_state);
    auto temperature = ResolveImport("temperature", import_state);
    auto sunshine_hours = ResolveImport("sunshine_hours", import_state);
    auto season_start = ResolveImport("season_start_doy", import_state);
    auto season_end = ResolveImport("season_end_doy", import_state);
    auto accumulated_forcing = ResolveImport("autumn_accumulated_forcing", import_state);

    auto number_flux = ResolveExport("pollen_number_emissions", export_state);
    auto mass_flux = ResolveExport("pollen_mass_emissions", export_state);
    auto diameter = ResolveExport("pollen_diameter", export_state);
    auto density = ResolveExport("pollen_density", export_state);
    auto forcing_diagnostic = ResolveExport("pollen_phenology_forcing", export_state);

    RequireFields("PollenScheme::Run", {{MapInput("day_of_year"), day_of_year.data() != nullptr},
                                        {MapInput("annual_pollen_production"), annual_production.data() != nullptr},
                                        {MapInput("wind_speed"), wind_speed.data() != nullptr},
                                        {MapInput("convective_velocity"), convective_velocity.data() != nullptr},
                                        {MapInput("precipitation"), precipitation.data() != nullptr},
                                        {MapInput("relative_humidity"), relative_humidity.data() != nullptr},
                                        {MapInput("temperature"), temperature.data() != nullptr},
                                        {MapInput("sunshine_hours"), sunshine_hours.data() != nullptr},
                                        {MapOutput("pollen_number_emissions"), number_flux.data() != nullptr},
                                        {MapOutput("pollen_mass_emissions"), mass_flux.data() != nullptr}});
    if (use_autumn_trigger_ && accumulated_forcing.data() == nullptr) {
        throw std::runtime_error("PollenScheme::Run missing required field '" + MapInput("autumn_accumulated_forcing") + "'");
    }

    const int nx = static_cast<int>(number_flux.extent(0));
    const int ny = static_cast<int>(number_flux.extent(1));
    const double default_start = season_start_doy_;
    const double default_end = season_end_doy_;
    const double default_vegetation_fraction = vegetation_fraction_default_;
    const double width = gaussian_width_;
    const double precipitation_low = precipitation_low_mm_interval_;
    const double precipitation_high = precipitation_high_mm_interval_;
    const double rh_low = rh_low_percent_;
    const double rh_high = rh_high_percent_;
    const double temperature_threshold = temperature_threshold_c_;
    const double temperature_slope = temperature_slope_;
    const double diameter_m = particle_diameter_m_;
    const double density_kg_m3 = particle_density_kg_m3_;
    const double grain_mass = std::numbers::pi / 6.0 * diameter_m * diameter_m * diameter_m * density_kg_m3;
    const double t_base = t_base_c_;
    const double sunshine_base = sunshine_base_hours_;
    const double temperature_exponent = temperature_exponent_;
    const double sunshine_exponent = sunshine_exponent_;
    const double sigmoid_a = sigmoid_a_;
    const double sigmoid_b = sigmoid_b_;
    const double trigger_threshold = phenology_threshold_;
    const auto method = phenology_method_;
    const bool use_trigger = use_autumn_trigger_;
    const bool has_start = season_start.data() != nullptr;
    const bool has_end = season_end.data() != nullptr;
    const bool has_vegetation_fraction = vegetation_fraction.data() != nullptr;
    const bool has_diameter = diameter.data() != nullptr;
    const bool has_density = density.data() != nullptr;
    const bool has_forcing = forcing_diagnostic.data() != nullptr;

    Kokkos::parallel_for(
        "Pollen_Emissions", Kokkos::MDRangePolicy<Kokkos::DefaultExecutionSpace, Kokkos::Rank<2>>({0, 0}, {nx, ny}), KOKKOS_LAMBDA(int i, int j) {
            const double doy = day_of_year(i, j, 0);
            const double start = has_start ? season_start(i, j, 0) : default_start;
            const double end = has_end ? season_end(i, j, 0) : default_end;
            const double mean = 0.5 * (start + end);
            const double sigma = (end - start) / width;
            const double temperature_c = temperature(i, j, 0) - 273.15;
            const double current_forcing = autumn_forcing(method, temperature_c, sunshine_hours(i, j, 0), t_base, sunshine_base, temperature_exponent,
                                                          sunshine_exponent, sigmoid_a, sigmoid_b);

            double flux = 0.0;
            if (sigma > 0.0 && doy >= start && doy <= end && (!use_trigger || accumulated_forcing(i, j, 0) >= trigger_threshold)) {
                const double z = (doy - mean) / sigma;
                const double vegetation = has_vegetation_fraction ? vegetation_fraction(i, j, 0) : default_vegetation_fraction;
                const double pollen_pool = vegetation * annual_production(i, j, 0) * std::exp(-0.5 * z * z);
                const double wind_factor = 1.5 - std::exp(-(std::max(0.0, wind_speed(i, j, 0)) + std::max(0.0, convective_velocity(i, j, 0))) / 5.0);
                const double rain_factor = linear_suppression(precipitation(i, j, 0), precipitation_low, precipitation_high);
                const double humidity_factor = linear_suppression(relative_humidity(i, j, 0), rh_low, rh_high);
                const double temperature_factor = 1.0 / (1.0 + std::exp(-temperature_slope * (temperature_c - temperature_threshold)));
                flux = pollen_pool * wind_factor * rain_factor * humidity_factor * temperature_factor / 86400.0;
            }

            number_flux(i, j, 0) = flux;
            mass_flux(i, j, 0) = flux * grain_mass;
            if (has_diameter) diameter(i, j, 0) = diameter_m;
            if (has_density) density(i, j, 0) = density_kg_m3;
            if (has_forcing) forcing_diagnostic(i, j, 0) = current_forcing;
        });

    MarkModified("pollen_number_emissions", export_state);
    MarkModified("pollen_mass_emissions", export_state);
    if (has_diameter) MarkModified("pollen_diameter", export_state);
    if (has_density) MarkModified("pollen_density", export_state);
    if (has_forcing) MarkModified("pollen_phenology_forcing", export_state);
    Kokkos::fence();
}

}  // namespace cece
