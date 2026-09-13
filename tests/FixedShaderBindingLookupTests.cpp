#include "Features/linear_lighting/FixedShaderBindingLookup.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using csah::linear_lighting::FixedShaderBindingLookup;

    FixedShaderBindingLookup<4> lookup;
    std::array<int, 5> shaders{};
    require(!lookup.insert(nullptr, 1), "null shader rejected");
    require(!lookup.insert(&shaders[0], 0), "zero binding rejected");
    require(lookup.find(nullptr) == 0, "null lookup is empty");

    for (std::size_t index = 0; index < 4; ++index) {
        require(
            lookup.insert(&shaders[index], index + 1),
            "fixed table accepts capacity entries");
    }
    for (std::size_t index = 0; index < 4; ++index) {
        require(
            lookup.find(&shaders[index]) == index + 1,
            "fixed table resolves every inserted entry");
    }

    require(
        lookup.insert(&shaders[2], 3),
        "identical duplicate is idempotent");
    require(
        !lookup.insert(&shaders[2], 99),
        "conflicting duplicate is rejected");
    require(
        !lookup.insert(&shaders[4], 5),
        "full table fails closed");
    require(
        lookup.find(&shaders[4]) == 0,
        "failed insertion remains absent");

    std::cout << "Fixed shader-binding lookup tests passed.\n";
    return EXIT_SUCCESS;
}
