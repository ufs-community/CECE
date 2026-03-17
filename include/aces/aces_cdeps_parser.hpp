#ifndef ACES_CDEPS_PARSER_HPP
#define ACES_CDEPS_PARSER_HPP

/**
 * @file aces_cdeps_parser.hpp
 * @brief Parser and validator for CDEPS streams configuration files.
 *
 * This module provides functionality to parse ESMF Config format streams files
 * used by CDEPS-inline for data ingestion. It validates file paths, variables,
 * and interpolation modes, and supports round-trip serialization for testing.
 *
 * @see Requirements 1.1, 1.2, 7.1-7.10
 */

#include <string>
#include <vector>

#include "aces/aces_config.hpp"

namespace aces {

/**
 * @class CdepsStreamsParser
 * @brief Parser and validator for CDEPS streams configuration files.
 *
 * This class provides static methods to parse ESMF Config format streams files,
 * validate their contents, and serialize them back to ESMF Config format for
 * round-trip testing.
 *
 * ESMF Config Format Example:
 * @code
 * # CDEPS Streams Configuration
 * stream::anthro_emissions
 *   file_paths = /data/emissions/CEDS_CO_2020.nc
 *   variables = CO_emis:CEDS_CO, NOx_emis:CEDS_NOx
 *   taxmode = cycle
 *   tintalgo = linear
 *   mapalgo = bilinear
 *   yearFirst = 2020
 *   yearLast = 2020
 *   yearAlign = 2020
 *   offset = 0
 * ::
 * @endcode
 */
class CdepsStreamsParser {
   public:
    /**
     * @brief Parse ESMF Config format streams file.
     *
     * Reads and parses a CDEPS streams configuration file in ESMF Config format.
     * The file should contain one or more stream definitions with file paths,
     * variables, and temporal interpolation settings.
     *
     * @param filepath Path to the ESMF Config format streams file.
     * @return AcesCdepsConfig object containing parsed stream configurations.
     * @throws std::runtime_error if file cannot be opened or parsed.
     *
     * @see Requirements 7.1, 7.2
     */
    static AcesCdepsConfig ParseStreamsFile(const std::string& filepath);

    /**
     * @brief Validate streams configuration.
     *
     * Performs comprehensive validation of a streams configuration including:
     * - File path existence and readability
     * - Variable existence in NetCDF files
     * - Valid interpolation modes (none, linear, nearest)
     * - Valid time axis modes (cycle, extend, limit)
     * - Required attributes present
     *
     * @param config The streams configuration to validate.
     * @param errors Output vector to receive error messages.
     * @return true if configuration is valid, false otherwise.
     *
     * @see Requirements 7.2-7.10
     */
    static bool ValidateStreamsConfig(const AcesCdepsConfig& config,
                                      std::vector<std::string>& errors);

    /**
     * @brief Write streams configuration to ESMF Config format file.
     *
     * Serializes an AcesCdepsConfig object back to ESMF Config format.
     * This enables round-trip testing: parse → serialize → parse should
     * produce equivalent configurations.
     *
     * @param filepath Path to output ESMF Config format file.
     * @param config The streams configuration to serialize.
     * @throws std::runtime_error if file cannot be written.
     *
     * @see Requirements 7.13, Property 16
     */
    static void WriteStreamsFile(const std::string& filepath, const AcesCdepsConfig& config);

   private:
    /**
     * @brief Validate file paths in a stream configuration.
     *
     * Checks that all file paths exist, are readable, and are valid NetCDF files.
     * Supports both absolute paths and paths relative to a configurable data root.
     *
     * @param stream The stream configuration to validate.
     * @param errors Output vector to receive error messages.
     *
     * @see Requirements 7.3, 7.4, 7.8
     */
    static void ValidateFilePaths(const CdepsStreamConfig& stream,
                                  std::vector<std::string>& errors);

    /**
     * @brief Validate variables in a stream configuration.
     *
     * Checks that all specified variables exist in the NetCDF files.
     * Opens each file and queries variable metadata.
     *
     * @param stream The stream configuration to validate.
     * @param errors Output vector to receive error messages.
     *
     * @see Requirements 7.5, 7.9
     */
    static void ValidateVariables(const CdepsStreamConfig& stream,
                                  std::vector<std::string>& errors);

    /**
     * @brief Validate interpolation mode in a stream configuration.
     *
     * Checks that temporal interpolation mode is one of: none, linear, nearest.
     * Checks that time axis mode is one of: cycle, extend, limit.
     *
     * @param stream The stream configuration to validate.
     * @param errors Output vector to receive error messages.
     *
     * @see Requirements 7.6, 7.10
     */
    static void ValidateInterpolationMode(const CdepsStreamConfig& stream,
                                          std::vector<std::string>& errors);

    /**
     * @brief Parse a single stream block from ESMF Config format.
     *
     * Parses a stream definition block between "stream::name" and "::".
     * Extracts all attributes and variable mappings.
     *
     * @param lines Vector of lines from the config file.
     * @param start_idx Starting line index for this stream block.
     * @param end_idx Output parameter for ending line index.
     * @return CdepsStreamConfig object for this stream.
     */
    static CdepsStreamConfig ParseStreamBlock(const std::vector<std::string>& lines,
                                              size_t start_idx, size_t& end_idx);

    /**
     * @brief Parse variable mapping string.
     *
     * Parses a variable mapping in the format "name_in_file:name_in_model".
     * Supports comma-separated lists of mappings.
     *
     * @param var_string Variable mapping string from config file.
     * @return Vector of CdepsVariableConfig objects.
     */
    static std::vector<CdepsVariableConfig> ParseVariables(const std::string& var_string);

    /**
     * @brief Trim whitespace from string.
     *
     * @param str String to trim.
     * @return Trimmed string.
     */
    static std::string Trim(const std::string& str);

    /**
     * @brief Check if a file is a valid NetCDF file.
     *
     * @param filepath Path to file to check.
     * @return true if file is a valid NetCDF file, false otherwise.
     */
    static bool IsNetCDFFile(const std::string& filepath);

    /**
     * @brief Check if a variable exists in a NetCDF file.
     *
     * @param filepath Path to NetCDF file.
     * @param varname Name of variable to check.
     * @return true if variable exists, false otherwise.
     */
    static bool NetCDFHasVariable(const std::string& filepath, const std::string& varname);
};

}  // namespace aces

#endif  // ACES_CDEPS_PARSER_HPP
