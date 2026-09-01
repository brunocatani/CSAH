#include "Features/vanilla_fixes/DirectionalLightDiagnosticSurface.h"

#include "support/Logger.h"

#include "VanillaFixesDirectionalDiagnosticCompositePS.h"
#include "VanillaFixesDirectionalDiagnosticCoverageVS.h"
#include "VanillaFixesLightingOwnershipBlackPS.h"
#include "VanillaFixesLightingOwnershipDiffusePS.h"
#include "VanillaFixesLightingOwnershipSpecularPS.h"
#include "VanillaFixesLightingOwnershipSslrPS.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <ranges>
#include <utility>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr UINT kLightingOwnershipFirstSlot = 4;
        constexpr std::size_t kLightingOwnershipResourceCount = 11;

        [[nodiscard]] bool setAndVerifyShaderResources(
            ID3D11DeviceContext* context,
            const std::array<ID3D11ShaderResourceView*,
                kLightingOwnershipResourceCount>& resources) noexcept
        {
            if (!context) {
                return false;
            }
            context->PSSetShaderResources(
                kLightingOwnershipFirstSlot,
                static_cast<UINT>(resources.size()),
                resources.data());
            std::array<ID3D11ShaderResourceView*,
                kLightingOwnershipResourceCount> verified{};
            context->PSGetShaderResources(
                kLightingOwnershipFirstSlot,
                static_cast<UINT>(verified.size()),
                verified.data());
            auto matches = true;
            for (std::size_t index = 0; index < verified.size(); ++index) {
                matches = matches && verified[index] == resources[index];
                if (verified[index]) {
                    verified[index]->Release();
                }
            }
            return matches;
        }

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

    ScopedLightingOwnershipCubemapBindings::
        ScopedLightingOwnershipCubemapBindings(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* black,
            ID3D11ShaderResourceView* white) noexcept :
        context_(context)
    {
        if (!context_ || !black || !white) {
            context_ = nullptr;
            return;
        }
        std::array<ID3D11ShaderResourceView*,
            kLightingOwnershipResourceCount> previous{};
        context_->PSGetShaderResources(
            kLightingOwnershipFirstSlot,
            static_cast<UINT>(previous.size()),
            previous.data());
        std::array<ID3D11ShaderResourceView*,
            kLightingOwnershipResourceCount> applied{};
        for (std::size_t index = 0; index < previous.size(); ++index) {
            previous_[index].Attach(previous[index]);
            applied[index] = previous_[index].Get();
        }
        applied[0] = black;  // t4 direct specular
        applied[1] = black;  // t5 direct diffuse
        applied[2] = black;  // t6 additive light
        applied[5] = white;  // t9 AO multiplier
        applied[6] = black;  // t10 scene colour
        applied[10] = black; // t14 screen-space reflection
        if (!setAndVerifyShaderResources(context_, applied)) {
            std::array<ID3D11ShaderResourceView*,
                kLightingOwnershipResourceCount> restore{};
            for (std::size_t index = 0; index < restore.size(); ++index) {
                restore[index] = previous_[index].Get();
            }
            (void)setAndVerifyShaderResources(context_, restore);
            context_ = nullptr;
            return;
        }
        active_ = true;
    }

    ScopedLightingOwnershipCubemapBindings::
        ~ScopedLightingOwnershipCubemapBindings() noexcept
    {
        if (!context_ || !active_) {
            return;
        }
        std::array<ID3D11ShaderResourceView*,
            kLightingOwnershipResourceCount> restore{};
        for (std::size_t index = 0; index < restore.size(); ++index) {
            restore[index] = previous_[index].Get();
        }
        if (setAndVerifyShaderResources(context_, restore)) {
            return;
        }
        const auto restoredOnRetry = setAndVerifyShaderResources(
            context_,
            restore);
        static std::atomic_bool firstRestoreFailureLogged{};
        if (!restoredOnRetry && !firstRestoreFailureLogged.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::error(
                "Exclusive cubemap ownership diagnostic could not restore the exact t4-through-t14 shader-resource bindings after one retry.");
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
        for (auto& shader : lightingOwnershipPixelShaders_) {
            shader.Reset();
        }
        lightingOwnershipBlackView_.Reset();
        lightingOwnershipBlackTexture_.Reset();
        lightingOwnershipWhiteView_.Reset();
        lightingOwnershipWhiteTexture_.Reset();
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
                "Exclusive directional synthetic coverage shader creation failed (HRESULT=0x{:08X}); modes 9 through 11 remain fail-closed.",
                static_cast<unsigned>(coverageVertexResult));
            return false;
        }
        compositeDevice_ = device;
        compositePixelShader_.Attach(shader);
        coverageVertexShader_.Attach(coverageVertexShader);
        const std::array<const unsigned char*, 4> ownershipBytecode{
            fo4vr_cs_vanilla_lighting_ownership_diffuse_ps,
            fo4vr_cs_vanilla_lighting_ownership_specular_ps,
            fo4vr_cs_vanilla_lighting_ownership_sslr_ps,
            fo4vr_cs_vanilla_lighting_ownership_black_ps,
        };
        const std::array<SIZE_T, 4> ownershipBytecodeLength{
            sizeof(fo4vr_cs_vanilla_lighting_ownership_diffuse_ps),
            sizeof(fo4vr_cs_vanilla_lighting_ownership_specular_ps),
            sizeof(fo4vr_cs_vanilla_lighting_ownership_sslr_ps),
            sizeof(fo4vr_cs_vanilla_lighting_ownership_black_ps),
        };
        auto ownershipShadersReady = true;
        for (std::size_t index = 0;
             index < lightingOwnershipPixelShaders_.size();
             ++index) {
            ID3D11PixelShader* ownershipShader{};
            const auto ownershipResult = createPixelShader(
                device,
                ownershipBytecode[index],
                ownershipBytecodeLength[index],
                nullptr,
                &ownershipShader);
            if (FAILED(ownershipResult) || !ownershipShader) {
                if (ownershipShader) {
                    ownershipShader->Release();
                }
                ownershipShadersReady = false;
                logging::error(
                    "Exclusive lighting ownership diagnostic shader {} creation failed (HRESULT=0x{:08X}); modes 12 through 15 remain fail-closed.",
                    index,
                    static_cast<unsigned>(ownershipResult));
                break;
            }
            lightingOwnershipPixelShaders_[index].Attach(ownershipShader);
        }
        if (!ownershipShadersReady) {
            for (auto& ownershipShader :
                 lightingOwnershipPixelShaders_) {
                ownershipShader.Reset();
            }
        }
        auto cubemapIsolationReady = ownershipShadersReady;
        if (cubemapIsolationReady) {
            D3D11_TEXTURE2D_DESC neutralDescription{};
            neutralDescription.Width = 1;
            neutralDescription.Height = 1;
            neutralDescription.MipLevels = 1;
            neutralDescription.ArraySize = 1;
            neutralDescription.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            neutralDescription.SampleDesc.Count = 1;
            neutralDescription.Usage = D3D11_USAGE_IMMUTABLE;
            neutralDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            constexpr std::array<std::uint16_t, 4> black{};
            const D3D11_SUBRESOURCE_DATA blackData{
                .pSysMem = black.data(),
                .SysMemPitch = static_cast<UINT>(sizeof(black)),
            };
            auto resourceResult = device->CreateTexture2D(
                &neutralDescription,
                &blackData,
                lightingOwnershipBlackTexture_.GetAddressOf());
            if (SUCCEEDED(resourceResult)) {
                resourceResult = device->CreateShaderResourceView(
                    lightingOwnershipBlackTexture_.Get(),
                    nullptr,
                    lightingOwnershipBlackView_.GetAddressOf());
            }
            neutralDescription.Format = DXGI_FORMAT_R32_FLOAT;
            constexpr float white = 1.0f;
            const D3D11_SUBRESOURCE_DATA whiteData{
                .pSysMem = &white,
                .SysMemPitch = sizeof(white),
            };
            if (SUCCEEDED(resourceResult)) {
                resourceResult = device->CreateTexture2D(
                    &neutralDescription,
                    &whiteData,
                    lightingOwnershipWhiteTexture_.GetAddressOf());
            }
            if (SUCCEEDED(resourceResult)) {
                resourceResult = device->CreateShaderResourceView(
                    lightingOwnershipWhiteTexture_.Get(),
                    nullptr,
                    lightingOwnershipWhiteView_.GetAddressOf());
            }
            if (FAILED(resourceResult) || !lightingOwnershipBlackView_ ||
                !lightingOwnershipWhiteView_) {
                cubemapIsolationReady = false;
                lightingOwnershipBlackView_.Reset();
                lightingOwnershipBlackTexture_.Reset();
                lightingOwnershipWhiteView_.Reset();
                lightingOwnershipWhiteTexture_.Reset();
                logging::error(
                    "Exclusive cubemap ownership diagnostic could not create its scoped black/white neutral resources (HRESULT=0x{:08X}); mode 15 remains fail-closed.",
                    static_cast<unsigned>(resourceResult));
            }
        }
        logging::info(
            "Exclusive directional diagnostic armed its generic packed-stereo t5 presenter; lighting ownership shaders armed={}, scoped stock-cubemap DFComposite isolation armed={}.",
            ownershipShadersReady,
            cubemapIsolationReady);
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

    ID3D11PixelShader*
        DirectionalLightDiagnosticSurface::lightingOwnershipPixelShader(
            const DirectionalLightDiagnosticMode mode,
            const bool environmentContract) const noexcept
    {
        if (!isLightingOwnershipDiagnostic(mode)) {
            return nullptr;
        }
        if (!environmentContract) {
            return lightingOwnershipPixelShaders_[3].Get();
        }
        if (mode == DirectionalLightDiagnosticMode::cubemapLookupOnly) {
            return nullptr;
        }
        const auto index = static_cast<std::size_t>(mode) -
            static_cast<std::size_t>(
                DirectionalLightDiagnosticMode::directDiffuseOnly);
        if (index >= 3) {
            return nullptr;
        }
        return lightingOwnershipPixelShaders_[index].Get();
    }

    ID3D11PixelShader* DirectionalLightDiagnosticSurface::
        lightingOwnershipBlackPixelShader() const noexcept
    {
        return lightingOwnershipPixelShaders_[3].Get();
    }

    ScopedLightingOwnershipCubemapBindings
        DirectionalLightDiagnosticSurface::scopeLightingOwnershipCubemap(
            ID3D11DeviceContext* context) noexcept
    {
        return ScopedLightingOwnershipCubemapBindings(
            context,
            lightingOwnershipBlackView_.Get(),
            lightingOwnershipWhiteView_.Get());
    }

    bool DirectionalLightDiagnosticSurface::isLightingOwnershipPixelShader(
        ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) {
            return false;
        }
        return std::ranges::any_of(
            lightingOwnershipPixelShaders_,
            [shader](const auto& candidate) {
                return candidate.Get() == shader;
            });
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
