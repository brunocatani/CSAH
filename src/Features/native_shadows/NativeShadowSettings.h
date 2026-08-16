#pragma once

#include <algorithm>
#include <cmath>

namespace community_shaders::native_shadows
{
    struct Settings
    {
        bool enabled{ true };
        bool extendedDirectionalCascades{ true };
        bool tiledDeferredLighting{ true };
        float directionalShadowDistance{ 15000.0f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.directionalShadowDistance =
            std::isfinite(result.directionalShadowDistance) ?
            std::clamp(result.directionalShadowDistance, 3000.0f, 50000.0f) :
            15000.0f;
        return result;
    }
}
