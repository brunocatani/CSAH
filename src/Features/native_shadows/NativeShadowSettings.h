#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace csah::native_shadows
{
    struct Settings
    {
        bool enabled{ true };
        bool extendedDirectionalCascades{ true };
        bool tiledDeferredLighting{ true };
        float directionalShadowDistance{ 15000.0f };
        float cascadeBlendDistance{ 100.0f };
        std::uint32_t orthographicShadowFilter{ 3 };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.directionalShadowDistance =
            std::isfinite(result.directionalShadowDistance) ?
            std::clamp(result.directionalShadowDistance, 3000.0f, 50000.0f) :
            15000.0f;
        result.cascadeBlendDistance =
            std::isfinite(result.cascadeBlendDistance) ?
            std::clamp(result.cascadeBlendDistance, 0.0f, 5000.0f) :
            100.0f;
        result.orthographicShadowFilter =
            std::min(result.orthographicShadowFilter, std::uint32_t{ 5 });
        return result;
    }
}
