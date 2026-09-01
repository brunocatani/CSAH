#include "Features/pbr/PbrRuntime.h"

#include "Features/ibl/IblRuntime.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include "support/Logger.h"

namespace community_shaders::pbr
{
    namespace
    {
        constexpr UINT kConstantSlot = 7;
        constexpr UINT kSurfaceClassSlot = 47;
    }

    ScopedDrawBindings::ScopedDrawBindings(
        ID3D11DeviceContext* context,
        ID3D11Buffer* constants,
        ID3D11ShaderResourceView* surfaceClass,
        std::atomic_uint64_t* restoreCounter) noexcept :
        context_(context), restoreCounter_(restoreCounter)
    {
        if (!context_ || !constants) {
            context_ = nullptr;
            return;
        }
        context_->PSGetConstantBuffers(
            kConstantSlot, 1, previousConstants_.GetAddressOf());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        if (surfaceClass) {
            context_->PSGetShaderResources(
                kSurfaceClassSlot,
                1,
                previousSurfaceClass_.GetAddressOf());
            context_->PSSetShaderResources(
                kSurfaceClassSlot, 1, &surfaceClass);
            surfaceClassBound_ = true;
        }
    }

    ScopedDrawBindings::~ScopedDrawBindings() noexcept
    {
        if (!context_) {
            return;
        }
        auto* previousConstants = previousConstants_.Get();
        if (surfaceClassBound_) {
            auto* previousSurfaceClass = previousSurfaceClass_.Get();
            context_->PSSetShaderResources(
                kSurfaceClassSlot, 1, &previousSurfaceClass);
        }
        context_->PSSetConstantBuffers(
            kConstantSlot, 1, &previousConstants);
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
        ID3D11DeviceContext* context) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        device_ = device;
        context_ = context;
        constants_.Reset();
        uploadedRevision_ = 0;
        uploadedActive_ = false;
        if (!device || !context) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(FrameData);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto result = device->CreateBuffer(
            &description, nullptr, constants_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !constants_) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "PBR settings-buffer creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(result));
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "PBR direct-light constants ready at private b7; exact FO4VR directional DFLight contracts remain the draw owner.");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        configuredEnabled_.store(safe.enabled, std::memory_order_release);
        legacyMaterials_.store(safe.legacyMaterials, std::memory_order_relaxed);
        directGgx_.store(safe.directGgx, std::memory_order_relaxed);
        grassGgx_.store(safe.grassGgx, std::memory_order_relaxed);
        environmentFresnel_.store(
            safe.environmentFresnel, std::memory_order_relaxed);
        energyConservation_.store(
            safe.energyConservation, std::memory_order_relaxed);
        multiscatterCompensation_.store(
            safe.multiscatterCompensation, std::memory_order_relaxed);
        specularOcclusion_.store(
            safe.specularOcclusion, std::memory_order_relaxed);
        roughnessMultiplier_.store(
            safe.roughnessMultiplier, std::memory_order_relaxed);
        specularRoughnessBlend_.store(
            safe.specularRoughnessBlend, std::memory_order_relaxed);
        baseF0Multiplier_.store(
            safe.baseF0Multiplier, std::memory_order_relaxed);
        minimumF0_.store(safe.minimumF0, std::memory_order_relaxed);
        cubemapToF0Multiplier_.store(
            safe.cubemapToF0Multiplier, std::memory_order_relaxed);
        complexMaterialF0Multiplier_.store(
            safe.complexMaterialF0Multiplier, std::memory_order_relaxed);
        directLightingScale_.store(
            safe.directLightingScale, std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);
        publishEffectiveState(safe);
    }

    void Runtime::setLinearLightingEnabled(bool enabled) noexcept
    {
        linearLightingEnabled_.store(enabled, std::memory_order_release);
        settingsRevision_.fetch_add(1, std::memory_order_release);
        publishEffectiveState(settings());
    }

    void Runtime::publishEffectiveState(const Settings& settings) noexcept
    {
        const auto effective = effectiveEnabled(
            settings,
            linearLightingEnabled_.load(std::memory_order_acquire));
        surface_classification::Runtime::get().setConsumerEnabled(
            surface_classification::Consumer::pbr,
            effective);
        linear_lighting::Runtime::get().setPbrMaterialsEnabled(effective);
        auto effectiveSettings = settings;
        effectiveSettings.enabled = effective;
        ibl::Runtime::get().applyPbrSettings(effectiveSettings);
    }

    bool Runtime::requested() const noexcept
    {
        return configuredEnabled_.load(std::memory_order_acquire) &&
            linearLightingEnabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    Settings Runtime::settings() const noexcept
    {
        return sanitize({
            .enabled = configuredEnabled_.load(std::memory_order_acquire),
            .legacyMaterials = legacyMaterials_.load(std::memory_order_relaxed),
            .directGgx = directGgx_.load(std::memory_order_relaxed),
            .grassGgx = grassGgx_.load(std::memory_order_relaxed),
            .environmentFresnel = environmentFresnel_.load(
                std::memory_order_relaxed),
            .energyConservation = energyConservation_.load(
                std::memory_order_relaxed),
            .multiscatterCompensation = multiscatterCompensation_.load(
                std::memory_order_relaxed),
            .specularOcclusion = specularOcclusion_.load(
                std::memory_order_relaxed),
            .roughnessMultiplier = roughnessMultiplier_.load(
                std::memory_order_relaxed),
            .specularRoughnessBlend = specularRoughnessBlend_.load(
                std::memory_order_relaxed),
            .baseF0Multiplier = baseF0Multiplier_.load(
                std::memory_order_relaxed),
            .minimumF0 = minimumF0_.load(std::memory_order_relaxed),
            .cubemapToF0Multiplier = cubemapToF0Multiplier_.load(
                std::memory_order_relaxed),
            .complexMaterialF0Multiplier =
                complexMaterialF0Multiplier_.load(
                    std::memory_order_relaxed),
            .directLightingScale = directLightingScale_.load(
                std::memory_order_relaxed),
        });
    }

    void Runtime::uploadSettings(
        ID3D11DeviceContext* context,
        bool active) noexcept
    {
        const auto revision = settingsRevision_.load(std::memory_order_acquire);
        if (!context || !constants_ ||
            (revision == uploadedRevision_ && active == uploadedActive_)) {
            return;
        }
        const auto data = makeFrameData(settings(), active);
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        uploadedRevision_ = revision;
        uploadedActive_ = active;
        settingsUploads_.fetch_add(1, std::memory_order_relaxed);
    }

    ScopedDrawBindings Runtime::scopeDraw(
        ID3D11DeviceContext* context,
        bool active) noexcept
    {
        if (!context || context != context_.Get() || !constants_ ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return {};
        }
        auto* surfaceClass = active ?
            surface_classification::Runtime::get().shaderResourceView() :
            nullptr;
        if (active && !surfaceClass) {
            return {};
        }
        uploadSettings(context, active);
        drawScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedDrawBindings(
            context, constants_.Get(), surfaceClass, &drawRestores_);
    }

    void Runtime::recordDrawFallback() noexcept
    {
        drawFallbacks_.fetch_add(1, std::memory_order_relaxed);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = settings(),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .drawScopes = drawScopes_.load(std::memory_order_relaxed),
            .drawRestores = drawRestores_.load(std::memory_order_relaxed),
            .drawFallbacks = drawFallbacks_.load(std::memory_order_relaxed),
            .settingsUploads = settingsUploads_.load(
                std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
