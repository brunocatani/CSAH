#pragma once

#include <algorithm>
#include <cmath>

namespace community_shaders::basic_wetness
{
    struct Settings
    {
        bool enabled{ false };
        float wetness{ 0.65f };
        float diffuseDarkening{ 0.18f };
        float specularMultiplier{ 1.6f };
        float roughnessScale{ 0.35f };
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.wetness = std::isfinite(result.wetness) ?
            std::clamp(result.wetness, 0.0f, 1.0f) : 0.65f;
        result.diffuseDarkening = std::isfinite(result.diffuseDarkening) ?
            std::clamp(result.diffuseDarkening, 0.0f, 0.75f) : 0.18f;
        result.specularMultiplier =
            std::isfinite(result.specularMultiplier) ?
            std::clamp(result.specularMultiplier, 0.0f, 4.0f) : 1.6f;
        result.roughnessScale = std::isfinite(result.roughnessScale) ?
            std::clamp(result.roughnessScale, 0.05f, 1.0f) : 0.35f;
        return result;
    }
}
