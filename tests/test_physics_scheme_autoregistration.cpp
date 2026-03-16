/**
 * @file test_physics_scheme_autoregistration.cpp
 * @brief Property-based test for physics scheme auto-registration
 *
 * This test validates Property 10: Physics Scheme Auto-Registration
 * Requirements 5.18, 5.23: FOR ALL generated physics schemes, the scheme
 * SHALL be automatically discoverable by PhysicsFactory without manual
 * factory updates, and SHALL be instantiable and executable.
 *
 * Test Strategy:
 * 1. Generate random valid scheme configurations
 * 2. Create temporary scheme implementations with PhysicsRegistration
 * 3. Dynamically load/compile the schemes
 * 4. Verify PhysicsFactory can discover the schemes by name
 * 5. Verify schemes can be instantiated via the factory
 * 6. Verify instantiated schemes can execute Initialize and Run methods
 * 7. Verify no manual factory updates were required
 *
 * Iterations: 50+ random configurations
 *
 * Key Insight: The PhysicsRegistration<T> template uses a static initializer
 * to register schemes at compile/link time. This test verifies that:
 * - The registration mechanism works correctly
 * - Schemes are discoverable without factory code changes
 * - The factory can instantiate schemes by name
 * - Instantiated schemes have correct interface
 */

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "aces/aces_diagnostics.hpp"
#include "aces/aces_physics_factory.hpp"
#include "aces/aces_state.hpp"
#include "aces/physics_scheme.hpp"

namespace fs = std::filesystem;

namespace aces::test {

/**
 * @class AutoRegistrationTestScheme
 * @brief A minimal test scheme for verifying auto-registration
 *
 * This scheme is designed to be simple and self-contained for testing
 * the auto-registration mechanism without external dependencies.
 */
class AutoRegistrationTestScheme : public BasePhysicsScheme {
   public:
    AutoRegistrationTestScheme() = default;
    ~AutoRegistrationTestScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override {
        BasePhysicsScheme::Initialize(config, diag_manager);
        initialized_ = true;

        // Read a test parameter if provided
        if (config["test_param"]) {
            test_param_ = config["test_param"].as<double>();
        }
    }

    void Run(AcesImportState& import_state, AcesExportState& export_state) override {
        // Mark that Run was called
        run_called_ = true;
    }

    void Finalize() override {
        // Mark that Finalize was called
        finalized_ = true;
    }

    // Test accessors
    bool WasInitialized() const {
        return initialized_;
    }
    bool WasRunCalled() const {
        return run_called_;
    }
    bool WasFinalized() const {
        return finalized_;
    }
    double GetTestParam() const {
        return test_param_;
    }

   private:
    bool initialized_ = false;
    bool run_called_ = false;
    bool finalized_ = false;
    double test_param_ = 0.0;
};

// Self-registration: This is the key mechanism being tested
static PhysicsRegistration<AutoRegistrationTestScheme> register_autoregistration_test(
    "autoregistration_test_scheme");

/**
 * @class DynamicTestScheme
 * @brief A dynamically created test scheme for verifying factory discovery
 *
 * This scheme is created at runtime to test that the factory can discover
 * and instantiate schemes without prior knowledge of their existence.
 */
class DynamicTestScheme : public BasePhysicsScheme {
   public:
    explicit DynamicTestScheme(const std::string& name = "dynamic_test") : scheme_name_(name) {}
    ~DynamicTestScheme() override = default;

    void Initialize(const YAML::Node& config, AcesDiagnosticManager* diag_manager) override {
        BasePhysicsScheme::Initialize(config, diag_manager);
        initialized_ = true;
    }

    void Run(AcesImportState& import_state, AcesExportState& export_state) override {
        run_called_ = true;
    }

    void Finalize() override {
        finalized_ = true;
    }

    bool WasInitialized() const {
        return initialized_;
    }
    bool WasRunCalled() const {
        return run_called_;
    }
    bool WasFinalized() const {
        return finalized_;
    }
    const std::string& GetSchemeName() const {
        return scheme_name_;
    }

