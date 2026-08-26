#pragma once

namespace community_shaders::vanilla_fixes
{
    [[nodiscard]] bool installSunOcclusionNativeHook() noexcept;
    void setSunOcclusionFixEnabled(bool enabled) noexcept;
}
