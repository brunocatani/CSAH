#include "Features/filmic_tonemapping/FilmicTonemappingRuntime.h"

#include "FilmicTonemappingPS.h"
#include "support/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace community_shaders::filmic_tonemapping
{
    namespace
    {
        constexpr SIZE_T kNativeBytecodeSize = 1848;
        constexpr UINT kConstantSlot = 12;
        constexpr std::array<std::uint8_t, 16> kNativeDxbcChecksum{
            0xF1, 0xBF, 0xA0, 0x42, 0xD5, 0x20, 0x62, 0xC4,
            0xAE, 0xDF, 0xD6, 0x12, 0xBB, 0xF4, 0x79, 0x9D,
        };

        [[nodiscard]] bool matchesNativeTonemap(
            const void* bytecode,
            SIZE_T bytecodeLength) noexcept
        {
            if (!bytecode || bytecodeLength != kNativeBytecodeSize) {
                return false;
            }
            const auto* bytes = static_cast<const std::uint8_t*>(bytecode);
            return std::memcmp(bytes, "DXBC", 4) == 0 &&
                std::memcmp(
                    bytes + 4,
                    kNativeDxbcChecksum.data(),
                    kNativeDxbcChecksum.size()) == 0;
        }
    }

    ScopedConstants::ScopedConstants(
        Runtime* owner,
        ID3D11DeviceContext* context,
        ID3D11Buffer* constants) noexcept :
        owner_(owner), context_(context)
    {
        if (!owner_ || !context_ || !constants) {
            owner_ = nullptr;
            context_ = nullptr;
            return;
        }
        ID3D11Buffer* previous{};
        context_->PSGetConstantBuffers(kConstantSlot, 1, &previous);
        previous_.Attach(previous);
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
    }

    ScopedConstants::~ScopedConstants() noexcept
    {
        if (!owner_ || !context_) {
            return;
        }
        auto* previous = previous_.Get();
        context_->PSSetConstantBuffers(kConstantSlot, 1, &previous);
        owner_->drawRestores_.fetch_add(1, std::memory_order_relaxed);
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        CreatePixelShaderFunction createPixelShader) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        trackedShaders_.store(0, std::memory_order_relaxed);
        for (auto& original : originals_) {
            original.Reset();
        }
        replacement_.Reset();
        constants_.Reset();
        device_ = device;
        context_ = context;
        uploadedRevision_ = 0;
        firstMatchLogged_.store(false, std::memory_order_relaxed);
        firstBindLogged_.store(false, std::memory_order_relaxed);
        if (!device || !context || !createPixelShader) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto initial = gpuConstants();
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(GpuConstants);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = &initial;
        auto result = device->CreateBuffer(
            &description, &data, constants_.ReleaseAndGetAddressOf());
        if (SUCCEEDED(result)) {
            result = createPixelShader(
                device,
                generated::kPixelShader.data(),
                generated::kPixelShader.size(),
                nullptr,
                replacement_.ReleaseAndGetAddressOf());
        }
        if (FAILED(result) || !constants_ || !replacement_) {
            constants_.Reset();
            replacement_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Filmic Tonemapping GPU resource creation failed (HRESULT=0x{:08X}); native HDR tonemapping remains bound.",
                static_cast<std::uint32_t>(result));
            return;
        }
        uploadedRevision_ = settingsRevision_.load(std::memory_order_acquire);
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Filmic Tonemapping replacement is GPU-ready for the unique FO4VR ImageSpace[027] HDR blend contract (CB12, no additional pass).");
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11PixelShader* shader) noexcept
    {
        if (!shader || !matchesNativeTonemap(bytecode, bytecodeLength)) {
            return;
        }
        matchingShaders_.fetch_add(1, std::memory_order_relaxed);
        const auto count = trackedShaders_.load(std::memory_order_acquire);
        for (std::uint32_t index = 0; index < count; ++index) {
            if (originals_[index].Get() == shader) {
                return;
            }
        }
        if (count >= originals_.size()) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Filmic Tonemapping exceeded its fixed original-shader capacity; the untracked shader remains native.");
            return;
        }
        originals_[count] = shader;
        trackedShaders_.store(count + 1, std::memory_order_release);
        if (!firstMatchLogged_.exchange(true, std::memory_order_relaxed)) {
            logging::info(
                "Filmic Tonemapping matched FO4VR ImageSpace[027] (1848 bytes, checksum f1bfa042d52062c4aedfd612bbf4799d).");
        }
    }

    PixelShaderSelection Runtime::selectPixelShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested) noexcept
    {
        if (!requested || !featureEnabled() || context != context_.Get() ||
            !replacement_) {
            return { requested, {} };
        }
        const auto count = trackedShaders_.load(std::memory_order_acquire);
        for (std::uint32_t index = 0; index < count; ++index) {
            if (originals_[index].Get() != requested) {
                continue;
            }
            replacementBinds_.fetch_add(1, std::memory_order_relaxed);
            if (!firstBindLogged_.exchange(true, std::memory_order_relaxed)) {
                logging::info(
                    "Filmic Tonemapping replaced its first native HDR blend bind; exposure, bloom, cinematic, fade, and bypass-mask inputs remain engine-owned.");
            }
            return {
                replacement_.Get(),
                { requested, replacement_.Get() },
            };
        }
        return { requested, {} };
    }

    ScopedConstants Runtime::scopeDraw(
        ID3D11DeviceContext* context,
        ShaderBinding binding) noexcept
    {
        if (!bindingActive(binding) || !context || context != context_.Get() ||
            !constants_) {
            return {};
        }
        uploadConstants(context);
        drawScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedConstants(this, context, constants_.Get());
    }

    bool Runtime::featureEnabled() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    bool Runtime::bindingActive(ShaderBinding binding) const noexcept
    {
        return binding && featureEnabled() && replacement_ &&
            binding.replacement == replacement_.Get();
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        nativeAutoExposure_.store(
            safe.useNativeAutoExposure, std::memory_order_relaxed);
        exposureCompensationEV_.store(
            safe.exposureCompensationEV, std::memory_order_relaxed);
        filmicStrength_.store(safe.filmicStrength, std::memory_order_relaxed);
        whitePointScale_.store(safe.whitePointScale, std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);
    }

    Runtime::GpuConstants Runtime::gpuConstants() const noexcept
    {
        return {
            .exposureMultiplier = std::exp2(
                exposureCompensationEV_.load(std::memory_order_relaxed)),
            .nativeAdaptationWeight = nativeAutoExposure_.load(
                std::memory_order_relaxed) ? 1.0f : 0.0f,
            .filmicStrength =
                filmicStrength_.load(std::memory_order_relaxed),
            .whitePointScale =
                whitePointScale_.load(std::memory_order_relaxed),
        };
    }

    void Runtime::uploadConstants(ID3D11DeviceContext* context) noexcept
    {
        const auto revision = settingsRevision_.load(std::memory_order_acquire);
        if (!context || !constants_ || uploadedRevision_ == revision) {
            return;
        }
        const auto data = gpuConstants();
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        uploadedRevision_ = revision;
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .useNativeAutoExposure = nativeAutoExposure_.load(
                    std::memory_order_relaxed),
                .exposureCompensationEV = exposureCompensationEV_.load(
                    std::memory_order_relaxed),
                .filmicStrength =
                    filmicStrength_.load(std::memory_order_relaxed),
                .whitePointScale =
                    whitePointScale_.load(std::memory_order_relaxed),
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .matchingShaders =
                matchingShaders_.load(std::memory_order_relaxed),
            .trackedShaders =
                trackedShaders_.load(std::memory_order_acquire),
            .replacementBinds =
                replacementBinds_.load(std::memory_order_relaxed),
            .drawScopes = drawScopes_.load(std::memory_order_relaxed),
            .drawRestores = drawRestores_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
