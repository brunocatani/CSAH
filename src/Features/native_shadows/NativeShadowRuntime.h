#pragma once

#include "Features/native_shadows/NativeShadowSettings.h"

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
        std::uint32_t observedCascadeCount{};
        std::uint32_t observedShadowResolution{};
        float observedCascadeDistance{};
        float observedRendererDistance{};
        std::uint32_t lateAttempts{};
        std::uint32_t failures{};
    };

    [[nodiscard]] bool startRuntime(const Settings& settings) noexcept;
    void onGameDataReady() noexcept;
    void onWorldReady(const char* boundary) noexcept;
    [[nodiscard]] RuntimeSnapshot snapshot() noexcept;
}
