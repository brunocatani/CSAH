#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"

#include <cstdint>

namespace csah::linear_lighting
{
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
    };

    struct DFTiledPointLightProducerFrameState
    {
        std::uint64_t revision{};
        float gamma{ 2.2f };
    };

    // Called only after the existing D3D bootstrap has initialized MinHook.
    // The exact FO4VR 1.2.72 function, caller, gamma loads, and source constant
    // are all checked before the first mutation.
    [[nodiscard]] bool installDFTiledPointLightHook() noexcept;

    // Revalidates both the native detour and all seven RIP-relative exponent
    // loads. A failed ownership check atomically restores vanilla 2.2 data.
    [[nodiscard]] bool validateDFTiledPointLightHook(
        const char* trigger) noexcept;

    // Publishes sanitized settings without locks or allocations. The record
    // hook samples only atomics on the render path.
    void publishDFTiledPointLightSettings(
        const Settings& settings) noexcept;

    [[nodiscard]] DFTiledPointLightHookSnapshot
    dFTiledPointLightHookSnapshot() noexcept;

    // Lock-free state consumed at the render boundary. A revision change
    // republishes b5 so Effect shaders use the exponent that the verified
    // native producer actually applied.
    [[nodiscard]] DFTiledPointLightProducerFrameState
    dFTiledPointLightProducerFrameState() noexcept;
}
