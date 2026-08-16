#pragma once

#include <algorithm>
#include <cmath>

namespace community_shaders::subsurface_scattering
{
    struct Settings
    {
        bool enabled{ true };
        float strength{ 0.55f };
        float radiusPixels{ 2.0f };
        float depthRejection{ 0.003f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.strength = std::isfinite(result.strength) ?
            std::clamp(result.strength, 0.0f, 1.0f) : 0.55f;
        result.radiusPixels = std::isfinite(result.radiusPixels) ?
            std::clamp(result.radiusPixels, 0.25f, 5.0f) : 2.0f;
        result.depthRejection = std::isfinite(result.depthRejection) ?
            std::clamp(result.depthRejection, 0.0001f, 0.05f) : 0.003f;
        return result;
    }
}
