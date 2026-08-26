#include "PCH.h"

#include "Features/vanilla_fixes/DirectionalLightStabilityRuntime.h"

#include "Features/sky_sync/SkySyncRuntime.h"
#include "support/Logger.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace community_shaders::vanilla_fixes
{
    namespace
    {
        constexpr UINT kConstantSlot = 7;

        [[nodiscard]] bool finiteDirection(
            const std::array<float, 3>& direction) noexcept
        {
            const auto lengthSquared =
                direction[0] * direction[0] +
                direction[1] * direction[1] +
                direction[2] * direction[2];
            return std::isfinite(direction[0]) &&
                std::isfinite(direction[1]) &&
                std::isfinite(direction[2]) &&
                std::isfinite(lengthSquared) &&
                lengthSquared > 0.99f && lengthSquared < 1.01f;
        }
    }

    ScopedDirectionalLightStabilityBinding::
        ScopedDirectionalLightStabilityBinding(
            ID3D11DeviceContext* context,
            ID3D11Buffer* constants,
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
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
    }

    ScopedDirectionalLightStabilityBinding::
        ~ScopedDirectionalLightStabilityBinding() noexcept
    {
        if (!context_) {
            return;
        }
        auto* previous = previousConstants_.Get();
        context_->PSSetConstantBuffers(kConstantSlot, 1, &previous);
        if (restoreCounter_) {
            restoreCounter_->fetch_add(1, std::memory_order_relaxed);
        }
    }

    DirectionalLightStabilityRuntime&
        DirectionalLightStabilityRuntime::get() noexcept
    {
        static auto* runtime = new DirectionalLightStabilityRuntime();
        return *runtime;
    }

    void DirectionalLightStabilityRuntime::onDeviceCreated(
        ID3D11Device* device) noexcept
    {
        gpuReady_.store(false, std::memory_order_release);
        constants_.Reset();
        uploadedConstants_ = {};
        uploaded_ = false;
        sourceValid_.store(false, std::memory_order_release);
        firstBindLogged_.store(false, std::memory_order_relaxed);
        if (!device) {
            return;
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(uploadedConstants_);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto result = device->CreateBuffer(
            &description,
            nullptr,
            constants_.GetAddressOf());
        if (FAILED(result) || !constants_) {
            logging::error(
                "Stable directional lighting could not create its private b7 constants (HRESULT=0x{:08X}).",
                static_cast<unsigned>(result));
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        gpuReady_.store(true, std::memory_order_release);
        logging::info(
            "Stable directional lighting created its private b7 world-direction constants; native b2 remains the fail-closed fallback.");
    }

    void DirectionalLightStabilityRuntime::setEnabled(
        const bool enabled) noexcept
    {
        enabled_.store(enabled, std::memory_order_release);
    }

    bool DirectionalLightStabilityRuntime::requested() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            gpuReady_.load(std::memory_order_acquire);
    }

    ScopedDirectionalLightStabilityBinding
        DirectionalLightStabilityRuntime::scopeDraw(
            ID3D11DeviceContext* context,
            const bool active) noexcept
    {
        if (!context || !active || !requested() || !constants_) {
            return {};
        }

        const auto source = sky_sync::Runtime::get().snapshot();
        const auto sourceValid = source.nativeDirectionalLightValid &&
            finiteDirection(source.nativeDirectionalLightDirection);
        sourceValid_.store(sourceValid, std::memory_order_release);
        for (std::size_t index = 0;
             index < source.nativeDirectionalLightDirection.size();
             ++index) {
            worldDirectionBits_[index].store(
                std::bit_cast<std::uint32_t>(
                    source.nativeDirectionalLightDirection[index]),
                std::memory_order_relaxed);
        }
        const std::array<float, 4> constants{
            sourceValid ? source.nativeDirectionalLightDirection[0] : 0.0f,
            sourceValid ? source.nativeDirectionalLightDirection[1] : 0.0f,
            sourceValid ? source.nativeDirectionalLightDirection[2] : 0.0f,
            sourceValid ? 1.0f : 0.0f,
        };
        if (!uploaded_ ||
            std::memcmp(
                uploadedConstants_.data(),
                constants.data(),
                sizeof(constants)) != 0) {
            context->UpdateSubresource(
                constants_.Get(),
                0,
                nullptr,
                constants.data(),
                0,
                0);
            uploadedConstants_ = constants;
            uploaded_ = true;
        }

        drawScopes_.fetch_add(1, std::memory_order_relaxed);
        if (sourceValid && !firstBindLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::info(
                "Stable directional lighting bound native world direction [{:.6f},{:.6f},{:.6f}]; each DFLight eye now derives its view-space vector from the same world source.",
                constants[0],
                constants[1],
                constants[2]);
        }
        return ScopedDirectionalLightStabilityBinding(
            context,
            constants_.Get(),
            &drawRestores_);
    }

    DirectionalLightStabilitySnapshot
        DirectionalLightStabilityRuntime::snapshot() const noexcept
    {
        std::array<float, 3> worldDirection{};
        for (std::size_t index = 0; index < worldDirection.size(); ++index) {
            worldDirection[index] = std::bit_cast<float>(
                worldDirectionBits_[index].load(
                    std::memory_order_relaxed));
        }
        return {
            .enabled = enabled_.load(std::memory_order_acquire),
            .gpuReady = gpuReady_.load(std::memory_order_acquire),
            .sourceValid = sourceValid_.load(std::memory_order_acquire),
            .worldDirection = worldDirection,
            .drawScopes = drawScopes_.load(std::memory_order_relaxed),
            .drawRestores = drawRestores_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
