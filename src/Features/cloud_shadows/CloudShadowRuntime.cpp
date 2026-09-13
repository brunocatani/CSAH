#include "Features/cloud_shadows/CloudShadowRuntime.h"

#include "support/Logger.h"

#include <algorithm>
#include <iterator>

namespace csah::cloud_shadows
{
    namespace
    {
        constexpr std::uint32_t kAllCubeFaces = (1u << 6u) - 1u;

    }

    ScopedCloudCapture::ScopedCloudCapture(
        Runtime* owner,
        ID3D11DeviceContext* context,
        std::uint32_t face,
        ID3D11RenderTargetView* cloudTarget,
        ID3D11BlendState* captureBlend) noexcept :
        owner_(owner), context_(context), face_(face)
    {
        if (!owner_ || !context_ || !cloudTarget || !captureBlend) {
            owner_ = nullptr;
            context_ = nullptr;
            return;
        }
        std::array<ID3D11RenderTargetView*, 8> rawTargets{};
        ID3D11DepthStencilView* rawDepth{};
        context_->OMGetRenderTargets(
            static_cast<UINT>(rawTargets.size()), rawTargets.data(), &rawDepth);
        for (std::size_t index = 0; index < rawTargets.size(); ++index) {
            previousTargets_[index].Attach(rawTargets[index]);
        }
        previousDepth_.Attach(rawDepth);

        ID3D11BlendState* rawBlend{};
        context_->OMGetBlendState(
            &rawBlend, previousBlendFactor_.data(), &previousSampleMask_);
        previousBlend_.Attach(rawBlend);

        rawTargets[3] = cloudTarget;
        context_->OMSetRenderTargets(
            static_cast<UINT>(rawTargets.size()),
            rawTargets.data(),
            previousDepth_.Get());
        context_->OMSetBlendState(
            captureBlend, previousBlendFactor_.data(), previousSampleMask_);
    }