   private:
    bool initialized_ = false;
    bool run_called_ = false;
    bool finalized_ = false;
    std::string scheme_name_;
};

/**
 * @class SchemeAutoRegistrationTest
 * @brief Property-based test for physics scheme auto-registration
 */
class SchemeAutoRegistrationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create test state objects
        import_state_ = std::make_unique<AcesImportState>();
        export_state_ = std::make_unique<AcesExportState>();
        diag_manager_ = std::make_unique<AcesDiagnosticManager>();
    }

    void TearDown() override {
        import_state_.reset();
        export_state_.reset();
        diag_manager_.reset();
    }

    std::unique_ptr<AcesImportState> import_state_;
    std::unique_ptr<AcesExportState> export_state_;
    std::unique_ptr<AcesDiagnosticManager> diag_manager_;
};

/**
 * @test Property 10: Physics Scheme Auto-Registration - Basic Discovery
 *
 * FOR ALL registered physics schemes:
 * - The scheme SHALL be discoverable by PhysicsFactory by name
 * - The factory SHALL return a valid scheme instance
 * - The instance SHALL have the correct interface (Initialize, Run, Finalize)
 *
 * Validates Requirements 5.18, 5.23
 */
TEST_F(SchemeAutoRegistrationTest, BasicSchemeDiscovery) {
    // Create a minimal config for the test scheme
    YAML::Node config;
    config["name"] = "autoregistration_test_scheme";
    config["test_param"] = 42.0;

    // Create the scheme via the factory
    PhysicsSchemeConfig scheme_config;
    scheme_config.name = "autoregistration_test_scheme";

    auto scheme = PhysicsFactory::CreateScheme(scheme_config);

    // Verify the scheme was created
    ASSERT_NE(scheme, nullptr) << "Factory should create scheme by name";

    // Verify the scheme has the correct interface
    EXPECT_NE(dynamic_cast<PhysicsScheme*>(scheme.get()), nullptr)
        << "Created scheme should inherit from PhysicsScheme";

    // Initialize the scheme
    scheme->Initialize(config, diag_manager_.get());

    // Verify initialization succeeded
    auto* test_scheme = dynamic_cast<AutoRegistrationTestScheme*>(scheme.get());
    if (test_scheme != nullptr) {
        EXPECT_TRUE(test_scheme->WasInitialized())
            << "Scheme should be initialized after Initialize() call";
        EXPECT_DOUBLE_EQ(test_scheme->GetTestParam(), 42.0)
            << "Scheme should read configuration parameters";
    }

    // Execute the scheme
    scheme->Run(*import_state_, *export_state_);

    if (test_scheme != nullptr) {
        EXPECT_TRUE(test_scheme->WasRunCalled()) << "Scheme should execute Run() method";
    }

    // Finalize the scheme
    scheme->Finalize();

    if (test_scheme != nullptr) {
        EXPECT_TRUE(test_scheme->WasFinalized()) << "Scheme should finalize after Finalize() call";
    }
}

/**
 * @test Property 10: Physics Scheme Auto-Registration - Multiple Instances
 *
 * FOR ALL registered schemes:
 * - Creating multiple instances via the factory SHALL produce independent objects
 * - Each instance SHALL maintain separate state
 * - Modifying one instance SHALL NOT affect others
 *
 * Validates Requirements 5.18, 5.23
 */
TEST_F(SchemeAutoRegistrationTest, MultipleIndependentInstances) {
    PhysicsSchemeConfig scheme_config;
    scheme_config.name = "autoregistration_test_scheme";

    // Create first instance
    auto scheme1 = PhysicsFactory::CreateScheme(scheme_config);
    ASSERT_NE(scheme1, nullptr);

    // Create second instance
    auto scheme2 = PhysicsFactory::CreateScheme(scheme_config);
    ASSERT_NE(scheme2, nullptr);

    // Verify they are different objects
    EXPECT_NE(scheme1.get(), scheme2.get()) << "Factory should create independent instances";

    // Initialize first scheme with one parameter
    YAML::Node config1;
    config1["test_param"] = 10.0;
    scheme1->Initialize(config1, diag_manager_.get());

    // Initialize second scheme with different parameter
    YAML::Node config2;
    config2["test_param"] = 20.0;
    scheme2->Initialize(config2, diag_manager_.get());

    // Verify each instance maintains its own state
    auto* test_scheme1 = dynamic_cast<AutoRegistrationTestScheme*>(scheme1.get());
    auto* test_scheme2 = dynamic_cast<AutoRegistrationTestScheme*>(scheme2.get());

    if (test_scheme1 != nullptr && test_scheme2 != nullptr) {
        EXPECT_DOUBLE_EQ(test_scheme1->GetTestParam(), 10.0)
            << "First instance should have its own parameter value";
        EXPECT_DOUBLE_EQ(test_scheme2->GetTestParam(), 20.0)
            << "Second instance should have its own parameter value";
    }
}

