#pragma once

#include <algorithm>
#include <cmath>

namespace community_shaders::filmic_tonemapping
{
    struct Settings final
    {
        bool enabled{ true };
        bool useNativeAutoExposure{ true };
        float exposureCompensationEV{};
        float filmicStrength{ 1.0f };
        float whitePointScale{ 1.0f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.exposureCompensationEV =
            std::isfinite(result.exposureCompensationEV) ?
            std::clamp(result.exposureCompensationEV, -3.0f, 3.0f) : 0.0f;
        result.filmicStrength = std::isfinite(result.filmicStrength) ?
            std::clamp(result.filmicStrength, 0.0f, 1.0f) : 1.0f;
        result.whitePointScale = std::isfinite(result.whitePointScale) ?
            std::clamp(result.whitePointScale, 0.5f, 2.0f) : 1.0f;
        return result;
    }
}
