#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace csah::complex_materials
{
    struct Settings
    {
        bool environmentResponseEnabled{ false };
        bool parallaxEnabled{ true };
        std::int32_t parallaxQuality{ 1 };
        float parallaxDepth{ 0.035f };
        float grazingClamp{ 0.18f };
        float fadeStart{ 2048.0f };
        float fadeEnd{ 8192.0f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        const Settings defaults{};
        auto result = settings;
        result.parallaxQuality = std::clamp(result.parallaxQuality, 0, 2);
        result.parallaxDepth = std::isfinite(result.parallaxDepth) ?
            std::clamp(result.parallaxDepth, 0.0f, 0.2f) :
            defaults.parallaxDepth;
        result.grazingClamp = std::isfinite(result.grazingClamp) ?
            std::clamp(result.grazingClamp, 0.05f, 1.0f) :
            defaults.grazingClamp;
        result.fadeStart = std::isfinite(result.fadeStart) ?
            std::clamp(result.fadeStart, 0.0f, 65536.0f) :
            defaults.fadeStart;
        result.fadeEnd = std::isfinite(result.fadeEnd) ?
            std::clamp(result.fadeEnd, result.fadeStart + 1.0f, 131072.0f) :
            defaults.fadeEnd;
        return result;
    }

    struct alignas(16) FrameData
    {
        std::uint32_t enableComplexParallax{};
        float parallaxDepth{};
        float minimumSteps{};
        float maximumSteps{};
        float grazingClamp{};
        float fadeStart{};
        float fadeEnd{};
        float padding{};
    };
    static_assert(sizeof(FrameData) == 32);
    static_assert(alignof(FrameData) == 16);

    [[nodiscard]] inline FrameData makeFrameData(
        const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        constexpr std::array<float, 3> minimumSteps{ 8.0f, 12.0f, 16.0f };
        constexpr std::array<float, 3> maximumSteps{ 12.0f, 20.0f, 28.0f };
        const auto quality = static_cast<std::size_t>(safe.parallaxQuality);
        return {
            .enableComplexParallax = safe.parallaxEnabled ? 1u : 0u,
            .parallaxDepth = safe.parallaxDepth,
            .minimumSteps = minimumSteps[quality],
            .maximumSteps = maximumSteps[quality],
            .grazingClamp = safe.grazingClamp,
            .fadeStart = safe.fadeStart,
            .fadeEnd = safe.fadeEnd,
        };
    }
}
