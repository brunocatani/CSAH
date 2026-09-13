#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace csah::volumetric_lighting
{
    struct Settings final
    {
        bool enabled{ true };
        std::uint32_t quality{ 2 };
        float intensity{ 1.0f };
        float baseScattering{};
        float shaftIntensity{ 1.35f };
        float densityContribution{ 0.50f };
        float densityScale{ 1.0f };
        float windSpeed{};
        float maxDistance{ 3000.0f };
        float temporalWeight{ 0.90f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        const Settings defaults{};
        auto result = settings;
        result.quality = std::clamp(result.quality, 0u, 2u);
        const auto finiteClamp = [](float value, float fallback, float minimum,
                                     float maximum) noexcept {
            return std::isfinite(value) ?
                std::clamp(value, minimum, maximum) : fallback;
        };
        result.intensity = finiteClamp(
            result.intensity, defaults.intensity, 0.0f, 4.0f);
        result.baseScattering = finiteClamp(
            result.baseScattering, defaults.baseScattering, 0.0f, 1.0f);
        result.shaftIntensity = finiteClamp(
            result.shaftIntensity, defaults.shaftIntensity, 0.0f, 4.0f);
        result.densityContribution = finiteClamp(
            result.densityContribution,
            defaults.densityContribution,
            0.0f,
            1.0f);
        result.densityScale = finiteClamp(
            result.densityScale, defaults.densityScale, 0.125f, 8.0f);
        result.windSpeed = finiteClamp(
            result.windSpeed, defaults.windSpeed, 0.0f, 100.0f);
        result.maxDistance = finiteClamp(
            result.maxDistance, defaults.maxDistance, 256.0f, 20000.0f);
        result.temporalWeight = finiteClamp(
            result.temporalWeight, defaults.temporalWeight, 0.0f, 0.98f);
        return result;
    }
}
