#pragma once

#include <cstddef>
#include <cstdint>

namespace community_shaders::linear_lighting
{
    struct Settings
    {
        bool enabled{ false };

        float lightGamma{ 1.8f };
        float colorGamma{ 1.8f };
        float emitColorGamma{ 1.8f };
        float glowmapGamma{ 1.8f };
        float ambientGamma{ 1.8f };
        float fogGamma{ 1.97f };
        float fogAlphaGamma{ 1.8f };
        float effectGamma{ 1.4f };
        float effectAlphaGamma{ 1.55f };
        float skyGamma{ 1.8f };
        float waterGamma{ 1.8f };
        float volumetricLightingGamma{ 1.8f };

        float vanillaDiffuseColorMultiplier{ 1.0f };
        float directionalLightMultiplier{ 1.0f };
        float pointLightMultiplier{ 1.0f };
        float ambientMultiplier{ 1.0f };
        float emitColorMultiplier{ 1.0f };
        float glowmapMultiplier{ 0.66f };

        float effectLightingMultiplier{ 0.32f };
        float membraneEffectMultiplier{ 1.0f };
        float bloodEffectMultiplier{ 1.0f };
        float projectedEffectMultiplier{ 1.0f };
        float deferredEffectMultiplier{ 1.0f };
        float otherEffectMultiplier{ 1.0f };
    };

    struct alignas(16) FrameData
    {
        std::uint32_t enableLinearLighting{};
        std::uint32_t isDirectionalLightLinear{};
        float directionalLightRuntimeMultiplier{ 1.0f };
        float lightGamma{ 1.8f };
        float colorGamma{ 1.8f };
        float emitColorGamma{ 1.8f };
        float glowmapGamma{ 1.8f };
        float ambientGamma{ 1.8f };
        float fogGamma{ 1.97f };
        float fogAlphaGamma{ 1.8f };
        float effectGamma{ 1.4f };
        float effectAlphaGamma{ 1.55f };
        float skyGamma{ 1.8f };
        float waterGamma{ 1.8f };
        float volumetricLightingGamma{ 1.8f };
        float vanillaDiffuseColorMultiplier{ 1.0f };
        float directionalLightMultiplier{ 1.0f };
        float pointLightMultiplier{ 1.0f };
        float ambientMultiplier{ 1.0f };
        float emitColorMultiplier{ 1.0f };
        float glowmapMultiplier{ 0.66f };
        float effectLightingMultiplier{ 0.32f };
        float membraneEffectMultiplier{ 1.0f };
        float bloodEffectMultiplier{ 1.0f };
        float projectedEffectMultiplier{ 1.0f };
        float deferredEffectMultiplier{ 1.0f };
        float otherEffectMultiplier{ 1.0f };
        std::uint32_t padding{};
    };

    struct alignas(16) GeometryData
    {
        float emissiveMultiplier{ 1.0f };
        float padding[3]{};
    };

    static_assert(sizeof(FrameData) == 112);
    static_assert(alignof(FrameData) == 16);
    static_assert(sizeof(GeometryData) == 16);
    static_assert(alignof(GeometryData) == 16);
    static_assert(offsetof(FrameData, enableLinearLighting) == 0);
    static_assert(offsetof(FrameData, directionalLightRuntimeMultiplier) == 8);
    static_assert(offsetof(FrameData, colorGamma) == 16);
    static_assert(offsetof(FrameData, vanillaDiffuseColorMultiplier) == 60);
    static_assert(offsetof(FrameData, otherEffectMultiplier) == 104);
    static_assert(offsetof(FrameData, padding) == 108);

    [[nodiscard]] Settings sanitize(const Settings& settings) noexcept;

    [[nodiscard]] FrameData makeFrameData(
        const Settings& settings,
        bool runtimeEnabled,
        bool isDirectionalLightLinear,
        float directionalLightRuntimeMultiplier) noexcept;
}
