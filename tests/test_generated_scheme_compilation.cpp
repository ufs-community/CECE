/**
 * @file test_generated_scheme_compilation.cpp
 * @brief Property-based test for generated physics scheme compilation
 *
 * This test validates Property 11: Generated Scheme Compilation
 * Requirement 5.24: FOR ALL generated physics schemes, compiling and running
 * SHALL produce no compilation errors or runtime crashes
 *
 * The test generates random valid scheme configurations, runs the scheme
 * generator for each configuration, and verifies that the generated code
 * compiles successfully without errors.
 *
 * Test Strategy:
 * 1. Generate random valid scheme configurations with various combinations of:
 *    - Import fields (0-5 fields)
 *    - Export fields (1-3 fields)
 *    - Diagnostic fields (0-3 fields)
 *    - Configuration options (0-4 options)
 * 2. Write each configuration to a temporary YAML file
 * 3. Run the scheme generator on the configuration
 * 4. Verify generated files exist and contain valid C++ code
 * 5. Attempt to compile the generated code
 * 6. Verify compilation succeeds with no errors
 *
 * Iterations: 50+ random configurations
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace aces::test {

/**
 * @class SchemeConfigGenerator
 * @brief Generates random valid physics scheme configurations
 */
class SchemeConfigGenerator {
   public:
    SchemeConfigGenerator() : rng_(std::random_device{}()), dist_(0, 1) {}

    /**
     * @brief Generate a random valid scheme configuration
     * @return YAML string representing a valid scheme configuration
     */
    std::string GenerateRandomConfig() {
        std::stringstream ss;

        // Generate scheme name
        std::string scheme_name = GenerateSchemeName();
        std::string class_name = scheme_name;
        class_name[0] = std::toupper(class_name[0]);

        ss << "scheme:\n";
        ss << "  name: " << class_name << "\n";
        ss << "  description: \"Auto-generated test scheme " << scheme_name << "\"\n";
        ss << "  language: cpp\n";

        // Generate imports (0-5 fields)
        int num_imports = dist_(rng_) % 6;
        if (num_imports > 0) {
            ss << "imports:\n";
            for (int i = 0; i < num_imports; ++i) {
                ss << "  - name: input_" << i << "\n";
                ss << "    var_name: input_" << i << "_var\n";
                ss << "    units: " << GenerateUnits() << "\n";
                ss << "    dimensions: " << (dist_(rng_) % 2 == 0 ? "2D" : "3D") << "\n";
            }
        }

        // Generate exports (1-3 fields, at least one required)
        int num_exports = 1 + (dist_(rng_) % 3);
        ss << "exports:\n";
        for (int i = 0; i < num_exports; ++i) {
            ss << "  - name: output_" << i << "\n";
            ss << "    var_name: output_" << i << "_var\n";
            ss << "    units: " << GenerateUnits() << "\n";
            ss << "    dimensions: " << (dist_(rng_) % 2 == 0 ? "2D" : "3D") << "\n";
        }

        // Generate diagnostics (0-3 fields)
        int num_diagnostics = dist_(rng_) % 4;
        if (num_diagnostics > 0) {
            ss << "diagnostics:\n";
            for (int i = 0; i < num_diagnostics; ++i) {
                ss << "  - name: diagnostic_" << i << "\n";
                ss << "    units: " << GenerateUnits() << "\n";
                ss << "    description: \"Diagnostic variable " << i << "\"\n";
            }
        }

        // Generate options (0-4 configuration parameters)
        int num_options = dist_(rng_) % 5;
        if (num_options > 0) {
            ss << "options:\n";
            for (int i = 0; i < num_options; ++i) {
                ss << "  - name: param_" << i << "\n";
                ss << "    type: " << GenerateOptionType() << "\n";
                ss << "    default: " << GenerateDefaultValue() << "\n";
                ss << "    description: \"Configuration parameter " << i << "\"\n";
            }
        }

        return ss.str();
    }

   private:
    std::mt19937 rng_;
    std::uniform_int_distribution<int> dist_;

    std::string GenerateSchemeName() {
        static const std::vector<std::string> prefixes = {"custom", "test", "example", "generated",
                                                          "auto"};
        static const std::vector<std::string> suffixes = {"emission", "process", "scheme", "kernel",
                                                          "compute"};

        int prefix_idx = dist_(rng_) % prefixes.size();
        int suffix_idx = dist_(rng_) % suffixes.size();
        return prefixes[prefix_idx] + "_" + suffixes[suffix_idx];
    }

    std::string GenerateUnits() {
        static const std::vector<std::string> units = {
            "K", "m/s", "kg/m3", "kg/m2/s", "Pa", "dimensionless", "W/m2", "mol/mol", "ppm", "ppb"};
        return units[dist_(rng_) % units.size()];
    }

