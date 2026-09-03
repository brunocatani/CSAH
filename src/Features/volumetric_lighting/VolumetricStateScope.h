#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>

namespace community_shaders::volumetric_lighting
{
    class StateScope final
    {
    public:
        explicit StateScope(ID3D11DeviceContext* context) noexcept;
        ~StateScope() noexcept;

        StateScope(const StateScope&) = delete;
        StateScope(StateScope&&) = delete;
        StateScope& operator=(const StateScope&) = delete;
        StateScope& operator=(StateScope&&) = delete;

    private:
        static constexpr std::size_t kMaximumClassInstances = 256;
        static constexpr UINT kPixelResources = 5;
        static constexpr UINT kPixelSamplers = 2;
        static constexpr UINT kPixelConstants = 5;
        static constexpr UINT kComputeResources = 3;
        static constexpr UINT kComputeSamplers = 2;
        static constexpr UINT kComputeConstants = 4;
        static constexpr UINT kComputeOutputs = 1;

        ID3D11DeviceContext* context_{};
        std::array<ID3D11RenderTargetView*,
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets_{};
        ID3D11DepthStencilView* depthStencil_{};
        Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
        D3D11_PRIMITIVE_TOPOLOGY topology_{
            D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED };

        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> geometryShader_;
        Microsoft::WRL::ComPtr<ID3D11HullShader> hullShader_;
        Microsoft::WRL::ComPtr<ID3D11DomainShader> domainShader_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> computeShader_;
        std::array<ID3D11ClassInstance*, kMaximumClassInstances> vertexClasses_{};
        std::array<ID3D11ClassInstance*, kMaximumClassInstances> pixelClasses_{};
        std::array<ID3D11ClassInstance*, kMaximumClassInstances> geometryClasses_{};
        std::array<ID3D11ClassInstance*, kMaximumClassInstances> hullClasses_{};
        std::array<ID3D11ClassInstance*, kMaximumClassInstances> domainClasses_{};
        std::array<ID3D11ClassInstance*, kMaximumClassInstances> computeClasses_{};
        UINT vertexClassCount_{};
        UINT pixelClassCount_{};
        UINT geometryClassCount_{};
        UINT hullClassCount_{};
        UINT domainClassCount_{};
        UINT computeClassCount_{};

        std::array<ID3D11ShaderResourceView*, kPixelResources> pixelResources_{};
        std::array<ID3D11SamplerState*, kPixelSamplers> pixelSamplers_{};
        std::array<ID3D11Buffer*, kPixelConstants> pixelConstants_{};
        std::array<ID3D11ShaderResourceView*, kComputeResources>
            computeResources_{};
        std::array<ID3D11SamplerState*, kComputeSamplers> computeSamplers_{};
        std::array<ID3D11Buffer*, kComputeConstants> computeConstants_{};
        std::array<ID3D11UnorderedAccessView*, kComputeOutputs>
            computeOutputs_{};

        Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
        FLOAT blendFactor_[4]{};
        UINT sampleMask_{ 0xFFFFFFFFu };
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState_;
        UINT stencilReference_{};
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterState_;
        std::array<D3D11_VIEWPORT,
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
            viewports_{};
        UINT viewportCount_{};
        Microsoft::WRL::ComPtr<ID3D11Predicate> predicate_;
        BOOL predicateValue_{};
    };
}
