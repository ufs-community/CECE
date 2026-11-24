// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cece/cece_fire_input.hpp"

namespace {

using Arguments = std::map<std::string, std::string>;

Arguments ParseArguments(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string key(argv[index]);
        if (key == "--help") {
            std::cout << "Usage: cece_fire_product_normalize --adapter ID --input PATH --output PATH --source-id ID "
                         "--cutoff UTC --cutoff-mode fixed|host_supplied --time-allocation POLICY --variant unscaled|scaled "
                         "[--profile-id ID --profile-version VERSION --profile-sha256 HASH]\n";
            std::exit(0);
        }
        if (key.size() < 3 || key.substr(0, 2) != "--" || index + 1 >= argc) {
            throw std::invalid_argument("arguments must be provided as --name value pairs");
        }
        if (!arguments.emplace(key.substr(2), argv[++index]).second) {
            throw std::invalid_argument("duplicate command-line option " + key);
        }
    }
    return arguments;
}

std::string Required(const Arguments& arguments, std::string_view name) {
    const auto found = arguments.find(std::string(name));
    if (found == arguments.end() || found->second.empty()) throw std::invalid_argument("missing required option --" + std::string(name));
    return found->second;
}

void ValidateOptions(const Arguments& arguments) {
    constexpr std::array<std::string_view, 17> allowed{"adapter",        "input",           "output",          "source-id",         "cutoff",
                                                       "cutoff-mode",    "time-allocation", "variant",         "profile-id",        "profile-version",
                                                       "profile-sha256", "product-id",      "product-version", "algorithm-version", "interval-start",
                                                       "interval-end",   "acquired-at"};
    for (const auto& [name, value] : arguments) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end() && name != "available-at" && name != "content-sha256") {
            throw std::invalid_argument("unknown command-line option --" + name);
        }
    }
}

void RequireEqualIfProvided(const Arguments& arguments, std::string_view name, const std::string& actual) {
    const auto found = arguments.find(std::string(name));
    if (found != arguments.end() && found->second != actual) {
        throw std::invalid_argument("source manifest " + std::string(name) + " does not match product metadata");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = ParseArguments(argc, argv);
        ValidateOptions(arguments);
        const auto adapter = Required(arguments, "adapter");
        const auto input = Required(arguments, "input");
        const auto output = Required(arguments, "output");
        const auto source_id = Required(arguments, "source-id");
        const auto cutoff = cece::FireUtcTime(Required(arguments, "cutoff"));
        const auto cutoff_mode_text = Required(arguments, "cutoff-mode");
        const auto allocation = Required(arguments, "time-allocation");
        const auto variant = Required(arguments, "variant");
        if (cutoff_mode_text != "fixed" && cutoff_mode_text != "host_supplied") {
            throw std::invalid_argument("cutoff mode must be fixed or host_supplied");
        }
        if (variant != "unscaled" && variant != "scaled") throw std::invalid_argument("variant must be unscaled or scaled");

        cece::FireProductDecodeOptions decode_options;
        decode_options.availability_cutoff = cutoff;
        decode_options.cutoff_mode = cutoff_mode_text == "fixed" ? cece::FireCutoffMode::Fixed : cece::FireCutoffMode::HostSupplied;
        decode_options.scaling = variant == "scaled" ? cece::FireProductScalingKind::Applied : cece::FireProductScalingKind::Unscaled;

        cece::FireProductEmissionsInterval product = [&]() {
            if (adapter == "rave-v2r0") return cece::DecodeRaveProductFile(input, decode_options);
            if (adapter == "gbbepx-v4r0") return cece::DecodeGbbepxProductFile(input, decode_options);
            throw std::invalid_argument("unsupported product adapter '" + adapter + "'");
        }();

        const auto& metadata = product.Data().product_identity.common_identity.Metadata();
        const auto& product_identity = product.Data().product_identity;
        RequireEqualIfProvided(arguments, "product-id", product_identity.product_id);
        RequireEqualIfProvided(arguments, "product-version", product_identity.version);
        RequireEqualIfProvided(arguments, "algorithm-version", metadata.algorithm_version);
        RequireEqualIfProvided(arguments, "variant", product_identity.variant);
        RequireEqualIfProvided(arguments, "interval-start", metadata.interval_start.ToString());
        RequireEqualIfProvided(arguments, "interval-end", metadata.interval_end.ToString());
        RequireEqualIfProvided(arguments, "acquired-at", metadata.acquired_at.ToString());
        RequireEqualIfProvided(arguments, "available-at", metadata.available_at.ToString());
        RequireEqualIfProvided(arguments, "content-sha256", metadata.content_sha256);

        cece::FireNormalizedProductWriteOptions write_options{source_id, allocation, std::nullopt};
        if (allocation == "profile") {
            write_options.time_profile = cece::FireProfileIdentity{Required(arguments, "profile-id"), Required(arguments, "profile-version"),
                                                                   Required(arguments, "profile-sha256")};
        } else if (arguments.contains("profile-id") || arguments.contains("profile-version") || arguments.contains("profile-sha256")) {
            throw std::invalid_argument("profile identity options require --time-allocation profile");
        }
        cece::WriteNormalizedFireProductFile(product, write_options, output);
        std::cout << "grid_sha256=" << metadata.grid_definition->Definition().grid_sha256 << '\n';
        if (product.Data().scaling_state.profile) {
            std::cout << "scaling_profile_id=" << product.Data().scaling_state.profile->id << '\n';
            std::cout << "scaling_profile_version=" << product.Data().scaling_state.profile->version << '\n';
            std::cout << "scaling_profile_sha256=" << product.Data().scaling_state.profile->content_sha256 << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fire product normalization error: " << error.what() << '\n';
        return 2;
    }
}
