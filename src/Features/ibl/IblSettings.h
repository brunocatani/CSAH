#pragma once

#include <algorithm>
#include <cmath>

namespace community_shaders::ibl
{
    struct Settings
    {
        // Preserve the behavior of builds that predate the independent IBL
        // control. The runtime remains fail-closed until a validated
        // environment pair exists even when this setting is enabled.
        bool enabled{ true };
        bool diffuseEnabled{ true };
        float diffuseLevel{ 1.0f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.diffuseLevel = std::isfinite(result.diffuseLevel) ?
            std::clamp(result.diffuseLevel, 0.0f, 2.0f) :
            1.0f;
        return result;
    }
}
