#ifndef CECE_PLUME_RISE_HPP
#define CECE_PLUME_RISE_HPP

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cece/physics_scheme.hpp"

namespace cece {

class PlumeRiseMethodFactory {
   public:
    using CreatorFunc = std::function<std::unique_ptr<PhysicsScheme>()>;

    static void RegisterMethod(const std::string& name, CreatorFunc creator);
    static std::unique_ptr<PhysicsScheme> CreateMethod(const std::string& name);
    static std::vector<std::string> AvailableMethods();

   private:
    static std::unordered_map<std::string, CreatorFunc>& GetRegistry();
};

template <typename T>
class PlumeRiseMethodRegistration {
   public:
    explicit PlumeRiseMethodRegistration(const std::string& name) {
        PlumeRiseMethodFactory::RegisterMethod(name, []() { return std::make_unique<T>(); });
    }
};

class PlumeRiseScheme final : public PhysicsScheme {
   public:
    void Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) override;
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;

   private:
    std::unique_ptr<PhysicsScheme> method_;
};

}  // namespace cece

#endif
