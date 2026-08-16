#pragma once

#include <algorithm>
#include <cmath>

namespace community_shaders::hair_specular
{
    struct Settings
    {
        bool enabled{ true };
        float specularMultiplier{ 1.6f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.specularMultiplier = std::isfinite(result.specularMultiplier) ?
            std::clamp(result.specularMultiplier, 0.0f, 4.0f) : 1.6f;
        return result;
    }
}
