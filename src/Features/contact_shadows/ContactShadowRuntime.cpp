#include "Features/contact_shadows/ContactShadowRuntime.h"

#include "render/ComputeStateScope.h"
#include "support/Logger.h"

#include "ContactShadowMaskCS.h"
#include "ContactShadowsDFLight.h"

#include <cstring>
#include <utility>

namespace community_shaders::contact_shadows
{
    namespace
    {
        constexpr std::size_t kOriginalSize = 26152;
        constexpr std::array<std::byte, 16> kOriginalChecksum{
            std::byte{ 0x12 }, std::byte{ 0x28 }, std::byte{ 0x07 },
            std::byte{ 0x87 }, std::byte{ 0xD2 }, std::byte{ 0xA5 },
            std::byte{ 0x11 }, std::byte{ 0x0C }, std::byte{ 0x82 },
            std::byte{ 0x0F }, std::byte{ 0x43 }, std::byte{ 0x37 },
            std::byte{ 0x51 }, std::byte{ 0xC8 }, std::byte{ 0x46 },
            std::byte{ 0x48 },
        };
        constexpr UINT kDepthSlot = 3;
        constexpr UINT kMaskSlot = 46;
        constexpr UINT kConstantSlot = 13;
        constexpr UINT kDFLightConstantSlot = 2;
        constexpr UINT kStereoConstantSlot = 8;
        constexpr UINT kCameraConstantSlot = 12;
        constexpr UINT kThreadGroupWidth = 8;
        constexpr UINT kThreadGroupHeight = 8;

        struct alignas(16) GpuSettings
        {
            float strength{};
            float maxDistance{};
            float thickness{};
            float sampleCount{};
            float minimumThickness{ 0.35f };
            float selfIntersectionBias{ 0.08f };
            float outerSampleScale{ 0.50f };
            float foveated{};
            float fadeDistance{};
            float reserved0{};
            float reserved1{};
            float reserved2{};
        };
        static_assert(sizeof(GpuSettings) == 48);

        [[nodiscard]] bool matchesOriginal(
            const void* bytecode,
            SIZE_T bytecodeLength) noexcept
        {
            return bytecode && bytecodeLength == kOriginalSize &&
                std::memcmp(bytecode, "DXBC", 4) == 0 &&
                std::memcmp(
                    static_cast<const std::byte*>(bytecode) + 4,
                    kOriginalChecksum.data(),
                    kOriginalChecksum.size()) == 0;
        }
    }

    ScopedDrawBindings::ScopedDrawBindings(
        ID3D11DeviceContext* context,
        ID3D11Buffer* constants,
        ID3D11ShaderResourceView* mask,
        std::atomic_uint64_t* restoreCounter) noexcept :
        context_(context), restoreCounter_(restoreCounter)
    {
        if (!context_ || !constants || !mask) {
            context_ = nullptr;
            return;
        }
        context_->PSGetConstantBuffers(
            kConstantSlot,
            1,
            previousConstants_.GetAddressOf());
        context_->PSGetShaderResources(
            kMaskSlot,
            1,
            previousMask_.GetAddressOf());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        context_->PSSetShaderResources(kMaskSlot, 1, &mask);
    }

