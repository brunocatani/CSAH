#pragma once

#include <d3d11.h>

#include <array>

namespace csah::render
{
    struct ComputeStateFootprint
    {
        UINT firstShaderResource{};
        UINT shaderResourceCount{};
        UINT firstUnorderedAccess{};
        UINT unorderedAccessCount{};
        UINT firstSampler{};
        UINT samplerCount{};
        UINT firstConstantBuffer{};
        UINT constantBufferCount{};
    };

    // Retains and restores the exact compute bindings touched by one bounded
    // dispatch. Storage is fixed, construction is allocation-free, and the
    // scope does not retain any state beyond the render-thread transaction.
    class ScopedComputeState final
    {
    public:
        ScopedComputeState(
            ID3D11DeviceContext* context,
            ComputeStateFootprint footprint) noexcept;
        ~ScopedComputeState();

        ScopedComputeState(const ScopedComputeState&) = delete;
        ScopedComputeState& operator=(const ScopedComputeState&) = delete;

        [[nodiscard]] bool captured() const noexcept
        {
            return captured_;
        }

        [[nodiscard]] bool restore() noexcept;

    private:
        void releaseRetainedState() noexcept;

        ID3D11DeviceContext* context_{};
        ComputeStateFootprint footprint_{};
        ID3D11ComputeShader* shader_{};
        std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES>
            classInstances_{};
        UINT classInstanceCount_{};
        std::array<
            ID3D11ShaderResourceView*,
            D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT>
            shaderResources_{};
        std::array<
            ID3D11UnorderedAccessView*,
            D3D11_PS_CS_UAV_REGISTER_COUNT>
            unorderedAccessViews_{};
        std::array<
            ID3D11SamplerState*,
            D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT>
            samplers_{};
        std::array<
            ID3D11Buffer*,
            D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>
            constantBuffers_{};
        bool captured_{};
    };
}
