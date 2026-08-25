#include "Features/vanilla_fixes/DirectionalLightDiagnosticSurface.h"

#include "support/Logger.h"

#include "VanillaFixesDirectionalDiagnosticCompositePS.h"
#include "VanillaFixesDirectionalDiagnosticCoverageVS.h"

#include <array>
#include <utility>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        [[nodiscard]] bool sameDevice(
            ID3D11Device* expected,
            ID3D11DeviceChild* child) noexcept
        {
            if (!expected || !child) {
                return false;
            }
            ComPtr<ID3D11Device> actual;
            child->GetDevice(&actual);
            return actual.Get() == expected;
        }
    }

    DirectionalLightDiagnosticSurface&
        DirectionalLightDiagnosticSurface::get() noexcept
    {
        static auto* instance = new DirectionalLightDiagnosticSurface();
        return *instance;
    }

    bool DirectionalLightDiagnosticSurface::onDeviceCreated(
        ID3D11Device* device,
        const CreatePixelShaderFunction createPixelShader,
        const CreateVertexShaderFunction createVertexShader) noexcept
    {
        resetSurface();
        compositePixelShader_.Reset();
        coverageVertexShader_.Reset();
        compositeDevice_.Reset();
        coverageDepthStencilState_.Reset();
        for (auto& state : coverageRasterizerStates_) {
            state = {};
        }
        firstCoverageRasterizerFailureLogged_ = false;
        if (!device || !createPixelShader || !createVertexShader) {
            return false;
        }
        D3D11_DEPTH_STENCIL_DESC coverageDepthDescription{};
        coverageDepthDescription.DepthEnable = FALSE;
        coverageDepthDescription.DepthWriteMask =
            D3D11_DEPTH_WRITE_MASK_ZERO;
        coverageDepthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
        coverageDepthDescription.StencilEnable = FALSE;
        const auto coverageDepthResult = device->CreateDepthStencilState(
            &coverageDepthDescription,
            coverageDepthStencilState_.GetAddressOf());
        if (FAILED(coverageDepthResult) || !coverageDepthStencilState_) {
            logging::error(
                "Exclusive directional coverage diagnostics could not create their depth-disabled state (HRESULT=0x{:08X}); modes 7 and 8 remain fail-closed.",
                static_cast<unsigned>(coverageDepthResult));
        }
        ID3D11PixelShader* shader{};
        const auto result = createPixelShader(
            device,
            fo4vr_cs_vanilla_directional_diagnostic_composite_ps,
            sizeof(fo4vr_cs_vanilla_directional_diagnostic_composite_ps),
            nullptr,
            &shader);
        if (FAILED(result) || !shader) {
            logging::error(
                "Exclusive directional diagnostic composite creation failed (HRESULT=0x{:08X}).",
                static_cast<unsigned>(result));
            return false;
        }
        ID3D11VertexShader* coverageVertexShader{};
        const auto coverageVertexResult = createVertexShader(
            device,
            fo4vr_cs_vanilla_directional_diagnostic_coverage_vs,
            sizeof(fo4vr_cs_vanilla_directional_diagnostic_coverage_vs),
            nullptr,
            &coverageVertexShader);
        if (FAILED(coverageVertexResult) || !coverageVertexShader) {
            shader->Release();
            logging::error(
                "Exclusive directional synthetic coverage shader creation failed (HRESULT=0x{:08X}); modes 9 and 10 remain fail-closed.",
                static_cast<unsigned>(coverageVertexResult));
            return false;
        }
        compositeDevice_ = device;
        compositePixelShader_.Attach(shader);
        coverageVertexShader_.Attach(coverageVertexShader);
        logging::info(
            "Exclusive directional diagnostic armed its generic packed-stereo t5 composite for the complete verified IBL DFComposite family.");
        return true;
    }

    DirectionalDiagnosticResources
        DirectionalLightDiagnosticSurface::prepareOutput(
            ID3D11DeviceContext* context,
            ID3D11RenderTargetView* referenceTarget) noexcept
    {
        if (!ensureResources(context, referenceTarget)) {
            return {};
        }
        constexpr std::array clear{ 0.0f, 0.0f, 0.0f, 0.0f };
        context->ClearRenderTargetView(renderTarget_.Get(), clear.data());
        return { renderTarget_, shaderResource_, width_, height_ };
    }

    DirectionalDiagnosticResources
        DirectionalLightDiagnosticSurface::currentInput(
            ID3D11DeviceContext* context) noexcept
    {
        if (!context || !device_ || !renderTarget_ || !shaderResource_) {
            return {};
        }
        ComPtr<ID3D11Device> currentDevice;
        context->GetDevice(&currentDevice);
        if (currentDevice.Get() != device_.Get()) {
            resetSurface();
            return {};
        }
        return { renderTarget_, shaderResource_, width_, height_ };
    }

    ID3D11PixelShader*
        DirectionalLightDiagnosticSurface::compositePixelShader() const noexcept
    {
        return compositePixelShader_.Get();
    }

    bool DirectionalLightDiagnosticSurface::isCompositePixelShader(
        ID3D11PixelShader* shader) const noexcept
    {
        return shader && shader == compositePixelShader_.Get();
    }

    ID3D11DepthStencilState* DirectionalLightDiagnosticSurface::
        coverageDepthStencilState() const noexcept
    {
        return coverageDepthStencilState_.Get();
    }

    ID3D11RasterizerState* DirectionalLightDiagnosticSurface::
        coverageRasterizerState(ID3D11RasterizerState* source) noexcept
    {
        if (!compositeDevice_) {
            return nullptr;
        }
        for (const auto& state : coverageRasterizerStates_) {
            if (state.sourceInitialized && state.source.Get() == source &&
                state.replacement) {
                return state.replacement.Get();
            }
        }

        D3D11_RASTERIZER_DESC description{};
        if (source) {
            source->GetDesc(&description);
        } else {
            description.FillMode = D3D11_FILL_SOLID;
            description.CullMode = D3D11_CULL_BACK;
            description.DepthClipEnable = TRUE;
        }
        description.FillMode = D3D11_FILL_SOLID;
        description.CullMode = D3D11_CULL_NONE;
        description.DepthClipEnable = FALSE;
        description.ScissorEnable = FALSE;

        ComPtr<ID3D11RasterizerState> replacement;
        const auto result = compositeDevice_->CreateRasterizerState(
            &description,
            replacement.GetAddressOf());
        if (FAILED(result) || !replacement) {
            if (!firstCoverageRasterizerFailureLogged_) {
                firstCoverageRasterizerFailureLogged_ = true;
                logging::error(
                    "Exclusive directional full-raster diagnostic could not create its state (HRESULT=0x{:08X}); mode 8 remains fail-closed.",
                    static_cast<unsigned>(result));
            }
            return nullptr;
        }
        for (auto& state : coverageRasterizerStates_) {
            if (state.sourceInitialized) {
                continue;
            }
            state.source = source;
            state.replacement = std::move(replacement);
            state.sourceInitialized = true;
            return state.replacement.Get();
        }
        if (!firstCoverageRasterizerFailureLogged_) {
            firstCoverageRasterizerFailureLogged_ = true;
            logging::error(
                "Exclusive directional coverage exceeded its fixed raster-state cache; synthetic coverage modes remain fail-closed for the unmatched state.");
        }
        return nullptr;
    }

    ID3D11VertexShader* DirectionalLightDiagnosticSurface::
        coverageVertexShader() const noexcept
    {
        return coverageVertexShader_.Get();
    }

    bool DirectionalLightDiagnosticSurface::ensureResources(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* referenceTarget) noexcept
    {
        if (!context || !referenceTarget) {
            return false;
        }
        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        ComPtr<ID3D11Resource> referenceResource;
        referenceTarget->GetResource(&referenceResource);
        ComPtr<ID3D11Texture2D> referenceTexture;
        if (!device || !referenceResource ||
            FAILED(referenceResource.As(&referenceTexture)) ||
            !referenceTexture || !sameDevice(device.Get(), referenceTarget)) {
            return false;
        }
        D3D11_TEXTURE2D_DESC referenceDescription{};
        referenceTexture->GetDesc(&referenceDescription);
        if (referenceDescription.Width == 0 ||
            referenceDescription.Height == 0 ||
            referenceDescription.SampleDesc.Count != 1 ||
            referenceDescription.ArraySize != 1) {
            return false;
        }
        if (device_.Get() == device.Get() && renderTarget_ && shaderResource_ &&
            width_ == referenceDescription.Width &&
            height_ == referenceDescription.Height) {
            return true;
        }

        resetSurface();
        D3D11_TEXTURE2D_DESC description{};
        description.Width = referenceDescription.Width;
        description.Height = referenceDescription.Height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags =
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> renderTarget;
        ComPtr<ID3D11ShaderResourceView> shaderResource;
        const auto textureResult = device->CreateTexture2D(
            &description,
            nullptr,
            &texture);
        const auto renderTargetResult = SUCCEEDED(textureResult) ?
            device->CreateRenderTargetView(
                texture.Get(),
                nullptr,
                &renderTarget) : E_FAIL;
        const auto shaderResourceResult = SUCCEEDED(renderTargetResult) ?
            device->CreateShaderResourceView(
                texture.Get(),
                nullptr,
                &shaderResource) : E_FAIL;
        if (FAILED(textureResult) || FAILED(renderTargetResult) ||
            FAILED(shaderResourceResult) || !texture || !renderTarget ||
            !shaderResource) {
            if (!firstFailureLogged_) {
                firstFailureLogged_ = true;
                logging::error(
                    "Exclusive directional diagnostic surface creation failed: texture=0x{:08X}, RTV=0x{:08X}, SRV=0x{:08X}.",
                    static_cast<unsigned>(textureResult),
                    static_cast<unsigned>(renderTargetResult),
                    static_cast<unsigned>(shaderResourceResult));
            }
            return false;
        }

        device_ = std::move(device);
        texture_ = std::move(texture);
        renderTarget_ = std::move(renderTarget);
        shaderResource_ = std::move(shaderResource);
        width_ = description.Width;
        height_ = description.Height;
        if (!firstReadyLogged_) {
            firstReadyLogged_ = true;
            logging::info(
                "Exclusive directional diagnostic allocated a private {}x{} packed-stereo R16G16B16A16_FLOAT surface; no engine lighting target is modified.",
                width_,
                height_);
        }
        return true;
    }

    void DirectionalLightDiagnosticSurface::resetSurface() noexcept
    {
        shaderResource_.Reset();
        renderTarget_.Reset();
        texture_.Reset();
        device_.Reset();
        width_ = 0;
        height_ = 0;
    }
}
