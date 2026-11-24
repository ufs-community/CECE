#include "cece/physics/cece_plume_rise.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "cece/cece_physics_factory.hpp"

namespace cece {
namespace {

PhysicsRegistration<PlumeRiseScheme> register_plume_rise("plume_rise");

}  // namespace

std::unordered_map<std::string, PlumeRiseMethodFactory::CreatorFunc>& PlumeRiseMethodFactory::GetRegistry() {
    static std::unordered_map<std::string, CreatorFunc> registry;
    return registry;
}

void PlumeRiseMethodFactory::RegisterMethod(const std::string& name, CreatorFunc creator) {
    if (name.empty() || !creator) throw std::invalid_argument("Plume-rise method registration requires a name and creator");
    if (!GetRegistry().emplace(name, std::move(creator)).second) {
        throw std::invalid_argument("Duplicate plume-rise method registration: " + name);
    }
}

std::unique_ptr<PhysicsScheme> PlumeRiseMethodFactory::CreateMethod(const std::string& name) {
    const auto& registry = GetRegistry();
    const auto method = registry.find(name);
    if (method != registry.end()) return method->second();

    std::vector<std::string> available = AvailableMethods();
    std::sort(available.begin(), available.end());
    std::ostringstream message;
    message << "Unknown or unimplemented plume-rise method '" << name << "'";
    if (!available.empty()) {
        message << "; implemented methods:";
        for (const auto& entry : available) message << " " << entry;
    }
    message << "; reserved for future methods: sofiev, freitas, and grell_freitas";
    throw std::invalid_argument(message.str());
}

std::vector<std::string> PlumeRiseMethodFactory::AvailableMethods() {
    std::vector<std::string> result;
    result.reserve(GetRegistry().size());
    for (const auto& [name, creator] : GetRegistry()) {
        (void)creator;
        result.push_back(name);
    }
    return result;
}

void PlumeRiseScheme::Initialize(const conf::Value& config, CeceDiagnosticManager* diag_manager) {
    method_.reset();
    const std::string method_name = config["method"].string_or("briggs");
    auto method = PlumeRiseMethodFactory::CreateMethod(method_name);
    method->Initialize(config, diag_manager);
    method_ = std::move(method);
}

void PlumeRiseScheme::Run(CeceImportState& import_state, CeceExportState& export_state) {
    if (!method_) throw std::logic_error("Plume-rise scheme must be initialized before Run");
    method_->Run(import_state, export_state);
}

}  // namespace cece
