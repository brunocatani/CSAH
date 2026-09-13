#pragma once

#include "Features/vanilla_fixes/VanillaFixesSettings.h"

#include <cstdint>

namespace csah::vanilla_fixes
{
    struct RuntimeSnapshot final
    {
        Settings settings{};
        bool nativeContractValid{};
        std::uint64_t appliedPolicies{};
    };

    [[nodiscard]] bool startRuntime(const Settings& settings) noexcept;
    void applySettings(const Settings& settings) noexcept;
    void onGameDataReady() noexcept;
    [[nodiscard]] Settings activeSettings() noexcept;
    [[nodiscard]] bool focusShadowsEnabled() noexcept;
    [[nodiscard]] DirectionalLightDiagnosticMode
        directionalLightDiagnosticMode() noexcept;
    [[nodiscard]] RuntimeSnapshot runtimeSnapshot() noexcept;
}
