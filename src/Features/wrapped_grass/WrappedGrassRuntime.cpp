#include "Features/wrapped_grass/WrappedGrassRuntime.h"

#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include "support/Logger.h"

namespace community_shaders::wrapped_grass
{
    namespace
    {
        constexpr UINT kConstantSlot = 11;
        constexpr UINT kSurfaceClassSlot = 47;

        struct alignas(16) GpuSettings
        {
            float enabled{};
            float wrapAmount{};
            float grassCode{ 1.0f / 255.0f };
            float codeTolerance{ 0.5f / 255.0f };
        };
        static_assert(sizeof(GpuSettings) == 16);
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
            kConstantSlot,
            1,
            previousConstants_.GetAddressOf());
        context_->PSGetShaderResources(
            kSurfaceClassSlot,
            1,
            previousSurfaceClass_.GetAddressOf());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        context_->PSSetShaderResources(
            kSurfaceClassSlot,
            1,
            &surfaceClass);
    }

    ScopedDrawBindings::~ScopedDrawBindings() noexcept
    {
        if (!context_) {
            return;
        }
        auto* previousSurfaceClass = previousSurfaceClass_.Get();
        auto* previousConstants = previousConstants_.Get();
        context_->PSSetShaderResources(
            kSurfaceClassSlot,
            1,
            &previousSurfaceClass);
        context_->PSSetConstantBuffers(
            kConstantSlot,
            1,
            &previousConstants);
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
        firstActiveDrawLogged_.store(false, std::memory_order_relaxed);
        if (!device || !context) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(GpuSettings);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto result = device->CreateBuffer(
            &description,
            nullptr,
            constants_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !constants_) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Wrapped Grass Lighting settings-buffer creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(result));
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Wrapped Grass Lighting GPU constants ready at b11; surface classes are consumed from t47.");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        wrapAmount_.store(safe.wrapAmount, std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);
        surface_classification::Runtime::get().setConsumerEnabled(
            surface_classification::Consumer::wrappedGrass,
            safe.enabled);
    }

    bool Runtime::requested() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
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
        const GpuSettings data{
            .enabled = active ? 1.0f : 0.0f,
            .wrapAmount = wrapAmount_.load(std::memory_order_relaxed),
        };
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
        if (active && !firstActiveDrawLogged_.exchange(
                          true, std::memory_order_relaxed)) {
            logging::info(
                "Wrapped Grass Lighting bound its directional-light class-mask consumer (class=1, wrap={:.3f}); producer coverage is reported separately.",
                wrapAmount_.load(std::memory_order_relaxed));
        }
        return ScopedDrawBindings(
            context,
            constants_.Get(),
            surfaceClass,
            &drawRestores_);
    }

    void Runtime::recordDrawFallback() noexcept
    {
        drawFallbacks_.fetch_add(1, std::memory_order_relaxed);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .wrapAmount = wrapAmount_.load(std::memory_order_relaxed),
            }),
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
