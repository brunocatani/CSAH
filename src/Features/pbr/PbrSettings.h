#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace csah::pbr
{
    struct Settings final
    {
        bool enabled{ true };
        bool legacyMaterials{ true };
        bool directGgx{ true };
        bool grassGgx{};
        bool environmentFresnel{ true };
        bool energyConservation{ true };
        bool multiscatterCompensation{ true };
        bool specularOcclusion{ true };
        float roughnessMultiplier{ 1.0f };
        float specularRoughnessBlend{ 1.0f };
        float baseF0Multiplier{ 0.32f };
        float minimumF0{ 0.02f };
        float cubemapToF0Multiplier{ 1.0f };
        float complexMaterialF0Multiplier{ 1.0f };
        float directLightingScale{ 1.0f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        const Settings defaults{};
        auto result = settings;
        result.roughnessMultiplier =
            std::isfinite(result.roughnessMultiplier) ?
            std::clamp(result.roughnessMultiplier, 0.05f, 4.0f) :
            defaults.roughnessMultiplier;
        result.specularRoughnessBlend =
            std::isfinite(result.specularRoughnessBlend) ?
            std::clamp(result.specularRoughnessBlend, 0.0f, 1.0f) :
            defaults.specularRoughnessBlend;
        result.baseF0Multiplier = std::isfinite(result.baseF0Multiplier) ?
            std::clamp(result.baseF0Multiplier, 0.0f, 4.0f) :
            defaults.baseF0Multiplier;
        result.minimumF0 = std::isfinite(result.minimumF0) ?
            std::clamp(result.minimumF0, 0.0f, 0.16f) :
            defaults.minimumF0;
        result.cubemapToF0Multiplier =
            std::isfinite(result.cubemapToF0Multiplier) ?
            std::clamp(result.cubemapToF0Multiplier, 0.0f, 4.0f) :
            defaults.cubemapToF0Multiplier;
        result.complexMaterialF0Multiplier =
            std::isfinite(result.complexMaterialF0Multiplier) ?
            std::clamp(result.complexMaterialF0Multiplier, 0.0f, 4.0f) :
            defaults.complexMaterialF0Multiplier;
        result.directLightingScale =
            std::isfinite(result.directLightingScale) ?
            std::clamp(result.directLightingScale, 0.0f, 4.0f) :
            defaults.directLightingScale;
        return result;
    }

    [[nodiscard]] inline bool effectiveEnabled(
        const Settings& settings,
        bool linearLightingEnabled) noexcept
    {
        return settings.enabled && linearLightingEnabled;
    }

    struct alignas(16) FrameData final
    {
        float enabled{};
        float legacyMaterials{};
        float directGgx{};
        float grassGgx{};
        float environmentFresnel{};
        float energyConservation{};
        float multiscatterCompensation{};
        float specularOcclusion{};
        float roughnessMultiplier{};
        float specularRoughnessBlend{};
        float baseF0Multiplier{};
        float minimumF0{};
        float cubemapToF0Multiplier{};
        float complexMaterialF0Multiplier{};
        float directLightingScale{};
        float reserved{};
    };
    static_assert(sizeof(FrameData) == 64);
    static_assert(alignof(FrameData) == 16);

    [[nodiscard]] inline FrameData makeFrameData(
        const Settings& settings,
        bool active = true) noexcept
    {
        const auto safe = sanitize(settings);
        return {
            .enabled = active && safe.enabled ? 1.0f : 0.0f,
            .legacyMaterials = safe.legacyMaterials ? 1.0f : 0.0f,
            .directGgx = safe.directGgx ? 1.0f : 0.0f,
            .grassGgx = safe.grassGgx ? 1.0f : 0.0f,
            .environmentFresnel = safe.environmentFresnel ? 1.0f : 0.0f,
            .energyConservation = safe.energyConservation ? 1.0f : 0.0f,
            .multiscatterCompensation =
                safe.multiscatterCompensation ? 1.0f : 0.0f,
            .specularOcclusion = safe.specularOcclusion ? 1.0f : 0.0f,
            .roughnessMultiplier = safe.roughnessMultiplier,
            .specularRoughnessBlend = safe.specularRoughnessBlend,
            .baseF0Multiplier = safe.baseF0Multiplier,
            .minimumF0 = safe.minimumF0,
            .cubemapToF0Multiplier = safe.cubemapToF0Multiplier,
            .complexMaterialF0Multiplier =
                safe.complexMaterialF0Multiplier,
            .directLightingScale = safe.directLightingScale,
        };
    }
}
