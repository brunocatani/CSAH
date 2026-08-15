#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace community_shaders::vanilla_fixes
{
    struct ShaderIdentity final
    {
        std::size_t bytecodeSize{};
        std::uint64_t hash{};
        std::array<std::uint32_t, 4> checksum{};
        bool hasDxbcHeader{};
    };

    enum class ShaderFix : std::uint8_t
    {
        none,
        saoRawAo,
        saoHorizontalBlur,
        sslrHorizontalBlur,
        sslrPrepass,
        sslrRaytrace,
        surfaceAnchoredCubemap,
    };

    struct ShaderSelection final
    {
        const void* data{};
        std::size_t size{};
        ShaderFix fix{ ShaderFix::none };
        bool replacementReady{};

        [[nodiscard]] bool targeted() const noexcept
        {
            return fix != ShaderFix::none;
        }

        [[nodiscard]] bool replaced() const noexcept
        {
            return targeted() && replacementReady;
        }
    };

    struct ShaderFixSnapshot final
    {
        std::uint64_t targeted{};
        std::uint64_t accepted{};
        std::uint64_t stockFallbacks{};
        std::uint64_t focusShadersCreated{};
    };

    [[nodiscard]] ShaderIdentity identifyShader(
        const void* bytecode,
        std::size_t bytecodeLength) noexcept;
    [[nodiscard]] bool isStockFocusShadowPixel(
        const ShaderIdentity& identity) noexcept;
    [[nodiscard]] ShaderSelection selectVertexShader(
        const void* bytecode,
        std::size_t bytecodeLength) noexcept;
    [[nodiscard]] ShaderSelection selectPixelShader(
        const void* bytecode,
        std::size_t bytecodeLength,
        std::vector<std::byte>& patchStorage) noexcept;
    [[nodiscard]] ShaderSelection selectComputeShader(
        const void* bytecode,
        std::size_t bytecodeLength) noexcept;
    void reportShaderCreationResult(
        const ShaderSelection& selection,
        bool accepted) noexcept;
    void reportFocusShaderCreated() noexcept;
    [[nodiscard]] ShaderFixSnapshot shaderFixSnapshot() noexcept;
}
