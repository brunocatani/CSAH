#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace community_shaders::vanilla_fixes
{
    struct DirectionalDiagnosticResources final
    {
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return renderTarget && shaderResource;
        }
    };

    // Render-thread-only owner for the private packed-stereo directional
    // diagnostic surface. Resource creation occurs only on first use or an
    // output-size/device transition; ordinary diagnostic draws allocate
    // nothing and perform no readback.
    class DirectionalLightDiagnosticSurface final
    {
    public:
        using CreatePixelShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11PixelShader**);

        [[nodiscard]] static DirectionalLightDiagnosticSurface& get() noexcept;

        [[nodiscard]] bool onDeviceCreated(
            ID3D11Device* device,
            CreatePixelShaderFunction createPixelShader) noexcept;
        [[nodiscard]] DirectionalDiagnosticResources prepareOutput(
            ID3D11DeviceContext* context,
            ID3D11RenderTargetView* referenceTarget) noexcept;
        [[nodiscard]] DirectionalDiagnosticResources currentInput(
            ID3D11DeviceContext* context) noexcept;
        [[nodiscard]] ID3D11PixelShader* compositePixelShader() const noexcept;
        [[nodiscard]] bool isCompositePixelShader(
            ID3D11PixelShader* shader) const noexcept;

    private:
        DirectionalLightDiagnosticSurface() = default;

        [[nodiscard]] bool ensureResources(
            ID3D11DeviceContext* context,
            ID3D11RenderTargetView* referenceTarget) noexcept;
        void resetSurface() noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource_;
        Microsoft::WRL::ComPtr<ID3D11Device> compositeDevice_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> compositePixelShader_;
        UINT width_{};
        UINT height_{};
        bool firstReadyLogged_{};
        bool firstFailureLogged_{};
    };
}
