#include <Kokkos_Core.hpp>
#include <cassert>
#include <cmath>
#include <iostream>

#include "cece/cece_scheme_runtime.hpp"

int main() {
    Kokkos::initialize();
    {
        cece::SchemeRuntime runtime({{"native_example", "multiplier: 3.0"}}, 3, 2);
        const double values[] = {1, 2, 3, 4, 5, 6};
        runtime.set_field("base_anthropogenic_nox", values, 6, 2);
        runtime.step(0.0, 3600.0);
        auto output = runtime.get_field("nox");
        for (int i = 0; i < 6; ++i) assert(output[i] == 3 * values[i]);
        runtime.set_field("base_anthropogenic_nox", values, 6, 2);
        runtime.step(3600.0, 3600.0);
        assert(runtime.get_field("nox") == output);  // no previous-step accumulation
        bool rejected = false;
        try {
            runtime.step(0.0, 3600.0);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected);
        cece::SchemeRuntime second({{"native_example", "multiplier: 2.0"}}, 1, 1);
        const double forcing[] = {5};
        second.set_field("base_anthropogenic_nox", forcing, 1, 1);
        second.step(0, 1);
        assert(second.get_field("nox")[0] == 10);
        assert(runtime.get_field("nox") == output);
    }
    auto closed = std::make_unique<cece::SchemeRuntime>(std::vector<cece::SchemeRequest>{{"native_example", "{}"}}, 1, 1);
    closed->close();
    Kokkos::finalize();
    closed.reset();  // closed objects must hold no Kokkos allocations
    std::cout << "scheme runtime checks passed\n";
}
