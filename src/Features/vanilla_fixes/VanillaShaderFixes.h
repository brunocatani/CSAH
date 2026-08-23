#pragma once

#include <d3d11.h>

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
        directionalLightRadialFade,
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
    [[nodiscard]] bool publishSslrPixelShaderPair(
        ID3D11PixelShader* fixedShader,
        ID3D11PixelShader* stockShader,
        ShaderFix fix) noexcept;
    [[nodiscard]] ID3D11PixelShader* selectSslrPixelShaderForBinding(
        ID3D11PixelShader* engineShader) noexcept;
    [[nodiscard]] ID3D11PixelShader* retainedStockSslrPixelShader(
        ID3D11PixelShader* correctedShader) noexcept;
    [[nodiscard]] bool isSslrRaytracePixelShader(
        ID3D11PixelShader* shader) noexcept;
    [[nodiscard]] bool publishDirectionalLightPixelShaderPair(
        ID3D11PixelShader* fixedShader,
        ID3D11PixelShader* stockShader,
        ShaderFix fix) noexcept;
    [[nodiscard]] ID3D11PixelShader*
        selectDirectionalLightPixelShaderForBinding(
            ID3D11PixelShader* engineShader) noexcept;
    void setDirectionalLightPitchFixRequested(bool requested) noexcept;
    void reportShaderCreationResult(
        const ShaderSelection& selection,
        bool accepted) noexcept;
    void reportFocusShaderCreated() noexcept;
    [[nodiscard]] ShaderFixSnapshot shaderFixSnapshot() noexcept;
}
