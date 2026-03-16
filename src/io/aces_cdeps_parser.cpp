/**
 * @file aces_cdeps_parser.cpp
 * @brief Implementation of CDEPS streams configuration parser.
 */

#include "aces/aces_cdeps_parser.hpp"

#include <netcdf.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace aces {

// Helper function to trim whitespace
std::string CdepsStreamsParser::Trim(const std::string& str) {
    auto start =
        std::find_if_not(str.begin(), str.end(), [](unsigned char ch) { return std::isspace(ch); });
    auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) {
                   return std::isspace(ch);
               }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

// Parse variable mapping string
std::vector<CdepsVariableConfig> CdepsStreamsParser::ParseVariables(const std::string& var_string) {
    std::vector<CdepsVariableConfig> variables;

    // Split by comma
    std::istringstream iss(var_string);
    std::string token;

    while (std::getline(iss, token, ',')) {
        token = Trim(token);
        if (token.empty()) continue;

        // Split by colon
        size_t colon_pos = token.find(':');
        if (colon_pos == std::string::npos) {
            // No colon, use same name for both
            CdepsVariableConfig var;
            var.name_in_file = token;
            var.name_in_model = token;
            variables.push_back(var);
        } else {
            // Colon present, split into file:model
            CdepsVariableConfig var;
            var.name_in_file = Trim(token.substr(0, colon_pos));
            var.name_in_model = Trim(token.substr(colon_pos + 1));
            variables.push_back(var);
        }
    }

    return variables;
}

// Parse a single stream block
CdepsStreamConfig CdepsStreamsParser::ParseStreamBlock(const std::vector<std::string>& lines,
                                                       size_t start_idx, size_t& end_idx) {
    CdepsStreamConfig stream;

    // Extract stream name from "stream::name" line
    std::string header = lines[start_idx];
    size_t double_colon = header.find("::");
    if (double_colon != std::string::npos) {
        stream.name = Trim(header.substr(double_colon + 2));
    }

    // Parse attributes until we hit "::"
    for (size_t i = start_idx + 1; i < lines.size(); ++i) {
        std::string line = Trim(lines[i]);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Check for end marker
        if (line == "::") {
            end_idx = i;
            return stream;
        }

        // Parse key = value
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq_pos));
        std::string value = Trim(line.substr(eq_pos + 1));

        // Parse based on key
        if (key == "file_paths") {
            // Split by comma for multiple files
            std::istringstream iss(value);
            std::string path;
            while (std::getline(iss, path, ',')) {
                path = Trim(path);
                if (!path.empty()) {
                    stream.file_paths.push_back(path);
                }
            }
        } else if (key == "variables") {
            stream.variables = ParseVariables(value);
        } else if (key == "taxmode") {
            stream.taxmode = value;
        } else if (key == "tintalgo") {
            stream.tintalgo = value;
        } else if (key == "mapalgo") {
            stream.mapalgo = value;
        } else if (key == "dtlimit") {
            stream.dtlimit = std::stoi(value);
        } else if (key == "yearFirst") {
            stream.yearFirst = std::stoi(value);
        } else if (key == "yearLast") {
            stream.yearLast = std::stoi(value);
        } else if (key == "yearAlign") {
            stream.yearAlign = std::stoi(value);
        } else if (key == "offset") {
            stream.offset = std::stoi(value);
        } else if (key == "meshfile") {
            stream.meshfile = value;
        } else if (key == "lev_dimname") {
            stream.lev_dimname = value;
        }
    }

    end_idx = lines.size();
    return stream;
}

// Parse streams file
AcesCdepsConfig CdepsStreamsParser::ParseStreamsFile(const std::string& filepath) {
    AcesCdepsConfig config;

    // Open file
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open streams file: " + filepath);
    }

    // Read all lines
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    // Parse stream blocks
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string trimmed = Trim(lines[i]);

        // Look for stream definition
        if (trimmed.find("stream::") == 0) {
            size_t end_idx;
            CdepsStreamConfig stream = ParseStreamBlock(lines, i, end_idx);
            config.streams.push_back(stream);
            i = end_idx;  // Skip to end of this stream block
        }
    }

    return config;
}

// Check if file is NetCDF
bool CdepsStreamsParser::IsNetCDFFile(const std::string& filepath) {
    int ncid;
    int status = nc_open(filepath.c_str(), NC_NOWRITE, &ncid);
    if (status == NC_NOERR) {
        nc_close(ncid);
        return true;
    }
    return false;
}

// Check if variable exists in NetCDF file
bool CdepsStreamsParser::NetCDFHasVariable(const std::string& filepath,
                                           const std::string& varname) {
    int ncid, varid;
    int status = nc_open(filepath.c_str(), NC_NOWRITE, &ncid);
    if (status != NC_NOERR) {
        return false;
    }

    status = nc_inq_varid(ncid, varname.c_str(), &varid);
    nc_close(ncid);

    return (status == NC_NOERR);
}

