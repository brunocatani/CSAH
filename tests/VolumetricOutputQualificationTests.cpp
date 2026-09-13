#include "Features/volumetric_lighting/VolumetricOutputQualification.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Volumetric output qualification test failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace csah::volumetric_lighting;
    std::array<OutputProbe, 128> probes{};
    for (std::size_t index = 0; index < probes.size(); ++index) {
        const auto value = static_cast<float>(index) / 256.0f;
        probes[index] = {
            .structured = value,
            .filtered = value * 0.75f,
            .depth = 0.5f,
            .positiveContrast = value * 0.25f,
        };
    }
    const auto valid = qualifyOutput(probes, 1.0f, 1.0f, 0.9f, 0.8f);
    require(valid.passed, "valid output was rejected");
    require(valid.failureMask == OutputFailure_None,
        "valid output retained a failure");
    require(valid.structuredMaximum > valid.filteredMean,
        "valid statistics were not retained");

    auto invalid = probes;
    invalid[17].structured = std::numeric_limits<float>::quiet_NaN();
    const auto nonFinite = qualifyOutput(invalid, 1.0f, 1.0f, 1.0f, 1.0f);
    require(!nonFinite.passed &&
            (nonFinite.failureMask & OutputFailure_NonFinite) != 0,
        "non-finite output was accepted");

    invalid = probes;
    invalid[5].filtered = 2.0f;
    const auto outOfRange = qualifyOutput(
        invalid, 1.0f, 1.0f, 1.0f, 1.0f);
    require(!outOfRange.passed &&
            (outOfRange.failureMask & OutputFailure_ProbeRange) != 0,
        "out-of-range integrated lighting was accepted");

    const auto invalidLight = qualifyOutput(
        probes, -1.0f, 1.0f, 1.0f, 1.0f);
    require(!invalidLight.passed &&
            (invalidLight.failureMask & OutputFailure_LightRange) != 0,
        "invalid image-space light data was accepted");

    require(!qualifyOutput(
                 std::span<const OutputProbe>(probes.data(), 64),
                 1.0f,
                 1.0f,
                 1.0f,
                 1.0f)
                 .passed,
        "incomplete stereo probe set was accepted");

    std::cout << "Volumetric output qualification tests passed.\n";
    return EXIT_SUCCESS;
}