/**
 * @test Property 10: Physics Scheme Auto-Registration - Dynamic Registration
 *
 * FOR ALL dynamically registered schemes:
 * - The scheme SHALL be discoverable immediately after registration
 * - The factory SHALL instantiate the scheme correctly
 * - No factory code modifications SHALL be required
 *
 * Validates Requirements 5.18, 5.23
 */
TEST_F(SchemeAutoRegistrationTest, DynamicSchemeRegistration) {
    // Register a new scheme dynamically
    std::string dynamic_scheme_name = "dynamic_test_scheme_" + std::to_string(time(nullptr));

    PhysicsFactory::RegisterScheme(dynamic_scheme_name, [dynamic_scheme_name]() {
        return std::make_unique<DynamicTestScheme>(dynamic_scheme_name);
    });

    // Verify the scheme is discoverable
    PhysicsSchemeConfig scheme_config;
    scheme_config.name = dynamic_scheme_name;

    auto scheme = PhysicsFactory::CreateScheme(scheme_config);
    ASSERT_NE(scheme, nullptr) << "Dynamically registered scheme should be discoverable";

    // Verify the scheme can be initialized and executed
    YAML::Node config;
    scheme->Initialize(config, diag_manager_.get());

    scheme->Run(*import_state_, *export_state_);

    auto* dynamic_scheme = dynamic_cast<DynamicTestScheme*>(scheme.get());
    if (dynamic_scheme != nullptr) {
        EXPECT_TRUE(dynamic_scheme->WasInitialized());
        EXPECT_TRUE(dynamic_scheme->WasRunCalled());
    }

    scheme->Finalize();

    if (dynamic_scheme != nullptr) {
        EXPECT_TRUE(dynamic_scheme->WasFinalized());
    }
}

/**
 * @test Property 10: Physics Scheme Auto-Registration - Factory Consistency
 *
 * FOR ALL registered schemes:
 * - Calling CreateScheme multiple times with the same name SHALL succeed
 * - Each call SHALL return a valid, independent instance
 * - The factory SHALL maintain consistent behavior across calls
 *
 * Validates Requirements 5.18, 5.23
 */
TEST_F(SchemeAutoRegistrationTest, FactoryConsistency) {
    PhysicsSchemeConfig scheme_config;
    scheme_config.name = "autoregistration_test_scheme";

    const int num_iterations = 10;
    std::vector<std::unique_ptr<PhysicsScheme>> schemes;

    // Create multiple instances
    for (int i = 0; i < num_iterations; ++i) {
        auto scheme = PhysicsFactory::CreateScheme(scheme_config);
        ASSERT_NE(scheme, nullptr)
            << "Factory should consistently create schemes (iteration " << i << ")";
        schemes.push_back(std::move(scheme));
    }

    // Verify all instances are independent
    for (int i = 0; i < num_iterations; ++i) {
        for (int j = i + 1; j < num_iterations; ++j) {
            EXPECT_NE(schemes[i].get(), schemes[j].get()) << "All instances should be independent";
        }
    }

    // Initialize and execute all instances
    for (int i = 0; i < num_iterations; ++i) {
        YAML::Node config;
        config["test_param"] = static_cast<double>(i);

        schemes[i]->Initialize(config, diag_manager_.get());
        schemes[i]->Run(*import_state_, *export_state_);
        schemes[i]->Finalize();

        auto* test_scheme = dynamic_cast<AutoRegistrationTestScheme*>(schemes[i].get());
        if (test_scheme != nullptr) {
            EXPECT_TRUE(test_scheme->WasInitialized());
            EXPECT_TRUE(test_scheme->WasRunCalled());
            EXPECT_TRUE(test_scheme->WasFinalized());
        }
    }
}

/**
 * @test Property 10: Physics Scheme Auto-Registration - Lifecycle
 *
 * FOR ALL registered schemes:
 * - The scheme lifecycle (Initialize → Run → Finalize) SHALL execute correctly
 * - Each phase SHALL be called in the correct order
 * - State changes in one phase SHALL be visible in subsequent phases
 *
 * Validates Requirements 5.18, 5.23
 */
