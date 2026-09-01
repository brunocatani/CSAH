#pragma once

#include "Features/native_shadows/NativeShadowSettings.h"

#include <cstddef>
#include <cstdint>

namespace community_shaders::native_shadows
{
    struct RuntimeSnapshot
    {
        Settings settings{};
        bool earlyContractAccepted{};
        bool cascadePatchesOwned{};
        bool tiledLightingPatchesOwned{};
        bool safetyCavesOwned{};
        bool lateCascadeStateReady{};
        bool fullCascadeMaskOwned{};
        bool tiledSettingForced{};
        bool fixedQualityForced{};
        std::uint32_t observedCascadeCount{};
        std::uint32_t observedShadowResolution{};
        std::uint32_t observedOrthographicShadowFilter{};
        float observedCascadeDistance{};
        float observedRendererDistance{};
        float observedCascadeBlendDistance{};
        std::uint32_t lateAttempts{};
        std::uint32_t failures{};
    };

    [[nodiscard]] bool startRuntime(const Settings& settings) noexcept;
    void onGameDataReady() noexcept;
    void onWorldReady(const char* boundary) noexcept;
    [[nodiscard]] std::size_t verifiedNodeAllocatorPatchPrefix(
        const void* entry) noexcept;
    [[nodiscard]] RuntimeSnapshot snapshot() noexcept;
}
