#include "Features/vanilla_fixes/SslrEnvironmentBinding.h"

#include "Features/ibl/IblRuntime.h"
#include "support/Logger.h"

#include <atomic>
#include <cstring>
#include <utility>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr UINT kFirstResourceSlot = 4;
        constexpr UINT kResourceCount = 6;
        constexpr UINT kSamplerSlot = 4;
        constexpr UINT kConstantSlot = 11;
        constexpr float kCurrentHitMinimumConfidence = 0.65f;

        struct Float3 final
        {
            float x{};
            float y{};
            float z{};
        };

        struct SslrEnvironmentConstants final
        {
            Float3 publishedProbeOrigin{};
            float publishedProbeOriginValid{};
            Float3 previousProbeOrigin{};
            float previousProbeOriginValid{};
            float transitionWeight{ 1.0f };
            float previousAvailable{};
            float currentHitMinimumConfidence{
                kCurrentHitMinimumConfidence
            };
            float publishedAvailable{};
        };

        static_assert(sizeof(SslrEnvironmentConstants) == 48);

        [[nodiscard]] Float3 copy(
            const ibl::Float3& value) noexcept
        {
            return { value.x, value.y, value.z };
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

        [[nodiscard]] bool sameDevice(
            ID3D11Device* expected,
            ID3D11ShaderResourceView* view) noexcept
        {
            if (!expected || !view) {
                return false;
            }
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            return resource && sameDevice(expected, resource.Get());
        }

        class Service final
        {
        public:
            [[nodiscard]] static Service& get() noexcept
            {
                static auto* service = new Service();
                return *service;
            }

            [[nodiscard]] bool initialize(
                ID3D11Device* device,
                ID3D11DeviceContext* context) noexcept
            {
                ready_.store(false, std::memory_order_release);
                failed_.store(false, std::memory_order_release);
                device_.Reset();
                context_.Reset();
                constants_.Reset();
                sampler_.Reset();
                bindings_.store(0, std::memory_order_relaxed);
                historyBindings_.store(0, std::memory_order_relaxed);
                bindingFailures_.store(0, std::memory_order_relaxed);
                restoreFailures_.store(0, std::memory_order_relaxed);
                firstHistoryBindingLogged_.store(
                    false,
                    std::memory_order_relaxed);
                if (!device || !context) {
                    markFailure("null device or context");
                    return false;
                }

                D3D11_BUFFER_DESC bufferDescription{};
                bufferDescription.ByteWidth =
                    sizeof(SslrEnvironmentConstants);
                bufferDescription.Usage = D3D11_USAGE_DEFAULT;
                bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                ComPtr<ID3D11Buffer> constants;
                if (FAILED(device->CreateBuffer(
                        &bufferDescription,
                        nullptr,
                        &constants)) || !constants) {
                    markFailure("b11 creation");
                    return false;
                }

                D3D11_SAMPLER_DESC samplerDescription{};
                samplerDescription.Filter =
                    D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                samplerDescription.AddressU =
                    D3D11_TEXTURE_ADDRESS_CLAMP;
                samplerDescription.AddressV =
                    D3D11_TEXTURE_ADDRESS_CLAMP;
                samplerDescription.AddressW =
                    D3D11_TEXTURE_ADDRESS_CLAMP;
                samplerDescription.MaxAnisotropy = 1;
                samplerDescription.ComparisonFunc =
                    D3D11_COMPARISON_NEVER;
                samplerDescription.MinLOD = 0.0f;
                samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
                ComPtr<ID3D11SamplerState> sampler;
                if (FAILED(device->CreateSamplerState(
                        &samplerDescription,
                        &sampler)) || !sampler) {
                    markFailure("s4 creation");
                    return false;
                }

                device_ = device;
                context_ = context;
                constants_ = std::move(constants);
                sampler_ = std::move(sampler);
                SslrEnvironmentConstants initial{};
                context->UpdateSubresource(
                    constants_.Get(),
                    0,
                    nullptr,
                    &initial,
                    0,
                    0);
                ready_.store(true, std::memory_order_release);
                logging::info(
                    "Vanilla Fixes stable-reflection binding initialized; corrected SSLR consumes Community Shaders' shared environment through private t4..t9/s4/b11 state.");
                return true;
            }

            void setRequested(const bool requested) noexcept
            {
                requested_.store(requested, std::memory_order_release);
                ibl::Runtime::get().setSslrConsumerEnabled(requested);
            }

            [[nodiscard]] bool ready() const noexcept
            {
                return requested_.load(std::memory_order_acquire) &&
                    infrastructureReady();
            }

            [[nodiscard]] bool prepare(
                ID3D11DeviceContext* context,
                std::array<ID3D11ShaderResourceView*, 6>& resources,
                ID3D11SamplerState*& sampler,
                ID3D11Buffer*& constants,
                bool& historyAvailable) noexcept
            {
                resources = {};
                sampler = nullptr;
                constants = nullptr;
                historyAvailable = false;
                if (!infrastructureReady()) {
                    return false;
                }
                if (!context || context != context_.Get() ||
                    !device_ || !constants_ || !sampler_) {
                    markFailure("draw context or owned resource mismatch");
                    return false;
                }

                ibl::SslrEnvironmentView environment;
                if (!requested_.load(std::memory_order_acquire) ||
                    !ibl::Runtime::get().tryGetSslrEnvironment(environment)) {
                    // The consumer is valid before the first environment is
                    // published. Explicit zero availability keeps the
                    // corrected raytrace on current-eye data only.
                    environment = {};
                }

                resources = {
                    environment.publishedEnvironment,
                    environment.publishedValidity,
                    environment.publishedPosition,
                    environment.previousEnvironment,
                    environment.previousValidity,
                    environment.previousPosition,
                };
                for (auto* resource : resources) {
                    if (resource && !sameDevice(device_.Get(), resource)) {
                        markFailure("shared environment device mismatch");
                        resources = {};
                        return false;
                    }
                }

                const SslrEnvironmentConstants values{
                    .publishedProbeOrigin = copy(
                        environment.publishedProbeOrigin.position),
                    .publishedProbeOriginValid =
                        environment.publishedProbeOrigin.valid ? 1.0f : 0.0f,
                    .previousProbeOrigin = copy(
                        environment.previousProbeOrigin.position),
                    .previousProbeOriginValid =
                        environment.previousProbeOrigin.valid ? 1.0f : 0.0f,
                    .transitionWeight = environment.transitionWeight,
                    .previousAvailable =
                        environment.previousAvailable ? 1.0f : 0.0f,
                    .currentHitMinimumConfidence =
                        kCurrentHitMinimumConfidence,
                    .publishedAvailable =
                        environment.publishedAvailable ? 1.0f : 0.0f,
                };
                context->UpdateSubresource(
                    constants_.Get(),
                    0,
                    nullptr,
                    &values,
                    0,
                    0);
                sampler = sampler_.Get();
                constants = constants_.Get();
                historyAvailable = environment.publishedAvailable;
                return true;
            }

            void recordBinding(const bool historyAvailable) noexcept
            {
                bindings_.fetch_add(1, std::memory_order_relaxed);
                if (historyAvailable) {
                    historyBindings_.fetch_add(1, std::memory_order_relaxed);
                    if (!firstHistoryBindingLogged_.exchange(
                            true,
                            std::memory_order_relaxed)) {
                        logging::info(
                            "Vanilla Fixes corrected SSLR consumed its first atomically published Community Shaders radiance/validity/position environment; visible hits remain confidence-owned and misses now retain world radiance.");
                    }
                }
            }

            void recordBindingFailure(const char* reason) noexcept
            {
                bindingFailures_.fetch_add(1, std::memory_order_relaxed);
                markFailure(reason);
            }

            void recordRestoreFailure() noexcept
            {
                restoreFailures_.fetch_add(1, std::memory_order_relaxed);
                markFailure("state restoration");
            }

            [[nodiscard]] SslrEnvironmentSnapshot snapshot() const noexcept
            {
                return {
                    .requested = requested_.load(std::memory_order_acquire),
                    .resourcesReady = ready_.load(
                        std::memory_order_acquire),
                    .failed = failed_.load(std::memory_order_acquire),
                    .bindings = bindings_.load(std::memory_order_relaxed),
                    .historyBindings = historyBindings_.load(
                        std::memory_order_relaxed),
                    .bindingFailures = bindingFailures_.load(
                        std::memory_order_relaxed),
                    .restoreFailures = restoreFailures_.load(
                        std::memory_order_relaxed),
                };
            }

        private:
            [[nodiscard]] bool infrastructureReady() const noexcept
            {
                return ready_.load(std::memory_order_acquire) &&
                    !failed_.load(std::memory_order_acquire);
            }

            void markFailure(const char* reason) noexcept
            {
                const auto first = !failed_.exchange(
                    true,
                    std::memory_order_acq_rel);
                if (first) {
                    logging::error(
                        "Vanilla Fixes stable-reflection binding failed at '{}'; later prepass/raytrace binds fall back to their retained stock pair.",
                        reason ? reason : "unknown");
                }
            }

            ComPtr<ID3D11Device> device_;
            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<ID3D11Buffer> constants_;
            ComPtr<ID3D11SamplerState> sampler_;
            std::atomic_bool requested_{};
            std::atomic_bool ready_{};
            std::atomic_bool failed_{};
            std::atomic_uint64_t bindings_{};
            std::atomic_uint64_t historyBindings_{};
            std::atomic_uint64_t bindingFailures_{};
            std::atomic_uint64_t restoreFailures_{};
            std::atomic_bool firstHistoryBindingLogged_{};
        };

        [[nodiscard]] bool appliedMatches(
            ID3D11DeviceContext* context,
            const std::array<ID3D11ShaderResourceView*, 6>& resources,
            ID3D11SamplerState* sampler,
            ID3D11Buffer* constants) noexcept
        {
            std::array<ID3D11ShaderResourceView*, 6> appliedResources{};
            context->PSGetShaderResources(
                kFirstResourceSlot,
                kResourceCount,
                appliedResources.data());
            ID3D11SamplerState* appliedSampler{};
            context->PSGetSamplers(kSamplerSlot, 1, &appliedSampler);
            ID3D11Buffer* appliedConstants{};
            context->PSGetConstantBuffers(
                kConstantSlot,
                1,
                &appliedConstants);
            const auto matches = appliedResources == resources &&
                appliedSampler == sampler && appliedConstants == constants;
            for (auto* resource : appliedResources) {
                if (resource) {
                    resource->Release();
                }
            }
            if (appliedSampler) {
                appliedSampler->Release();
            }
            if (appliedConstants) {
                appliedConstants->Release();
            }
            return matches;
        }
    }

    bool initializeSslrEnvironmentBinding(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        return Service::get().initialize(device, context);
    }

    void setSslrSuiteRequested(const bool requested) noexcept
    {
        Service::get().setRequested(requested);
    }

    bool sslrSuiteReady() noexcept
    {
        return Service::get().ready();
    }

    SslrEnvironmentSnapshot sslrEnvironmentSnapshot() noexcept
    {
        return Service::get().snapshot();
    }

    ScopedSslrEnvironmentBinding::ScopedSslrEnvironmentBinding(
        ID3D11DeviceContext* context,
        const bool exactCorrectedRaytrace) noexcept
    {
        if (!context || !exactCorrectedRaytrace) {
            return;
        }

        std::array<ID3D11ShaderResourceView*, 6> resources{};
        ID3D11SamplerState* sampler{};
        ID3D11Buffer* constants{};
        bool historyAvailable{};
        if (!Service::get().prepare(
                context,
                resources,
                sampler,
                constants,
                historyAvailable)) {
            return;
        }

        context_ = context;
        std::array<ID3D11ShaderResourceView*, 6> previousResources{};
        context->PSGetShaderResources(
            kFirstResourceSlot,
            kResourceCount,
            previousResources.data());
        for (std::size_t index = 0; index < previousResources.size(); ++index) {
            previousResources_[index].Attach(previousResources[index]);
        }
        ID3D11SamplerState* previousSampler{};
        context->PSGetSamplers(kSamplerSlot, 1, &previousSampler);
        previousSampler_.Attach(previousSampler);
        ID3D11Buffer* previousConstants{};
        context->PSGetConstantBuffers(
            kConstantSlot,
            1,
            &previousConstants);
        previousConstants_.Attach(previousConstants);
        captured_ = true;

        context->PSSetShaderResources(
            kFirstResourceSlot,
            kResourceCount,
            resources.data());
        context->PSSetSamplers(kSamplerSlot, 1, &sampler);
        context->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        if (!appliedMatches(context, resources, sampler, constants)) {
            Service::get().recordBindingFailure("state application");
            // D3D setters are void. Retry the complete transaction once. If
            // the state still does not match, restore the captured stock state
            // and leave this scope inactive so the draw hook can rebind the
            // retained stock shader before issuing the draw.
            context->PSSetShaderResources(
                kFirstResourceSlot,
                kResourceCount,
                resources.data());
            context->PSSetSamplers(kSamplerSlot, 1, &sampler);
            context->PSSetConstantBuffers(kConstantSlot, 1, &constants);
            if (!appliedMatches(context, resources, sampler, constants)) {
                (void)restore();
                return;
            }
        }
        active_ = true;
        Service::get().recordBinding(historyAvailable);
    }

    ScopedSslrEnvironmentBinding::~ScopedSslrEnvironmentBinding()
    {
        (void)restore();
    }

    bool ScopedSslrEnvironmentBinding::active() const noexcept
    {
        return active_ && !restored_;
    }

    bool ScopedSslrEnvironmentBinding::restore() noexcept
    {
        if (restored_) {
            return true;
        }
        if (!captured_ || !context_) {
            restored_ = true;
            active_ = false;
            return false;
        }
        std::array<ID3D11ShaderResourceView*, 6> resources{};
        for (std::size_t index = 0; index < resources.size(); ++index) {
            resources[index] = previousResources_[index].Get();
        }
        auto* sampler = previousSampler_.Get();
        auto* constants = previousConstants_.Get();
        context_->PSSetShaderResources(
            kFirstResourceSlot,
            kResourceCount,
            resources.data());
        context_->PSSetSamplers(kSamplerSlot, 1, &sampler);
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        auto restored = appliedMatches(
            context_.Get(),
            resources,
            sampler,
            constants);
        if (!restored) {
            context_->PSSetShaderResources(
                kFirstResourceSlot,
                kResourceCount,
                resources.data());
            context_->PSSetSamplers(kSamplerSlot, 1, &sampler);
            context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
            restored = appliedMatches(
                context_.Get(),
                resources,
                sampler,
                constants);
            if (!restored) {
                Service::get().recordRestoreFailure();
            }
        }
        restored_ = true;
        active_ = false;
        return restored;
    }
}
