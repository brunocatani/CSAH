#pragma once

#include <d3d11.h>

#include <cstddef>

namespace community_shaders::diagnostics::hdr_output_probe
{
    [[nodiscard]] bool install() noexcept;

    void onPixelShaderCreated(
        const void* bytecode,
        std::size_t bytecodeLength,
        ID3D11PixelShader* shader) noexcept;

    void onPixelShaderBound(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* shader) noexcept;
}
