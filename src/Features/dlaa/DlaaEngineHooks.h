#pragma once

#include <cstdint>

namespace community_shaders::dlaa
{
    struct EngineHookSnapshot
    {
        bool installed{};
        bool owned{};
        bool preRenderCallOwned{};
        bool postImageSpaceCallOwned{};
        std::uint64_t validationFailures{};
    };

    // Installs only after the FO4VR 1.2.72 executable gate has passed. Every
    // address below is guarded by the independently verified live bytes and
    // original destination before one owned transaction changes the image.
    [[nodiscard]] bool installEngineHooks() noexcept;
    [[nodiscard]] bool validateEngineHooks(const char* trigger) noexcept;
    [[nodiscard]] EngineHookSnapshot engineHookSnapshot() noexcept;
}
