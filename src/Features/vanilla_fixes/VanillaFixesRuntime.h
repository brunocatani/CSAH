#pragma once

#include "Features/vanilla_fixes/VanillaFixesSettings.h"

#include <cstdint>

namespace community_shaders::vanilla_fixes
{
    struct RuntimeSnapshot final
    {
        Settings settings{};
        bool nativeContractValid{};
        bool hotReloadActive{};
        std::uint64_t appliedPolicies{};
        std::uint64_t externalReloads{};
    };

    [[nodiscard]] bool startRuntime(const Settings& settings) noexcept;
    void applySettings(const Settings& settings) noexcept;
    [[nodiscard]] Settings activeSettings() noexcept;
    [[nodiscard]] bool focusShadowsEnabled() noexcept;
    [[nodiscard]] DirectionalLightDiagnosticMode
        directionalLightDiagnosticMode() noexcept;
    [[nodiscard]] RuntimeSnapshot runtimeSnapshot() noexcept;
}
