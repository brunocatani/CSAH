#pragma once

#include <cstdint>

namespace community_shaders::render
{
    struct HookSnapshot
    {
        bool deviceCreationImportInstalled{};
        bool deviceCreationImportOwned{};
        bool deviceCaptured{};
        bool deviceHooksInstalled{};
        bool createPixelShaderCellOwned{};
        bool pixelShaderBindCellOwned{};
        std::uint64_t pixelShaderBindRepairs{};
        std::uint64_t pixelShaderBindRepairFailures{};
        std::uint64_t pixelShaderBindRecursions{};
        std::uint64_t deviceCreationCalls{};
        std::uint64_t pixelShaderCreationCalls{};
        std::uint64_t pixelShaderBindCalls{};
    };

    // Installs before Fallout4VR creates the D3D11 device. The import entry is
    // found by PE metadata and checked against the live d3d11 export first.
    [[nodiscard]] bool installEarlyD3D11Hooks() noexcept;
    // Observes a displaced bind hook twice before mutating it. If bind calls
    // still reach this plugin, the existing downstream chain is preserved.
    [[nodiscard]] bool maintainD3D11ShaderBindHook(
        const char* trigger) noexcept;
    [[nodiscard]] HookSnapshot d3d11HookSnapshot() noexcept;
}