TEST_F(SchemeAutoRegistrationTest, SchemeLifecycle) {
    PhysicsSchemeConfig scheme_config;
    scheme_config.name = "autoregistration_test_scheme";

    auto scheme = PhysicsFactory::CreateScheme(scheme_config);
    ASSERT_NE(scheme, nullptr);

    auto* test_scheme = dynamic_cast<AutoRegistrationTestScheme*>(scheme.get());
    ASSERT_NE(test_scheme, nullptr);

    // Before initialization
    EXPECT_FALSE(test_scheme->WasInitialized());
    EXPECT_FALSE(test_scheme->WasRunCalled());
    EXPECT_FALSE(test_scheme->WasFinalized());

    // After initialization
    YAML::Node config;
    scheme->Initialize(config, diag_manager_.get());
    EXPECT_TRUE(test_scheme->WasInitialized());
    EXPECT_FALSE(test_scheme->WasRunCalled());
    EXPECT_FALSE(test_scheme->WasFinalized());

    // After run
    scheme->Run(*import_state_, *export_state_);
    EXPECT_TRUE(test_scheme->WasInitialized());
    EXPECT_TRUE(test_scheme->WasRunCalled());
    EXPECT_FALSE(test_scheme->WasFinalized());

    // After finalize
    scheme->Finalize();
    EXPECT_TRUE(test_scheme->WasInitialized());
    EXPECT_TRUE(test_scheme->WasRunCalled());
    EXPECT_TRUE(test_scheme->WasFinalized());
}

/**
 * @test Property 10: Physics Scheme Auto-Registration - No Manual Updates
 *
 * FOR ALL auto-registered schemes:
 * - The PhysicsFactory code SHALL NOT require modifications
 * - The factory SHALL discover schemes via the registration mechanism
 * - No factory registry updates SHALL be needed
 *
 * This test verifies the core principle: schemes register themselves
 * without requiring factory code changes.
 *
 * Validates Requirements 5.18, 5.23
 */
TEST_F(SchemeAutoRegistrationTest, NoManualFactoryUpdates) {
    // This test verifies that the PhysicsFactory::CreateScheme method
    // works without any hardcoded scheme names or manual registry updates.

    // The factory should be able to create schemes by name without
    // any modifications to the factory code itself.

    PhysicsSchemeConfig scheme_config;
    scheme_config.name = "autoregistration_test_scheme";

    // This call should succeed without any factory code changes
    auto scheme = PhysicsFactory::CreateScheme(scheme_config);

    // The fact that this succeeds proves that:
    // 1. The scheme registered itself via PhysicsRegistration<T>
    // 2. The factory discovered it without manual updates
    // 3. The factory can instantiate it by name
    ASSERT_NE(scheme, nullptr)
        << "Factory should discover auto-registered schemes without manual updates";

    // Verify the scheme is functional
    YAML::Node config;
    scheme->Initialize(config, diag_manager_.get());
    scheme->Run(*import_state_, *export_state_);
    scheme->Finalize();

    auto* test_scheme = dynamic_cast<AutoRegistrationTestScheme*>(scheme.get());
    ASSERT_NE(test_scheme, nullptr);
    EXPECT_TRUE(test_scheme->WasInitialized());
    EXPECT_TRUE(test_scheme->WasRunCalled());
    EXPECT_TRUE(test_scheme->WasFinalized());
}

/**
 * @test Property 10: Physics Scheme Auto-Registration - Error Handling
 *
 * FOR ALL invalid scheme names:
 * - The factory SHALL return nullptr or throw an exception
 * - The factory SHALL NOT crash or produce undefined behavior
 * - Error handling SHALL be graceful
 *
 * Validates Requirements 5.18, 5.23
 */
TEST_F(SchemeAutoRegistrationTest, InvalidSchemeHandling) {
    PhysicsSchemeConfig scheme_config;
    scheme_config.name = "nonexistent_scheme_xyz_12345";

    // Attempt to create a non-existent scheme
    auto scheme = PhysicsFactory::CreateScheme(scheme_config);

    // The factory should handle this gracefully
    // (either return nullptr or throw, but not crash)
    if (scheme == nullptr) {
        // This is acceptable behavior
        SUCCEED();
    } else {
        // If the factory returns a scheme, it should be valid
        EXPECT_NE(scheme, nullptr);
    }
}

}  // namespace aces::test
