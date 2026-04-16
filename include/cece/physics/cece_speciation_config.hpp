#ifndef CECE_SPECIATION_CONFIG_HPP
#define CECE_SPECIATION_CONFIG_HPP

/**
 * @file cece_speciation_config.hpp
 * @brief Data structures and loader for chemical mechanism speciation configuration.
 *
 * Defines the 19 MEGAN3 emission classes, speciation data structures (MechanismSpecies,
 * SpeciationMapping, SpeciationConfig), and the SpeciationConfigLoader class that reads
 * mechanism species from MICM/OpenAtmos configuration files and emission speciation
 * mappings from CECE-specific dataset-oriented YAML files.
 *
 * Two-tier design:
 *   1. Mechanism file (MICM/OpenAtmos format): species names + molecular weights
 *   2. Speciation mapping file (CECE dataset-oriented format): emission class → mechanism
 *      species with per-class scale factors
 *
 * The MAP file uses a nested structure where each dataset (e.g., MEGAN, CEDS_TRA) maps
 * mechanism species to their contributing emission classes with per-class scale factors.
 * This eliminates the previous 201-speciated-species intermediate step.
 */

#include <yaml-cpp/yaml.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace cece {

/**
 * @enum EmissionClass
 * @brief The 19 MEGAN3 biogenic emission categories.
 */
enum class EmissionClass : int {
    ISOP = 0,
    MBO,
    MT_PINE,
    MT_ACYC,
    MT_CAMP,
    MT_SABI,
    MT_AROM,
    NO,
    SQT_HR,
    SQT_LR,
    MEOH,
    ACTO,
    ETOH,
    ACID,
    LVOC,
    OXPROD,
    STRESS,
    OTHER,
    CO,
    COUNT = 19
};

/**
 * @brief Converts an EmissionClass enum value to its string name.
 * @param ec The emission class.
 * @return The string name (e.g., "ISOP", "MBO", etc.).
 */
inline std::string EmissionClassToString(EmissionClass ec) {
    static const char* names[] = {"ISOP",   "MBO",    "MT_PINE", "MT_ACYC", "MT_CAMP",
                                  "MT_SABI", "MT_AROM", "NO",     "SQT_HR",  "SQT_LR",
                                  "MEOH",   "ACTO",   "ETOH",    "ACID",    "LVOC",
                                  "OXPROD", "STRESS", "OTHER",   "CO"};
    int idx = static_cast<int>(ec);
    if (idx >= 0 && idx < static_cast<int>(EmissionClass::COUNT)) {
        return names[idx];
    }
    return "UNKNOWN";
}

/**
 * @brief Converts a string name to an EmissionClass enum value.
 * @param name The emission class name (e.g., "ISOP", "MBO", etc.).
 * @param[out] ec The resulting emission class.
 * @return True if the name was recognized, false otherwise.
 */
inline bool StringToEmissionClass(const std::string& name, EmissionClass& ec) {
    static const std::unordered_map<std::string, EmissionClass> lookup = {
        {"ISOP", EmissionClass::ISOP},       {"MBO", EmissionClass::MBO},
        {"MT_PINE", EmissionClass::MT_PINE}, {"MT_ACYC", EmissionClass::MT_ACYC},
        {"MT_CAMP", EmissionClass::MT_CAMP}, {"MT_SABI", EmissionClass::MT_SABI},
        {"MT_AROM", EmissionClass::MT_AROM}, {"NO", EmissionClass::NO},
        {"SQT_HR", EmissionClass::SQT_HR},  {"SQT_LR", EmissionClass::SQT_LR},
        {"MEOH", EmissionClass::MEOH},       {"ACTO", EmissionClass::ACTO},
        {"ETOH", EmissionClass::ETOH},       {"ACID", EmissionClass::ACID},
        {"LVOC", EmissionClass::LVOC},       {"OXPROD", EmissionClass::OXPROD},
        {"STRESS", EmissionClass::STRESS},   {"OTHER", EmissionClass::OTHER},
        {"CO", EmissionClass::CO},
    };
    auto it = lookup.find(name);
    if (it != lookup.end()) {
        ec = it->second;
        return true;
    }
    return false;
}

/**
 * @struct MechanismSpecies
 * @brief A chemical species defined by a specific chemical mechanism.
 *
 * Parsed from MICM/OpenAtmos mechanism configuration files.
 * The molecular weight uses MICM convention (kg/mol) internally but
 * is stored as g/mol for consistency with CMAQ speciation factors.
 */
struct MechanismSpecies {
    std::string name;           ///< Species name (e.g., "ISOP", "TERP", "PAR")
    double molecular_weight;    ///< Molecular weight in g/mol
};