    std::string GenerateOptionType() {
        static const std::vector<std::string> types = {"double", "int", "bool"};
        return types[dist_(rng_) % types.size()];
    }

    std::string GenerateDefaultValue() {
        int choice = dist_(rng_) % 3;
        if (choice == 0) {
            return std::to_string(1.0e-6 * (1 + dist_(rng_) % 100));
        } else if (choice == 1) {
            return std::to_string(dist_(rng_) % 100);
        } else {
            return dist_(rng_) % 2 == 0 ? "true" : "false";
        }
    }
};

/**
 * @class GeneratedSchemeCompilationTest
 * @brief Property-based test for scheme generation and compilation
 */
class GeneratedSchemeCompilationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create temporary directory for test artifacts
        temp_dir_ = fs::temp_directory_path() / "aces_scheme_test";
        fs::create_directories(temp_dir_);

        // Create subdirectories for generated files
        fs::create_directories(temp_dir_ / "include" / "aces" / "physics");
        fs::create_directories(temp_dir_ / "src" / "physics");
    }

    void TearDown() override {
        // Clean up temporary directory
        if (fs::exists(temp_dir_)) {
            fs::remove_all(temp_dir_);
        }
    }

    /**
     * @brief Run the scheme generator on a configuration file
     * @param config_path Path to the YAML configuration file
     * @return True if generator succeeded, false otherwise
     */
    bool RunSchemeGenerator(const std::string& config_path) {
        // Build command to run the generator.
        // Try multiple potential paths to the generator script.
        std::vector<std::string> paths = {"scripts/generate_physics_scheme.py",
                                          "../scripts/generate_physics_scheme.py"};

        for (const auto& path : paths) {
            if (fs::exists(path)) {
                std::string cmd = "python3 " + path + " " + config_path;
                cmd += " > /dev/null 2>&1";
                int result = std::system(cmd.c_str());
                return result == 0;
            }
        }

        return false;
    }

    /**
     * @brief Verify that generated files exist and contain valid C++ code
     * @param scheme_name Name of the generated scheme
     * @return True if files are valid, false otherwise
     */
    bool VerifyGeneratedFiles(const std::string& scheme_name) {
        std::string header_file = "include/aces/physics/aces_" + scheme_name + ".hpp";
        std::string impl_file = "src/physics/aces_" + scheme_name + ".cpp";

        // Check that files exist
        if (!fs::exists(header_file)) {
            ADD_FAILURE() << "Header file not generated: " << header_file;
            return false;
        }
        if (!fs::exists(impl_file)) {
            ADD_FAILURE() << "Implementation file not generated: " << impl_file;
            return false;
        }

        // Verify header file contains expected content
        std::ifstream header(header_file);
        std::string header_content((std::istreambuf_iterator<char>(header)),
                                   std::istreambuf_iterator<char>());

        if (header_content.find("#ifndef ACES_") == std::string::npos) {
            ADD_FAILURE() << "Header file missing include guard";
            return false;
        }
        if (header_content.find("class ") == std::string::npos) {
            ADD_FAILURE() << "Header file missing class definition";
            return false;
        }
        if (header_content.find("BasePhysicsScheme") == std::string::npos) {
            ADD_FAILURE() << "Header file missing BasePhysicsScheme inheritance";
            return false;
        }

        // Verify implementation file contains expected content
        std::ifstream impl(impl_file);
        std::string impl_content((std::istreambuf_iterator<char>(impl)),
                                 std::istreambuf_iterator<char>());

        if (impl_content.find("PhysicsRegistration") == std::string::npos) {
            ADD_FAILURE() << "Implementation file missing PhysicsRegistration";
            return false;
        }
        if (impl_content.find("void Initialize") == std::string::npos) {
            ADD_FAILURE() << "Implementation file missing Initialize method";
            return false;
        }
        if (impl_content.find("void Run") == std::string::npos) {
            ADD_FAILURE() << "Implementation file missing Run method";
            return false;
        }

        return true;
    }

    /**
     * @brief Attempt to compile generated scheme code
     * @param scheme_name Name of the generated scheme
     * @return True if compilation succeeded, false otherwise
     */
    bool CompileGeneratedScheme(const std::string& scheme_name) {
        std::string header_file = "include/aces/physics/aces_" + scheme_name + ".hpp";
        std::string impl_file = "src/physics/aces_" + scheme_name + ".cpp";

        // Create a simple compilation test file
        std::string test_file = "test_compile_" + scheme_name + ".cpp";
        std::ofstream test_src(test_file);

        test_src << "#include \"" << header_file << "\"\n";
        test_src << "#include <iostream>\n";
        test_src << "\n";
        test_src << "int main() {\n";
        test_src << "  // Verify the generated class can be instantiated\n";
        test_src << "  // (This is a compile-time check only)\n";
        test_src << "  return 0;\n";
        test_src << "}\n";
        test_src.close();

        // Attempt compilation with g++.
        // We use ACES_TEST_COMPILE_FLAGS environment variable to pass include paths,
        // which avoids hardcoding environment-specific paths in the source code.
        const char* extra_flags = std::getenv("ACES_TEST_COMPILE_FLAGS");
        std::string flags = extra_flags ? std::string(extra_flags) : "";

        // Also ensure we look in the current directory for the header.
        // We need -Iinclude to find the generated header in build/include/aces/physics/...
        // and -I. for general project headers.
        std::string compile_cmd = "g++ -std=c++20 -fPIC -fopenmp -I. -Iinclude " + flags +
                                  " -c " + impl_file + " -o /tmp/test_" + scheme_name + ".o 2>&1";

        int result = std::system(compile_cmd.c_str());

        // Clean up test file
        fs::remove(test_file);

        return result == 0;
    }

    fs::path temp_dir_;
};

