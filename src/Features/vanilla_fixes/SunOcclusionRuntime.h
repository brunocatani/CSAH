#pragma once

namespace csah::vanilla_fixes
{
    [[nodiscard]] bool installSunOcclusionNativeHook() noexcept;
    void setSunOcclusionFixEnabled(bool enabled) noexcept;
}
