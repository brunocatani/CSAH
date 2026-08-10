#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"

#include <numbers>

namespace community_shaders::linear_lighting
{
    constexpr float kVanillaPointLightGamma = 2.2f;

    struct DFTiledPointLightProducerState
    {
        bool enabled{};
        float gamma{ kVanillaPointLightGamma };
        float colorMultiplier{ 1.0f };
    };

    [[nodiscard]] inline DFTiledPointLightProducerState
    makeDFTiledPointLightProducerState(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        if (!safe.enabled) {
            return {};
        }
        return {
            .enabled = true,
            .gamma = safe.lightGamma,
            .colorMultiplier = std::numbers::pi_v<float> *
                safe.pointLightMultiplier,
        };
    }
}