    ScopedDrawBindings::~ScopedDrawBindings() noexcept
    {
        if (!context_) {
            return;
        }
        auto* previousConstant = previousConstants_.Get();
        auto* previousMask = previousMask_.Get();
        context_->PSSetShaderResources(kMaskSlot, 1, &previousMask);
        context_->PSSetConstantBuffers(kConstantSlot, 1, &previousConstant);
        if (restoreCounter_) {
            restoreCounter_->fetch_add(1, std::memory_order_relaxed);
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        HRESULT(STDMETHODCALLTYPE* createPixelShader)(
            ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
            ID3D11PixelShader**)) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        device_ = device;
        context_ = context;
        replacement_.Reset();
        maskCompute_.Reset();
        constants_.Reset();
        maskTexture_.Reset();
        maskView_.Reset();
        maskOutput_.Reset();
        maskWidth_ = 0;
        maskHeight_ = 0;
        for (auto& original : originals_) {
            original.Reset();
        }
        trackedShaders_.store(0, std::memory_order_relaxed);
        uploadedRevision_ = 0;
        if (!device || !context || !createPixelShader) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        ID3D11PixelShader* replacement{};
        const auto pixelShaderResult = createPixelShader(
            device,
            fo4vr_cs_contact_shadows_dflight,
            sizeof(fo4vr_cs_contact_shadows_dflight),
            nullptr,
            &replacement);
        if (FAILED(pixelShaderResult) || !replacement) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows exact DFLight replacement creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(pixelShaderResult));
            return;
        }
        replacement_.Attach(replacement);

        const auto computeResult = device->CreateComputeShader(
            fo4vr_cs_contact_shadow_mask,
            sizeof(fo4vr_cs_contact_shadow_mask),
            nullptr,
            maskCompute_.ReleaseAndGetAddressOf());
        if (FAILED(computeResult) || !maskCompute_) {
            replacement_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows mask compute creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(computeResult));
            return;
        }

        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(GpuSettings);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto bufferResult = device->CreateBuffer(
            &description,
            nullptr,
            constants_.ReleaseAndGetAddressOf());
        if (FAILED(bufferResult) || !constants_) {
            replacement_.Reset();
            maskCompute_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows settings-buffer creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(bufferResult));
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Contact Shadows GPU mask pipeline ready; exact directional DFLight identity 12280787d2a5110c820f433751c84648 is armed fail-closed.");
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11PixelShader* shader) noexcept
    {
        if (!shader || !matchesOriginal(bytecode, bytecodeLength)) {
            return;
        }
        matchingShaders_.fetch_add(1, std::memory_order_relaxed);
        for (const auto& original : originals_) {
            if (original.Get() == shader) {
                return;
            }
        }
        for (auto& original : originals_) {
            if (!original) {
                original = shader;
                trackedShaders_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        failures_.fetch_add(1, std::memory_order_relaxed);
    }

    PixelShaderSelection Runtime::selectPixelShader(
        ID3D11PixelShader* requested) noexcept
    {
        if (!requested || !featureEnabled() || !replacement_ ||
            !maskCompute_ || !constants_) {
            return { requested, {} };
        }
        for (const auto& original : originals_) {
            if (original.Get() == requested) {
                replacementBinds_.fetch_add(1, std::memory_order_relaxed);
                return { replacement_.Get(), { requested, replacement_.Get() } };
            }
        }
        return { requested, {} };
    }

    void Runtime::uploadSettings(ID3D11DeviceContext* context) noexcept
    {
        const auto revision = settingsRevision_.load(std::memory_order_acquire);
        if (!context || !constants_ || revision == uploadedRevision_) {
            return;
        }
        const GpuSettings data{
            .strength = strength_.load(std::memory_order_relaxed),
            .maxDistance = maxDistance_.load(std::memory_order_relaxed),
            .thickness = thickness_.load(std::memory_order_relaxed),
            .sampleCount = static_cast<float>(
                sampleCount_.load(std::memory_order_relaxed)),
            .foveated = foveated_.load(std::memory_order_relaxed) ? 1.0f : 0.0f,
            .fadeDistance = fadeDistance_.load(std::memory_order_relaxed),
        };
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        uploadedRevision_ = revision;
    }

    bool Runtime::ensureMaskResources(
        ID3D11ShaderResourceView* depth) noexcept
    {
        if (!device_ || !depth) {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        depth->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (!resource || FAILED(resource.As(&texture)) || !texture) {
            return false;
        }
        D3D11_TEXTURE2D_DESC source{};
        texture->GetDesc(&source);
        if (source.Width < 2 || source.Height == 0 || (source.Width & 1u) != 0 ||
            source.ArraySize != 1 || source.SampleDesc.Count != 1) {
            return false;
        }
        if (maskTexture_ && maskView_ && maskOutput_ &&
            maskWidth_ == source.Width && maskHeight_ == source.Height) {
            return true;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = source.Width;
        description.Height = source.Height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags =
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> nextTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextView;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> nextOutput;
        const auto textureResult = device_->CreateTexture2D(
            &description,
            nullptr,
            nextTexture.GetAddressOf());
        const auto viewResult = SUCCEEDED(textureResult) ?
            device_->CreateShaderResourceView(
                nextTexture.Get(),
                nullptr,
                nextView.GetAddressOf()) : E_FAIL;
        const auto outputResult = SUCCEEDED(viewResult) ?
            device_->CreateUnorderedAccessView(
                nextTexture.Get(),
                nullptr,
                nextOutput.GetAddressOf()) : E_FAIL;
        if (FAILED(textureResult) || FAILED(viewResult) || FAILED(outputResult) ||
            !nextTexture || !nextView || !nextOutput) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows mask allocation failed for {}x{} (texture=0x{:08X}, SRV=0x{:08X}, UAV=0x{:08X}).",
                source.Width,
                source.Height,
                static_cast<std::uint32_t>(textureResult),
                static_cast<std::uint32_t>(viewResult),
                static_cast<std::uint32_t>(outputResult));
            return false;
        }

        maskTexture_ = std::move(nextTexture);
        maskView_ = std::move(nextView);
        maskOutput_ = std::move(nextOutput);
        maskWidth_ = source.Width;
        maskHeight_ = source.Height;
        maskRebuilds_.fetch_add(1, std::memory_order_relaxed);
        logging::info(
            "Contact Shadows stereo mask allocated at {}x{} R8_UNORM.",
            maskWidth_,
            maskHeight_);
        return true;
    }

    bool Runtime::dispatchMask(ID3D11DeviceContext* context) noexcept
    {
        if (!context || !maskCompute_ || !constants_) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth;
        Microsoft::WRL::ComPtr<ID3D11Buffer> dflight;
        Microsoft::WRL::ComPtr<ID3D11Buffer> stereo;
        Microsoft::WRL::ComPtr<ID3D11Buffer> camera;
        context->PSGetShaderResources(kDepthSlot, 1, depth.GetAddressOf());
        context->PSGetConstantBuffers(
            kDFLightConstantSlot,
            1,
            dflight.GetAddressOf());
        context->PSGetConstantBuffers(
            kStereoConstantSlot,
            1,
            stereo.GetAddressOf());
        context->PSGetConstantBuffers(
            kCameraConstantSlot,
            1,
            camera.GetAddressOf());
        if (!depth || !dflight || !stereo || !camera ||
            !ensureMaskResources(depth.Get())) {
            return false;
        }

        render::ScopedComputeState restore(
            context,
            {
                .firstShaderResource = 0,
                .shaderResourceCount = 1,
                .firstUnorderedAccess = 0,
                .unorderedAccessCount = 1,
                .firstConstantBuffer = kDFLightConstantSlot,
                .constantBufferCount =
                    kConstantSlot - kDFLightConstantSlot + 1,
            });
        if (!restore.captured()) {
            return false;
        }

        auto* depthView = depth.Get();
        auto* output = maskOutput_.Get();
        auto* dflightConstants = dflight.Get();
        auto* stereoConstants = stereo.Get();
        auto* cameraConstants = camera.Get();
        auto* settingsConstants = constants_.Get();
        context->CSSetShader(maskCompute_.Get(), nullptr, 0);
        context->CSSetShaderResources(0, 1, &depthView);
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->CSSetConstantBuffers(
            kDFLightConstantSlot,
            1,
            &dflightConstants);
        context->CSSetConstantBuffers(
            kStereoConstantSlot,
            1,
            &stereoConstants);
        context->CSSetConstantBuffers(
            kCameraConstantSlot,
            1,
            &cameraConstants);
        context->CSSetConstantBuffers(
            kConstantSlot,
            1,
            &settingsConstants);
        context->Dispatch(
            (maskWidth_ + kThreadGroupWidth - 1) / kThreadGroupWidth,
            (maskHeight_ + kThreadGroupHeight - 1) / kThreadGroupHeight,
            1);
        if (!restore.restore()) {
            return false;
        }
        maskDispatches_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    ScopedDrawBindings Runtime::scopeDraw(
        ID3D11DeviceContext* context,
        ShaderBinding binding) noexcept
    {
        if (!binding || binding.replacement != replacement_.Get() ||
            !featureEnabled() || !constants_) {
            return {};
        }
        uploadSettings(context);
        if (!dispatchMask(context)) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        drawScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedDrawBindings(
            context,
            constants_.Get(),
            maskView_.Get(),
            &drawRestores_);
    }

    void Runtime::recordDrawFallback() noexcept
    {
        drawFallbacks_.fetch_add(1, std::memory_order_relaxed);
    }

    bool Runtime::featureEnabled() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        foveated_.store(safe.foveated, std::memory_order_relaxed);
        strength_.store(safe.strength, std::memory_order_relaxed);
        maxDistance_.store(safe.maxDistance, std::memory_order_relaxed);
        fadeDistance_.store(safe.fadeDistance, std::memory_order_relaxed);
        thickness_.store(safe.thickness, std::memory_order_relaxed);
        sampleCount_.store(safe.sampleCount, std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .foveated = foveated_.load(std::memory_order_relaxed),
                .strength = strength_.load(std::memory_order_relaxed),
                .maxDistance = maxDistance_.load(std::memory_order_relaxed),
                .fadeDistance = fadeDistance_.load(std::memory_order_relaxed),
                .thickness = thickness_.load(std::memory_order_relaxed),
                .sampleCount = sampleCount_.load(std::memory_order_relaxed),
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .matchingShaders = matchingShaders_.load(std::memory_order_relaxed),
            .trackedShaders = trackedShaders_.load(std::memory_order_relaxed),
            .replacementBinds = replacementBinds_.load(std::memory_order_relaxed),
            .maskDispatches = maskDispatches_.load(std::memory_order_relaxed),
            .maskRebuilds = maskRebuilds_.load(std::memory_order_relaxed),
            .drawScopes = drawScopes_.load(std::memory_order_relaxed),
            .drawRestores = drawRestores_.load(std::memory_order_relaxed),
            .drawFallbacks = drawFallbacks_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