// Validate file paths
void CdepsStreamsParser::ValidateFilePaths(const CdepsStreamConfig& stream,
                                           std::vector<std::string>& errors) {
    if (stream.file_paths.empty()) {
        errors.push_back("Stream '" + stream.name + "': No file paths specified");
        return;
    }

    for (const auto& filepath : stream.file_paths) {
        // Check if file exists
        std::ifstream file(filepath);
        if (!file.good()) {
            errors.push_back("Stream '" + stream.name + "': File not found: " + filepath);
            continue;
        }
        file.close();

        // Check if it's a valid NetCDF file
        if (!IsNetCDFFile(filepath)) {
            errors.push_back("Stream '" + stream.name +
                             "': File is not a valid NetCDF file: " + filepath);
        }
    }
}

// Validate variables
void CdepsStreamsParser::ValidateVariables(const CdepsStreamConfig& stream,
                                           std::vector<std::string>& errors) {
    if (stream.variables.empty()) {
        errors.push_back("Stream '" + stream.name + "': No variables specified");
        return;
    }

    // Check each variable in each file
    for (const auto& filepath : stream.file_paths) {
        if (!IsNetCDFFile(filepath)) {
            // Already reported in ValidateFilePaths
            continue;
        }

        for (const auto& var : stream.variables) {
            if (!NetCDFHasVariable(filepath, var.name_in_file)) {
                errors.push_back("Stream '" + stream.name + "': Variable '" + var.name_in_file +
                                 "' not found in file: " + filepath);
            }
        }
    }
}

// Validate interpolation mode
void CdepsStreamsParser::ValidateInterpolationMode(const CdepsStreamConfig& stream,
                                                   std::vector<std::string>& errors) {
    // Valid temporal interpolation algorithms
    const std::vector<std::string> valid_tintalgo = {"none", "linear", "nearest", "lower", "upper"};
    if (std::find(valid_tintalgo.begin(), valid_tintalgo.end(), stream.tintalgo) ==
        valid_tintalgo.end()) {
        errors.push_back("Stream '" + stream.name + "': Invalid temporal interpolation mode '" +
                         stream.tintalgo + "'. Valid options: none, linear, nearest, lower, upper");
    }

    // Valid time axis modes
    const std::vector<std::string> valid_taxmode = {"cycle", "extend", "limit"};
    if (std::find(valid_taxmode.begin(), valid_taxmode.end(), stream.taxmode) ==
        valid_taxmode.end()) {
        errors.push_back("Stream '" + stream.name + "': Invalid time axis mode '" + stream.taxmode +
                         "'. Valid options: cycle, extend, limit");
    }
}

// Validate streams configuration
bool CdepsStreamsParser::ValidateStreamsConfig(const AcesCdepsConfig& config,
                                               std::vector<std::string>& errors) {
    if (config.streams.empty()) {
        errors.push_back("No streams defined in configuration");
        return false;
    }

    for (const auto& stream : config.streams) {
        // Validate required attributes
        if (stream.name.empty()) {
            errors.push_back("Stream has no name");
        }

        // Validate file paths
        ValidateFilePaths(stream, errors);

        // Validate variables
        ValidateVariables(stream, errors);

        // Validate interpolation modes
        ValidateInterpolationMode(stream, errors);
    }

    return errors.empty();
}

// Write streams file
void CdepsStreamsParser::WriteStreamsFile(const std::string& filepath,
                                          const AcesCdepsConfig& config) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }

    file << "# CDEPS Streams Configuration for ACES\n";
    file << "# Generated by CdepsStreamsParser\n\n";

    for (const auto& stream : config.streams) {
        file << "stream::" << stream.name << "\n";

        // Write file paths
        if (!stream.file_paths.empty()) {
            file << "  file_paths = ";
            for (size_t i = 0; i < stream.file_paths.size(); ++i) {
                if (i > 0) file << ", ";
                file << stream.file_paths[i];
            }
            file << "\n";
        }

        // Write variables
        if (!stream.variables.empty()) {
            file << "  variables = ";
            for (size_t i = 0; i < stream.variables.size(); ++i) {
                if (i > 0) file << ", ";
                file << stream.variables[i].name_in_file << ":"
                     << stream.variables[i].name_in_model;
            }
            file << "\n";
        }

        // Write other attributes
        file << "  taxmode = " << stream.taxmode << "\n";
        file << "  tintalgo = " << stream.tintalgo << "\n";
        file << "  mapalgo = " << stream.mapalgo << "\n";
        file << "  dtlimit = " << stream.dtlimit << "\n";
        file << "  yearFirst = " << stream.yearFirst << "\n";
        file << "  yearLast = " << stream.yearLast << "\n";
        file << "  yearAlign = " << stream.yearAlign << "\n";
        file << "  offset = " << stream.offset << "\n";

        if (!stream.meshfile.empty()) {
            file << "  meshfile = " << stream.meshfile << "\n";
        }

        file << "  lev_dimname = " << stream.lev_dimname << "\n";

        file << "::\n\n";
    }

    file.close();
}

}  // namespace aces
