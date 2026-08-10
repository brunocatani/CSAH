#include "Features/linear_lighting/DFTiledPointLightModel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>

namespace
{
    [[nodiscard]] bool near(float lhs, float rhs) noexcept
    {
        return std::abs(lhs - rhs) <= 0.00001f;
    }

    [[nodiscard]] bool expect(bool condition, const char* message) noexcept
    {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
        }
        return condition;
    }
}

int main()
{
    using namespace community_shaders::linear_lighting;
    bool passed = true;

    Settings disabled{};
    disabled.lightGamma = 1.1f;
    disabled.pointLightMultiplier = 7.0f;
    const auto vanilla = makeDFTiledPointLightProducerState(disabled);
    passed &= expect(!vanilla.enabled, "disabled state remained active");
    passed &= expect(
        near(vanilla.gamma, kVanillaPointLightGamma),
        "disabled state did not restore FO4VR gamma");
    passed &= expect(
        near(vanilla.colorMultiplier, 1.0f),
        "disabled state did not restore FO4VR light scale");

    Settings enabled{};
    enabled.enabled = true;
    enabled.lightGamma = 1.8f;
    enabled.pointLightMultiplier = 0.5f;
    const auto linear = makeDFTiledPointLightProducerState(enabled);
    passed &= expect(linear.enabled, "enabled state was lost");
    passed &= expect(near(linear.gamma, 1.8f), "configured gamma was lost");
    passed &= expect(
        near(
            linear.colorMultiplier,
            std::numbers::pi_v<float> * 0.5f),
        "point-light PI compensation was not applied");

    enabled.lightGamma = std::numeric_limits<float>::infinity();
    enabled.pointLightMultiplier = -4.0f;
    const auto sanitized = makeDFTiledPointLightProducerState(enabled);
    passed &= expect(
        near(sanitized.gamma, Settings{}.lightGamma),
        "non-finite gamma was not sanitized");
    passed &= expect(
        near(sanitized.colorMultiplier, 0.0f),
        "negative point multiplier was not sanitized");

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
