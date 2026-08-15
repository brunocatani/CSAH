#pragma once

#include <d3d11.h>

#include <cstdint>

namespace community_shaders::dlaa
{
    struct D3D11HookSnapshot
    {
        bool installed{};
        bool owned{};
        bool mapOwned{};
        bool unmapOwned{};
        std::uint64_t validationFailures{};
    };

    [[nodiscard]] bool installD3D11Hooks(
        ID3D11DeviceContext* context) noexcept;
    [[nodiscard]] bool validateD3D11Hooks(const char* trigger) noexcept;
    [[nodiscard]] D3D11HookSnapshot d3d11HookSnapshot() noexcept;
}
