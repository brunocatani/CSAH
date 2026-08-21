#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace community_shaders::skylighting
{
    enum class Quality : std::uint32_t
    {
        low = 0,
        medium = 1,
        high = 2,
    };

    struct Settings
    {
        bool enabled{ true };
        Quality quality{ Quality::high };
        float minimumDiffuseVisibility{ 0.10f };
        float minimumSpecularVisibility{ 0.10f };
        float maximumZenithDegrees{ 90.0f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Quality sanitizeQuality(
        std::uint32_t value) noexcept
    {
        return value <= static_cast<std::uint32_t>(Quality::high) ?
            static_cast<Quality>(value) : Quality::high;
    }

    [[nodiscard]] inline std::string_view qualityName(Quality quality) noexcept
    {
        switch (quality) {
        case Quality::low:
            return "Low";
        case Quality::medium:
            return "Medium";
        case Quality::high:
            return "High";
        }
        return "High";
    }

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.quality = sanitizeQuality(
            static_cast<std::uint32_t>(result.quality));
        result.minimumDiffuseVisibility =
            std::isfinite(result.minimumDiffuseVisibility) ?
            std::clamp(result.minimumDiffuseVisibility, 0.01f, 1.0f) :
            0.10f;
        result.minimumSpecularVisibility =
            std::isfinite(result.minimumSpecularVisibility) ?
            std::clamp(result.minimumSpecularVisibility, 0.01f, 1.0f) :
            0.10f;
        result.maximumZenithDegrees =
            std::isfinite(result.maximumZenithDegrees) ?
            std::clamp(result.maximumZenithDegrees, 0.0f, 90.0f) :
            90.0f;
        return result;
    }
}
