/**
 * @file tide_yaml_c.cpp
 * @brief C++ implementation of the TIDE YAML parser using yaml-cpp.
 */

#include <yaml-cpp/yaml.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

extern "C" {
    /**
     * @struct tide_stream_config_t
     * @brief Configuration for a single TIDE data stream.
     */
    typedef struct {
        char* name;
        char* mesh_file;
        char* lev_dimname;
        char* tax_mode;
        char* time_interp;
        char* map_algo;
        char* read_mode;
        double dt_limit;
        int year_first;
        int year_last;
        int year_align;
        int offset;
        char** input_files;
        int num_files;
        char** file_vars;
        char** model_vars;
        int num_fields;
        char* cf_detection_mode;
        int cf_cache_enabled;
        int cf_log_level;
    } tide_stream_config_t;

    /**
     * @struct tide_config_t
     * @brief Top-level configuration containing multiple TIDE streams.
     */
    typedef struct {
        tide_stream_config_t* streams;
        int num_streams;
    } tide_config_t;

    /**
     * @brief Parses a TIDE YAML configuration file.
     * @param filename The path to the YAML file.
     * @return A pointer to a tide_config_t structure, or nullptr on failure.
     */
    tide_config_t* tide_parse_yaml(const char* filename) {
        try {
            // Load YAML file
            YAML::Node config = YAML::LoadFile(filename);

            // Ensure streams key exists
            if (!config["streams"]) {
                std::cerr << "ERROR: [TIDE] 'streams' key missing from YAML configuration file: " << filename << std::endl;
                return nullptr;
            }

            auto streams_node = config["streams"];
            int num_streams = streams_node.size();

            tide_config_t* cfg = new tide_config_t();
            cfg->num_streams = num_streams;
            cfg->streams = new tide_stream_config_t[num_streams];

            // Parse each stream configuration
            for (int i = 0; i < num_streams; ++i) {
                auto s = streams_node[i];
                tide_stream_config_t& sc = cfg->streams[i];

                // Mandatory string attributes (no default)
                sc.name = strdup(s["name"].as<std::string>().c_str());

                // Optional attributes with defaults
                if (s["lev_dimname"]) {
                     std::cout << "DEBUG: lev_dimname: " << s["lev_dimname"].as<std::string>() << std::endl;
                }

                if (s["mesh_file"]) {
                    sc.mesh_file = strdup(s["mesh_file"].as<std::string>().c_str());
                } else {
                    sc.mesh_file = strdup("none");
                }

                sc.lev_dimname = s["lev_dimname"] ? strdup(s["lev_dimname"].as<std::string>().c_str()) : strdup("null");
                sc.tax_mode = s["tax_mode"] ? strdup(s["tax_mode"].as<std::string>().c_str()) : strdup("cycle");
                sc.time_interp = s["time_interp"] ? strdup(s["time_interp"].as<std::string>().c_str()) : strdup("linear");
                sc.map_algo = s["map_algo"] ? strdup(s["map_algo"].as<std::string>().c_str()) : strdup("bilinear");
                sc.read_mode = s["read_mode"] ? strdup(s["read_mode"].as<std::string>().c_str()) : strdup("single");
                sc.dt_limit = s["dt_limit"] ? s["dt_limit"].as<double>() : 1.5;
                sc.year_first = s["year_first"].as<int>();
                sc.year_last = s["year_last"].as<int>();
                sc.year_align = s["year_align"].as<int>();
                sc.offset = s["offset"] ? s["offset"].as<int>() : 0;

                // Parse input files list
                auto files = s["input_files"];
                sc.num_files = files.size();
                sc.input_files = new char*[sc.num_files];
                for (int j = 0; j < sc.num_files; ++j) {
                    sc.input_files[j] = strdup(files[j].as<std::string>().c_str());
                }

                // Parse field maps list
                auto fields = s["field_maps"];
                sc.num_fields = fields.size();
                sc.file_vars = new char*[sc.num_fields];
                sc.model_vars = new char*[sc.num_fields];
                for (int j = 0; j < sc.num_fields; ++j) {
                    sc.file_vars[j] = strdup(fields[j]["file_var"].as<std::string>().c_str());
                    sc.model_vars[j] = strdup(fields[j]["model_var"].as<std::string>().c_str());
                }

                // CF detection configuration (Task 11)
                sc.cf_detection_mode = s["cf_detection_mode"] ?
                    strdup(s["cf_detection_mode"].as<std::string>().c_str()) : strdup("auto");
                sc.cf_cache_enabled = s["cf_cache_enabled"] ? s["cf_cache_enabled"].as<bool>() ? 1 : 0 : 1;
                sc.cf_log_level = s["cf_log_level"] ? s["cf_log_level"].as<int>() : 2;
            }
            return cfg;
        } catch (const YAML::BadFile& e) {
            std::cerr << "ERROR: [TIDE] Failed to load YAML configuration file: " << filename << ". " << e.what() << std::endl;
            return nullptr;
        } catch (const YAML::ParserException& e) {
            std::cerr << "ERROR: [TIDE] YAML Parsing Error in file " << filename << ": " << e.what() << std::endl;
            return nullptr;
        } catch (const YAML::Exception& e) {
            std::cerr << "ERROR: [TIDE] YAML Exception while reading " << filename << ": " << e.what() << std::endl;
            return nullptr;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: [TIDE] Unexpected error parsing YAML file " << filename << ": " << e.what() << std::endl;
            return nullptr;
        }
    }

    /**
     * @brief Frees the memory allocated by tide_parse_yaml.
     * @param cfg The configuration structure to free.
     */
    void tide_free_config(tide_config_t* cfg) {
        if (!cfg) return;

        // Free memory for each stream
        for (int i = 0; i < cfg->num_streams; ++i) {
            tide_stream_config_t& sc = cfg->streams[i];
            free(sc.name); free(sc.mesh_file); free(sc.lev_dimname);
            free(sc.tax_mode); free(sc.time_interp); free(sc.map_algo); free(sc.read_mode);
            free(sc.cf_detection_mode);
            for (int j = 0; j < sc.num_files; ++j) free(sc.input_files[j]);
            delete[] sc.input_files;
            for (int j = 0; j < sc.num_fields; ++j) {
                free(sc.file_vars[j]); free(sc.model_vars[j]);
            }
            delete[] sc.file_vars; delete[] sc.model_vars;
        }
        delete[] cfg->streams;
        delete cfg;
    }
}