    ScopedCloudCapture::~ScopedCloudCapture() noexcept
    {
        if (!context_ || !owner_) {
            return;
        }
        std::array<ID3D11RenderTargetView*, 8> rawTargets{};
        for (std::size_t index = 0; index < rawTargets.size(); ++index) {
            rawTargets[index] = previousTargets_[index].Get();
        }
        context_->OMSetRenderTargets(
            static_cast<UINT>(rawTargets.size()),
            rawTargets.data(),
            previousDepth_.Get());
        context_->OMSetBlendState(
            previousBlend_.Get(),
            previousBlendFactor_.data(),
            previousSampleMask_);
        owner_->markFaceRendered(face_);
        owner_->restores_.fetch_add(1, std::memory_order_relaxed);
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        device_ = device;
        context_ = context;
        cloudCube_.Reset();
        cloudView_.Reset();
        for (auto& target : cloudTargets_) {
            target.Reset();
        }
        sampler_.Reset();
        sourceBlend_.Reset();
        captureBlend_.Reset();
        cubeSize_ = 0;
        populatedFaces_.store(0, std::memory_order_relaxed);
        lastFace_ = 6;
        firstCaptureLogged_.store(false, std::memory_order_relaxed);
        firstCaptureRejectLogged_.store(false, std::memory_order_relaxed);
        firstCubeReadyLogged_.store(false, std::memory_order_relaxed);
        firstLightingBindLogged_.store(false, std::memory_order_relaxed);
        firstLightingRejectLogged_.store(false, std::memory_order_relaxed);
        if (!device || !context) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        const auto samplerResult = device->CreateSamplerState(
            &samplerDescription, sampler_.ReleaseAndGetAddressOf());
        if (FAILED(samplerResult) || !sampler_) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Cloud Shadows sampler creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(samplerResult));
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Cloud Shadows private cube capture ready; Sky descriptors 4/5/6 are armed without an engine vtable hook.");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        opacity_.store(safe.opacity, std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);
    }

    bool Runtime::requested() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    bool Runtime::ensureCaptureResources(
        const D3D11_TEXTURE2D_DESC& source) noexcept
    {
        if (!device_ || source.Width == 0 || source.Width != source.Height ||
            source.ArraySize < 6 || source.SampleDesc.Count != 1 ||
            (source.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) == 0) {
            return false;
        }
        if (cloudCube_ && cloudView_ && cubeSize_ == source.Width) {
            return true;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = source.Width;
        description.Height = source.Height;
        description.MipLevels = 1;
        description.ArraySize = 6;
        description.Format = DXGI_FORMAT_R8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags =
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 6> targets;
        auto result = device_->CreateTexture2D(
            &description, nullptr, texture.GetAddressOf());
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = description.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        viewDescription.TextureCube.MostDetailedMip = 0;
        viewDescription.TextureCube.MipLevels = 1;
        result = SUCCEEDED(result) ? device_->CreateShaderResourceView(
            texture.Get(), &viewDescription, view.GetAddressOf()) : result;
        for (std::uint32_t face = 0; SUCCEEDED(result) && face < 6; ++face) {
            D3D11_RENDER_TARGET_VIEW_DESC targetDescription{};
            targetDescription.Format = description.Format;
            targetDescription.ViewDimension =
                D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            targetDescription.Texture2DArray.MipSlice = 0;
            targetDescription.Texture2DArray.FirstArraySlice = face;
            targetDescription.Texture2DArray.ArraySize = 1;
            result = device_->CreateRenderTargetView(
                texture.Get(),
                &targetDescription,
                targets[face].GetAddressOf());
        }
        if (FAILED(result) || !texture || !view ||
            std::ranges::any_of(targets, [](const auto& target) {
                return !target;
            })) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Cloud Shadows cube allocation failed for {}x{} (HRESULT=0x{:08X}).",
                source.Width, source.Height,
                static_cast<std::uint32_t>(result));
            return false;
        }
        cloudCube_ = std::move(texture);
        cloudView_ = std::move(view);
        cloudTargets_ = std::move(targets);
        cubeSize_ = source.Width;
        populatedFaces_.store(0, std::memory_order_relaxed);
        lastFace_ = 6;
        cubeRebuilds_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool Runtime::ensureCaptureBlend(ID3D11BlendState* source) noexcept
    {
        if (!device_) {
            return false;
        }
        if (captureBlend_ && sourceBlend_.Get() == source) {
            return true;
        }
        D3D11_BLEND_DESC description{};
        if (source) {
            source->GetDesc(&description);
            if (!description.IndependentBlendEnable) {
                for (std::size_t index = 1;
                     index < std::size(description.RenderTarget); ++index) {
                    description.RenderTarget[index] = description.RenderTarget[0];
                }
            }
        } else {
            for (auto& target : description.RenderTarget) {
                target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            }
        }
        description.IndependentBlendEnable = TRUE;
        auto& cloud = description.RenderTarget[3];
        cloud.BlendEnable = TRUE;
        cloud.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        cloud.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        cloud.BlendOp = D3D11_BLEND_OP_ADD;
        cloud.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        cloud.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        cloud.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        cloud.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;

        Microsoft::WRL::ComPtr<ID3D11BlendState> next;
        const auto result = device_->CreateBlendState(
            &description, next.GetAddressOf());
        if (FAILED(result) || !next) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        sourceBlend_ = source;
        captureBlend_ = std::move(next);
        return true;
    }

    ScopedCloudCapture Runtime::scopeCapture(
        ID3D11DeviceContext* context,
        linear_lighting::ReplacementShaderBinding binding) noexcept
    {
        if (!requested() || !context || context != context_.Get() ||
            binding.family !=
                linear_lighting::ReplacementShaderFamily::sky ||
            binding.contractPlusOne < 5 || binding.contractPlusOne > 7) {
            return {};
        }
        // Sky contract N stores descriptor N-1. Contracts 5-7 therefore map
        // exactly to the Clouds, CloudsLerp, and CloudsFade descriptors 4-6.
        captureCandidates_.fetch_add(1, std::memory_order_relaxed);
        const auto reject = [this](const char* reason) noexcept {
            captureRejects_.fetch_add(1, std::memory_order_relaxed);
            if (!firstCaptureRejectLogged_.exchange(
                    true, std::memory_order_relaxed)) {
                logging::warn(
                    "Cloud Shadows rejected its first qualified Sky capture: {}.",
                    reason);
            }
            return ScopedCloudCapture{};
        };

        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
        context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
        if (!target) {
            return reject("render target 0 is null");
        }
        D3D11_RENDER_TARGET_VIEW_DESC targetDescription{};
        target->GetDesc(&targetDescription);
        if (targetDescription.ViewDimension !=
                D3D11_RTV_DIMENSION_TEXTURE2DARRAY ||
            targetDescription.Texture2DArray.MipSlice != 0 ||
            targetDescription.Texture2DArray.ArraySize != 1 ||
            targetDescription.Texture2DArray.FirstArraySlice >= 6) {
            return reject("render target 0 is not one reflection-cubemap face");
        }
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        target->GetResource(resource.GetAddressOf());
        if (!resource || FAILED(resource.As(&texture)) || !texture) {
            return reject("reflection target resource is not Texture2D");
        }
        D3D11_TEXTURE2D_DESC source{};
        texture->GetDesc(&source);
        if (!ensureCaptureResources(source)) {
            return reject("private cloud cube allocation contract failed");
        }

        Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
        context->OMGetBlendState(blend.GetAddressOf(), nullptr, nullptr);
        if (!ensureCaptureBlend(blend.Get())) {
            return reject("private MRT3 blend-state creation failed");
        }
        const auto face = targetDescription.Texture2DArray.FirstArraySlice;
        if (lastFace_ != face) {
            constexpr std::array<float, 4> clear{};
            context->ClearRenderTargetView(
                cloudTargets_[face].Get(), clear.data());
            populatedFaces_.fetch_and(
                ~(1u << face), std::memory_order_relaxed);
            lastFace_ = face;
        }
        capturedDraws_.fetch_add(1, std::memory_order_relaxed);
        if (!firstCaptureLogged_.exchange(true, std::memory_order_relaxed)) {
            logging::info(
                "Cloud Shadows captured its first qualified cloud draw (descriptor={}, face={}, cubeSize={}).",
                binding.contractPlusOne - 1,
                face,
                cubeSize_);
        }
        return ScopedCloudCapture(
            this,
            context,
            face,
            cloudTargets_[face].Get(),
            captureBlend_.Get());
    }

    void Runtime::markFaceRendered(std::uint32_t face) noexcept
    {
        if (face < 6) {
            const auto populated = populatedFaces_.fetch_or(
                                       1u << face,
                                       std::memory_order_relaxed) |
                (1u << face);
            if (populated == kAllCubeFaces &&
                !firstCubeReadyLogged_.exchange(
                    true, std::memory_order_relaxed)) {
                logging::info(
                    "Cloud Shadows populated all six private cubemap faces; directional-light sampling is armed.");
            }
        }
    }

    bool Runtime::prepareLighting(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView*& cube,
        ID3D11SamplerState*& sampler,
        float& opacity) noexcept
    {
        cube = nullptr;
        sampler = nullptr;
        opacity = 0.0f;
        const auto populatedFaces =
            populatedFaces_.load(std::memory_order_relaxed);
        if (!requested() || !context || context != context_.Get() ||
            populatedFaces != kAllCubeFaces || !cloudView_ || !sampler_) {
            lightingRejects_.fetch_add(1, std::memory_order_relaxed);
            if (!firstLightingRejectLogged_.exchange(
                    true, std::memory_order_relaxed)) {
                logging::warn(
                    "Cloud Shadows rejected its first lighting bind (requested={}, contextMatch={}, faceMask=0x{:02X}, cubeView={}, sampler={}).",
                    requested(),
                    context && context == context_.Get(),
                    populatedFaces,
                    cloudView_ != nullptr,
                    sampler_ != nullptr);
            }
            return false;
        }
        cube = cloudView_.Get();
        sampler = sampler_.Get();
        opacity = opacity_.load(std::memory_order_relaxed);
        lightingBinds_.fetch_add(1, std::memory_order_relaxed);
        if (!firstLightingBindLogged_.exchange(
                true, std::memory_order_relaxed)) {
            logging::info(
                "Cloud Shadows bound the completed private cube to directional lighting (opacity={:.3f}).",
                opacity);
        }
        return true;
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .opacity = opacity_.load(std::memory_order_relaxed),
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .cubeReady = populatedFaces_.load(std::memory_order_relaxed) ==
                kAllCubeFaces,
            .populatedFaceMask =
                populatedFaces_.load(std::memory_order_relaxed),
            .captureCandidates =
                captureCandidates_.load(std::memory_order_relaxed),
            .capturedDraws = capturedDraws_.load(std::memory_order_relaxed),
            .captureRejects =
                captureRejects_.load(std::memory_order_relaxed),
            .cubeRebuilds = cubeRebuilds_.load(std::memory_order_relaxed),
            .lightingBinds = lightingBinds_.load(std::memory_order_relaxed),
            .lightingRejects =
                lightingRejects_.load(std::memory_order_relaxed),
            .restores = restores_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
