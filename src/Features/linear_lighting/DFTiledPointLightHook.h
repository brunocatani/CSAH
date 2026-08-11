#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"

#include <array>
#include <cstdint>

namespace community_shaders::linear_lighting
{
    struct PointLightProducerEnergySample
    {
        bool captured{};
        std::int32_t recordKind{};
        float range{};
        float gamma{};
        float multiplier{ 1.0f };
        std::array<float, 3> postGammaColor{};
        std::array<float, 3> finalColor{};
    };

    struct DFTiledPointLightHookSnapshot
    {
        bool installed{};
        bool detourOwned{};
        bool gammaLoadsOwned{};
        bool enabled{};
        std::uint64_t completedCalls{};
        std::uint64_t modifiedCalls{};
        std::uint64_t passThroughCalls{};
        std::uint64_t invalidColorSources{};
        std::uint64_t validationFailures{};
        float activeGamma{ 2.2f };
        float activeColorMultiplier{ 1.0f };
        PointLightProducerEnergySample energySample{};
    };

    // Called only after the existing D3D bootstrap has initialized MinHook.
    // The exact FO4VR 1.2.72 function, caller, gamma loads, and source constant
    // are all checked before the first mutation.
    [[nodiscard]] bool installDFTiledPointLightHook() noexcept;

    // Revalidates both the native detour and all three RIP-relative exponent
    // loads. A failed ownership check atomically restores vanilla 2.2 data.
    [[nodiscard]] bool validateDFTiledPointLightHook(
        const char* trigger) noexcept;

    // Publishes sanitized settings without locks or allocations. The record
    // hook samples only atomics on the render path.
    void publishDFTiledPointLightSettings(
        const Settings& settings) noexcept;

    [[nodiscard]] DFTiledPointLightHookSnapshot
    dFTiledPointLightHookSnapshot() noexcept;
}