/**
 * @struct SpeciationMapping
 * @brief A single emission-class-to-mechanism-species mapping with a scale factor.
 *
 * Parsed from CECE dataset-oriented speciation mapping YAML files.
 * Each entry represents the fractional contribution of one emission class
 * to one mechanism species.
 */
struct SpeciationMapping {
    std::string mechanism_species;   ///< Target mechanism species name (e.g., "ISOP", "TERP")
    EmissionClass emission_class;    ///< Source emission class (e.g., EmissionClass::MT_PINE)
    double scale_factor;             ///< Fractional contribution (positive)
};

/**
 * @struct SpeciationConfig
 * @brief Complete speciation configuration assembled from mechanism and mapping files.
 *
 * Combines mechanism species definitions (from MICM file) with emission speciation
 * mappings (from CECE dataset-oriented mapping file) into a single validated configuration.
 */
struct SpeciationConfig {
    std::string mechanism_name;                  ///< Mechanism identifier (e.g., "CB6_AE7")
    std::vector<MechanismSpecies> species;       ///< Mechanism species from MICM file
    std::vector<SpeciationMapping> mappings;      ///< Class→mechanism mappings with scale factors
    std::string dataset_name;                    ///< Which dataset was loaded (e.g., "MEGAN")
};

/**
 * @class SpeciationConfigLoader
 * @brief Loads speciation configuration from MICM mechanism files and CECE mapping files.
 *
 * Two-tier loading:
 *   1. Mechanism species (names + molecular weights) from a MICM/OpenAtmos YAML file
 *   2. Emission speciation mappings from a CECE dataset-oriented YAML mapping file
 *
 * The MAP file uses a nested structure:
 *   mechanism: CB6
 *   datasets:
 *     MEGAN:
 *       isop:          # mechanism species name
 *         ISOP: 1.0    # emission class: scale factor
 *       terp:
 *         MT_PINE: 0.5
 *         MT_ACYC: 0.3
 *
 * Supports round-trip serialization: a valid config can be serialized to YAML via
 * ToYaml() and parsed back to produce an equivalent SpeciationConfig.
 */
class SpeciationConfigLoader {
   public:
    /**
     * @brief Load speciation config from a mechanism file and a mapping file.
     * @param mechanism_path Path to the MICM/OpenAtmos mechanism YAML file.
     * @param mapping_path Path to the CECE dataset-oriented speciation mapping YAML file.
     * @param dataset The dataset name to select from the MAP file (default: "MEGAN").
     * @return A validated SpeciationConfig.
     * @throws std::runtime_error if a file does not exist.
     * @throws YAML::ParserException if YAML syntax is invalid.
     * @throws std::invalid_argument if validation fails or dataset not found.
     */
    SpeciationConfig Load(const std::string& mechanism_path,
                          const std::string& mapping_path,
                          const std::string& dataset = "MEGAN");

    /**
     * @brief Serialize a SpeciationConfig back to valid YAML strings.
     * @param config The speciation configuration to serialize.
     * @return A YAML string representing the combined mechanism and mapping content.
     */
    static std::string ToYaml(const SpeciationConfig& config);

   private:
    /**
     * @brief Parse mechanism species from a MICM/OpenAtmos YAML file.
     * @param node The parsed YAML root node of the mechanism file.
     * @return A partial SpeciationConfig with mechanism_name and species populated.
     *
     * Expects the MICM format:
     *   name: <mechanism_name>
     *   species:
     *     - name: <species_name>
     *       molecular weight [kg mol-1]: <value>
     *
     * Molecular weights are converted from kg/mol (MICM) to g/mol internally.
     */
    SpeciationConfig ParseMechanism(const YAML::Node& node);

    /**
     * @brief Parse speciation mappings from a CECE dataset-oriented mapping YAML file.
     * @param node The parsed YAML root node of the mapping file.
     * @param config The SpeciationConfig to populate with mappings and dataset_name.
     * @param dataset The dataset name to select from the MAP file.
     *
     * Expects the format:
     *   mechanism: <mechanism_name>
     *   datasets:
     *     <dataset_name>:
     *       <mechanism_species>:
     *         <emission_class>: <scale_factor>
     */
    void ParseMapping(const YAML::Node& node, SpeciationConfig& config,
                      const std::string& dataset);

    /**
     * @brief Validate cross-references between mechanism and mapping data.
     * @param config The fully populated SpeciationConfig.
     * @throws std::invalid_argument if any mechanism species in mappings is not in the
     *         mechanism species list, or any emission class name is invalid.
     */
    void Validate(const SpeciationConfig& config);
};

}  // namespace cece

#endif  // CECE_SPECIATION_CONFIG_HPP
