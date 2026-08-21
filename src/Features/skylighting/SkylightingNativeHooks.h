#pragma once

namespace community_shaders::skylighting
{
    [[nodiscard]] bool installNativeHooks() noexcept;
    [[nodiscard]] bool validateNativeHooks(const char* trigger) noexcept;
}
