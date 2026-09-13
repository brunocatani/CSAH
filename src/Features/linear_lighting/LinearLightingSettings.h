#pragma once

#include "Features/complex_materials/ComplexParallaxSettings.h"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace csah::linear_lighting
{
    constexpr float kNativeLightingResponseGamma = 2.2f;

    struct Settings
    {
        bool enabled{ false };
        // Skyrim's 1.8 response is retained for authored material colours.
        // Fallout's native light, ambient, fog, and sky producers use 2.2;
        // preserving that floor prevents low encoded values from being
        // amplified several times more than their daytime counterparts.
        bool preserveNativeDarkness{ true };

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
        float otherEffectMultiplier{ 1.0f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
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
        // Fixed compatibility slots. The active FO4VR Effect domain has no
        // independently selectable Blood, Projected, or Deferred contract,
        // but packaged replacement bytecode relies on the established b5 ABI.
        float bloodEffectMultiplier{ 1.0f };
        float projectedEffectMultiplier{ 1.0f };
        float deferredEffectMultiplier{ 1.0f };
        float otherEffectMultiplier{ 1.0f };
        std::uint32_t lightProducerGammaBits{
            std::bit_cast<std::uint32_t>(2.2f) };
        complex_materials::FrameData complexParallax{};
    };

    struct alignas(16) GeometryData
    {
        float emissiveMultiplier{ 1.0f };
        float padding[3]{};
    };

    static_assert(sizeof(FrameData) == 144);
    static_assert(alignof(FrameData) == 16);
    static_assert(sizeof(GeometryData) == 16);
    static_assert(alignof(GeometryData) == 16);
    static_assert(offsetof(FrameData, enableLinearLighting) == 0);
    static_assert(offsetof(FrameData, directionalLightRuntimeMultiplier) == 8);
    static_assert(offsetof(FrameData, colorGamma) == 16);
    static_assert(offsetof(FrameData, vanillaDiffuseColorMultiplier) == 60);
    static_assert(offsetof(FrameData, otherEffectMultiplier) == 104);
    static_assert(offsetof(FrameData, lightProducerGammaBits) == 108);
    static_assert(offsetof(FrameData, complexParallax) == 112);

    [[nodiscard]] Settings sanitize(const Settings& settings) noexcept;

    [[nodiscard]] constexpr float calibratedLightingResponseGamma(
        bool preserveNativeDarkness,
        float configuredGamma) noexcept
    {
        return preserveNativeDarkness &&
                configuredGamma < kNativeLightingResponseGamma ?
            kNativeLightingResponseGamma : configuredGamma;
    }

    [[nodiscard]] FrameData makeFrameData(
        const Settings& settings,
        bool runtimeEnabled,
        bool isDirectionalLightLinear,
        float directionalLightRuntimeMultiplier,
        float lightProducerGamma = 2.2f,
        const complex_materials::Settings& complexMaterialSettings = {}) noexcept;
}
