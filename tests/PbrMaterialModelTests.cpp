#include "Features/pbr/PbrMaterialModel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "PBR material model test failed: " << message
                      << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    [[nodiscard]] bool near(float left, float right) noexcept
    {
        return std::fabs(left - right) < 1.0e-6f;
    }
}

int main()
{
    using namespace community_shaders::pbr;

    require(effectiveEnabled(Settings{}, true),
        "enabled PBR did not accept Linear Lighting");
    require(!effectiveEnabled(Settings{}, false),
        "PBR did not fail closed without Linear Lighting");

    require(near(decodeComplexMetalness(1.0f), 0.0f),
        "ordinary material decoded as metal");
    require(near(decodeComplexMetalness(0.75f), 0.5f),
        "complex metalness transport changed");
    require(near(decodeComplexMetalness(0.5f), 1.0f),
        "fully metallic transport changed");
    require(near(decodeComplexMetalness(
                     std::numeric_limits<float>::quiet_NaN()),
                 0.0f),
        "invalid metalness did not fail closed");

    require(phongEncodedToRoughness(1.0f) <
            phongEncodedToRoughness(0.0f),
        "Phong conversion did not preserve gloss ordering");
    require(phongEncodedToRoughness(1.0f) >=
            kMinimumPerceptualRoughness,
        "Phong conversion violated the roughness floor");
    require(near(
                phongEncodedToRoughness(1.0f),
                std::pow(2.0f / 2050.0f, 0.25f)),
        "Phong conversion is not the verified fourth-root mapping");

    Settings settings{};
    settings.specularRoughnessBlend = 1.0f;
    require(near(materialRoughness(3.5f, 1.0f, 0.0f, settings), 0.5f),
        "environment LOD roughness conversion changed");
    require(near(materialRoughness(7.0f, 1.0f, 1.0f, settings),
                 phongEncodedToRoughness(1.0f)),
        "legacy specular roughness conversion changed");

    settings.baseF0Multiplier = 0.32f;
    settings.cubemapToF0Multiplier = 1.0f;
    settings.minimumF0 = 0.02f;
    require(near(dielectricF0(0.5f, settings), 0.16f),
        "dielectric F0 conversion changed");
    require(near(dielectricF0(0.0f, settings), 0.02f),
        "minimum dielectric F0 was not preserved");

    const auto disabledFrame = makeFrameData(settings, false);
    require(near(disabledFrame.enabled, 0.0f),
        "draw-local disabled state did not fail closed");

    std::cout << "PBR material transport model tests passed.\n";
    return EXIT_SUCCESS;
}
