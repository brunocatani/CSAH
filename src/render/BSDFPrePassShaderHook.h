#pragma once

#include <cstdint>

namespace community_shaders::render
{
    struct DFPrePassDescriptorScope
    {
        std::uint32_t descriptor{};
        bool active{};
    };

    struct DFPrePassHookSnapshot
    {
        bool installed{};
        bool vtableCellOwned{};
        std::uint64_t setupCalls{};
        std::uint64_t validationFailures{};
        std::uint32_t lastDescriptor{};
    };

    // Fallout4VR.exe 1.2.72 BSDFPrePassShader slot 3 owns the complete
    // descriptor-qualified pixel-shader selection transaction. The hook is
    // guarded by the exact live vtable target and verified function bytes.
    [[nodiscard]] bool installBSDFPrePassShaderHook() noexcept;
    [[nodiscard]] bool validateBSDFPrePassShaderHook(
        const char* trigger) noexcept;

    // Render-thread hot-path read. The scope is active only while the
    // verified engine transaction calls PSSetShader; no engine pointer is
    // retained.
    [[nodiscard]] DFPrePassDescriptorScope
        activeDFPrePassDescriptorScope() noexcept;
    [[nodiscard]] DFPrePassHookSnapshot
        dFPrePassHookSnapshot() noexcept;
}
