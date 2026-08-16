#pragma once

#include <algorithm>
#include <cmath>

namespace community_shaders::wrapped_grass
{
    struct Settings
    {
        bool enabled{ true };
        float wrapAmount{ 0.5f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.wrapAmount = std::isfinite(result.wrapAmount) ?
            std::clamp(result.wrapAmount, 0.0f, 1.0f) : 0.5f;
        return result;
    }
}