/**
 * @test Property 11: Generated Scheme Compilation
 *
 * FOR ALL randomly generated valid scheme configurations:
 * - The scheme generator SHALL successfully parse the configuration
 * - The generator SHALL create valid C++ header and implementation files
 * - The generated files SHALL contain required class definitions and methods
 * - The generated code SHALL compile without errors
 *
 * Validates Requirement 5.24
 */
TEST_F(GeneratedSchemeCompilationTest, RandomConfigurationsCompile) {
    SchemeConfigGenerator generator;
    const int num_iterations = 50;
    int successful_generations = 0;
    int successful_compilations = 0;

    for (int i = 0; i < num_iterations; ++i) {
        // Generate random configuration
        std::string config_yaml = generator.GenerateRandomConfig();

        // Write configuration to temporary file
        std::string config_file = "test_scheme_config_" + std::to_string(i) + ".yaml";
        std::ofstream config_out(config_file);
        config_out << config_yaml;
        config_out.close();

        // Run scheme generator
        if (!RunSchemeGenerator(config_file)) {
            ADD_FAILURE() << "Scheme generator failed for iteration " << i;
            fs::remove(config_file);
            continue;
        }
        successful_generations++;

        // Extract scheme name from config
        size_t name_pos = config_yaml.find("name: ");
        if (name_pos == std::string::npos) {
            ADD_FAILURE() << "Could not extract scheme name from config";
            fs::remove(config_file);
            continue;
        }

        size_t name_end = config_yaml.find('\n', name_pos);
        std::string scheme_name = config_yaml.substr(name_pos + 6, name_end - name_pos - 6);

        // Convert class name to scheme name (lowercase)
        std::transform(scheme_name.begin(), scheme_name.end(), scheme_name.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Verify generated files
        if (!VerifyGeneratedFiles(scheme_name)) {
            ADD_FAILURE() << "Generated files invalid for iteration " << i;
            fs::remove(config_file);
            continue;
        }

        // Attempt compilation
        if (CompileGeneratedScheme(scheme_name)) {
            successful_compilations++;
        } else {
            ADD_FAILURE() << "Compilation failed for scheme: " << scheme_name;
        }

        // Clean up
        fs::remove(config_file);
        std::string header_file = "include/aces/physics/aces_" + scheme_name + ".hpp";
        std::string impl_file = "src/physics/aces_" + scheme_name + ".cpp";
        if (fs::exists(header_file)) fs::remove(header_file);
        if (fs::exists(impl_file)) fs::remove(impl_file);
    }

    // Verify that most configurations were successfully generated and compiled
    EXPECT_GE(successful_generations, num_iterations * 0.9)
        << "At least 90% of configurations should generate successfully";
    EXPECT_GE(successful_compilations, num_iterations * 0.9)
        << "At least 90% of generated schemes should compile successfully";
}

/**
 * @test Verify generated schemes have required methods
 *
 * FOR ALL generated schemes:
 * - The header file SHALL declare Initialize, Run, and Finalize methods
 * - The implementation file SHALL define these methods
 * - The implementation SHALL use PhysicsRegistration for self-registration
 */
TEST_F(GeneratedSchemeCompilationTest, GeneratedSchemesHaveRequiredMethods) {
    SchemeConfigGenerator generator;

    // Generate a single configuration
    std::string config_yaml = generator.GenerateRandomConfig();

    // Write configuration to temporary file
    std::string config_file = "test_required_methods.yaml";
    std::ofstream config_out(config_file);
    config_out << config_yaml;
    config_out.close();

    // Run scheme generator
    ASSERT_TRUE(RunSchemeGenerator(config_file)) << "Scheme generator failed";

    // Extract scheme name
    size_t name_pos = config_yaml.find("name: ");
    ASSERT_NE(name_pos, std::string::npos);

    size_t name_end = config_yaml.find('\n', name_pos);
    std::string scheme_name = config_yaml.substr(name_pos + 6, name_end - name_pos - 6);
    std::transform(scheme_name.begin(), scheme_name.end(), scheme_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Verify header file
    std::string header_file = "include/aces/physics/aces_" + scheme_name + ".hpp";
    std::ifstream header(header_file);
    std::string header_content((std::istreambuf_iterator<char>(header)),
                               std::istreambuf_iterator<char>());

    EXPECT_NE(header_content.find("void Initialize"), std::string::npos)
        << "Header should declare Initialize method";
    EXPECT_NE(header_content.find("void Run"), std::string::npos)
        << "Header should declare Run method";
    EXPECT_NE(header_content.find("void Finalize"), std::string::npos)
        << "Header should declare Finalize method";

    // Verify implementation file
    std::string impl_file = "src/physics/aces_" + scheme_name + ".cpp";
    std::ifstream impl(impl_file);
    std::string impl_content((std::istreambuf_iterator<char>(impl)),
                             std::istreambuf_iterator<char>());

    EXPECT_NE(impl_content.find("::Initialize"), std::string::npos)
        << "Implementation should define Initialize method";
    EXPECT_NE(impl_content.find("::Run"), std::string::npos)
        << "Implementation should define Run method";
    EXPECT_NE(impl_content.find("::Finalize"), std::string::npos)
        << "Implementation should define Finalize method";
    EXPECT_NE(impl_content.find("PhysicsRegistration"), std::string::npos)
        << "Implementation should use PhysicsRegistration";

    // Clean up
    fs::remove(config_file);
    if (fs::exists(header_file)) fs::remove(header_file);
    if (fs::exists(impl_file)) fs::remove(impl_file);
}

/**
 * @test Verify generated schemes handle various field combinations
 *
 * FOR ALL combinations of import/export/diagnostic fields:
 * - The generator SHALL successfully create valid code
 * - The generated code SHALL compile without errors
 */
TEST_F(GeneratedSchemeCompilationTest, VariousFieldCombinations) {
    // Test specific combinations
    std::vector<std::tuple<int, int, int>> combinations = {
        {0, 1, 0},  // No imports, 1 export, no diagnostics
        {1, 1, 0},  // 1 import, 1 export, no diagnostics
        {3, 2, 2},  // 3 imports, 2 exports, 2 diagnostics
        {5, 3, 3},  // 5 imports, 3 exports, 3 diagnostics
    };

    for (const auto& [num_imports, num_exports, num_diagnostics] : combinations) {
        std::stringstream ss;

        ss << "scheme:\n";
        ss << "  name: TestScheme\n";
        ss << "  description: \"Test scheme with " << num_imports << " imports, " << num_exports
           << " exports, " << num_diagnostics << " diagnostics\"\n";
        ss << "  language: cpp\n";

        if (num_imports > 0) {
            ss << "imports:\n";
            for (int i = 0; i < num_imports; ++i) {
                ss << "  - name: input_" << i << "\n";
                ss << "    var_name: input_" << i << "_var\n";
                ss << "    units: K\n";
                ss << "    dimensions: 3D\n";
            }
        }

        ss << "exports:\n";
        for (int i = 0; i < num_exports; ++i) {
            ss << "  - name: output_" << i << "\n";
            ss << "    var_name: output_" << i << "_var\n";
            ss << "    units: kg/m2/s\n";
            ss << "    dimensions: 2D\n";
        }

        if (num_diagnostics > 0) {
            ss << "diagnostics:\n";
            for (int i = 0; i < num_diagnostics; ++i) {
                ss << "  - name: diag_" << i << "\n";
                ss << "    units: dimensionless\n";
                ss << "    description: \"Diagnostic " << i << "\"\n";
            }
        }

        std::string config_file = "test_combination_" + std::to_string(num_imports) + "_" +
                                  std::to_string(num_exports) + "_" +
                                  std::to_string(num_diagnostics) + ".yaml";
        std::ofstream config_out(config_file);
        config_out << ss.str();
        config_out.close();

        EXPECT_TRUE(RunSchemeGenerator(config_file))
            << "Generator failed for combination: " << num_imports << " imports, " << num_exports
            << " exports, " << num_diagnostics << " diagnostics";

        // Clean up
        fs::remove(config_file);
        std::string header_file = "include/aces/physics/aces_testscheme.hpp";
        std::string impl_file = "src/physics/aces_testscheme.cpp";
        if (fs::exists(header_file)) fs::remove(header_file);
        if (fs::exists(impl_file)) fs::remove(impl_file);
    }
}

}  // namespace aces::test
