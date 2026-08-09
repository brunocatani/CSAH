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
        bool shaderInterceptionActive{};
        bool createPixelShaderDetourEnabled{};
        bool pixelShaderBindDetourEnabled{};
        std::uint64_t shaderHookInstallFailures{};
        std::uint64_t shaderHookValidationFailures{};
        std::uint64_t pixelShaderBindRecursions{};
        std::uint64_t deviceCreationCalls{};
        std::uint64_t pixelShaderCreationCalls{};
        std::uint64_t pixelShaderBindCalls{};
    };

    // Installs before Fallout4VR creates the D3D11 device. The import entry is
    // found by PE metadata and checked against the live d3d11 export first.
    [[nodiscard]] bool installEarlyD3D11Hooks() noexcept;
    // Verifies the live native method detours without mutating an unknown
    // downstream chain. Failed validation is rate-limited and fail-closed.
    [[nodiscard]] bool validateD3D11ShaderHooks(
        const char* trigger) noexcept;
    [[nodiscard]] HookSnapshot d3d11HookSnapshot() noexcept;
}
