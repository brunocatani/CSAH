#pragma once

#include <algorithm>
#include <cmath>

namespace csah::cloud_shadows
{
    struct Settings
    {
        bool enabled{ true };
        float opacity{ 0.55f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.opacity = std::isfinite(result.opacity) ?
            std::clamp(result.opacity, 0.0f, 1.0f) : 0.55f;
        return result;
    }
}
