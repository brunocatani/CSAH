#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace csah::contact_shadows
{
    struct Settings
    {
        bool enabled{ true };
        bool foveated{ true };
        float strength{ 0.85f };
        float maxDistance{ 96.0f };
        float fadeDistance{ 2048.0f };
        float thickness{ 0.012f };
        std::uint32_t sampleCount{ 8 };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.strength = std::isfinite(result.strength) ?
            std::clamp(result.strength, 0.0f, 1.0f) : 0.85f;
        result.maxDistance = std::isfinite(result.maxDistance) ?
            std::clamp(result.maxDistance, 8.0f, 256.0f) : 96.0f;
        result.fadeDistance = std::isfinite(result.fadeDistance) ?
            std::clamp(result.fadeDistance, 256.0f, 8192.0f) : 2048.0f;
        result.thickness = std::isfinite(result.thickness) ?
            std::clamp(result.thickness, 0.002f, 0.05f) : 0.012f;
        result.sampleCount = std::clamp(result.sampleCount, 2u, 8u);
        return result;
    }
}
