#include "Features/complex_materials/ComplexParallaxModel.h"
#include "Features/complex_materials/ComplexParallaxSettings.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    using namespace community_shaders::complex_materials;

    int failures{};

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    [[nodiscard]] bool near(float left, float right) noexcept
    {
        return std::fabs(left - right) < 1.0e-6f;
    }
}

int main()
{
    expect(!Settings{}.environmentResponseEnabled,
        "metal response must fail closed when its INI key is absent");
    expect(landscapeParallaxSlot(273u) == 0u &&
            landscapeParallaxSlot(274u) == 1u &&
            landscapeParallaxSlot(275u) == 2u,
        "exact landscape contracts did not map to stable slots");
    expect(landscapeParallaxSlot(272u) ==
            kLandscapeMaterialContractIndices.size(),
        "an adjacent non-landscape contract entered the parallax family");
    expect(shouldUseLandscapeParallax(true, true, 274u),
        "enabled ready parallax contract stayed inactive");
    expect(!shouldUseLandscapeParallax(false, true, 274u) &&
            !shouldUseLandscapeParallax(true, false, 274u) &&
            !shouldUseLandscapeParallax(true, true, 272u),
        "parallax fail-closed gate accepted an invalid state");

    Settings malformed{};
    malformed.parallaxQuality = 99;
    malformed.parallaxDepth = std::numeric_limits<float>::quiet_NaN();
    malformed.grazingClamp = -2.0f;
    malformed.fadeStart = 5000.0f;
    malformed.fadeEnd = 100.0f;
    const auto safe = sanitize(malformed);
    expect(safe.parallaxQuality == 2, "quality did not clamp");
    expect(near(safe.parallaxDepth, Settings{}.parallaxDepth),
        "invalid depth did not use its default");
    expect(near(safe.grazingClamp, 0.05f),
        "grazing clamp did not preserve its safe lower bound");
    expect(safe.fadeEnd >= safe.fadeStart + 1.0f,
        "fade interval is not strictly increasing");

    const auto frame = makeFrameData(safe);
    expect(frame.enableComplexParallax == 1u,
        "enabled setting did not reach the frame ABI");
    expect(near(frame.minimumSteps, 16.0f) &&
            near(frame.maximumSteps, 28.0f),
        "high quality did not select the bounded step range");

    const std::array<std::array<float, 3>, 3> rows{
        std::array<float, 3>{ 1.0f, 2.0f, 3.0f },
        std::array<float, 3>{ 4.0f, 5.0f, 6.0f },
        std::array<float, 3>{ 7.0f, 8.0f, 9.0f },
    };
    const auto tangent = transformViewDirectionToTangentSpace(
        rows,
        { 1.0f, 10.0f, 100.0f });
    expect(near(tangent[0], 741.0f) && near(tangent[1], 852.0f) &&
            near(tangent[2], 963.0f),
        "FO4VR TBN rows were used without the required transpose");
    expect(near(depthFromLandscapeHeight(0.75f), 0.25f),
        "landscape elevation alpha was not inverted into ray depth");
    expect(near(adaptiveParallaxStepCount(12.0f, 20.0f, 1.0f, 1.0f),
               20.0f),
        "near-field parallax did not preserve its full step budget");
    expect(adaptiveParallaxStepCount(12.0f, 20.0f, 1.0f, 0.25f) <
            20.0f,
        "fading parallax did not reduce its march budget");
    expect(near(adaptiveParallaxStepCount(12.0f, 20.0f, 1.0f, 0.0f),
               4.0f),
        "fully faded parallax did not converge to the bounded minimum");

    if (failures) {
        return 1;
    }
    std::cout << "Complex parallax settings and stereo model tests passed.\n";
    return 0;
}
